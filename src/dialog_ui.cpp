// dialog_ui.cpp - see dialog_ui.h
#include "dialog_ui.h"
#include "engine.h"
#include "input.h"
#include "portrait.h"
#include "script.h"
#include "text.h"
#include <string.h>

namespace bt {

// ---- layout (GDD 7.1; docs/NEXT_SLICE_DIALOG.md 5.2)
const float BOX_X = 24, BOX_Y = 318, BOX_W = 592, BOX_H = 118;
const uint32_t BOX_ARGB = 0xE0101020u;                 // alpha 0.88 (224/255)
const uint32_t BOX_EDGE = 0xFF7A6446u;
const float PORT_X = 31, PORT_Y = 325, PORT_S = 104, FRAME = 2;
const uint32_t GOLD = 0xFFE8B848u;
const float TEXT_X = 150, TEXT_Y = 334;
const float PLATE_X = 144, PLATE_Y = 300, PLATE_W = 96, PLATE_H = 22;
const float ADV_X = 601, ADV_Y = 421;
const float CHOICE_RIGHT = 616, CHOICE_GAP = 24, CHOICE_MIN_W = 236, CHOICE_ROW = 26;
const uint32_t TEXT_ARGB = 0xFFF4F4F4u, SYS_ARGB = 0xFFFFCD75u, OPT_ARGB = 0xFFDCDCDCu, OPT_SEL_ARGB = 0xFFFFE096u;
// depth layers (engine.h: larger z = nearer)
const float Z_FADE = 2.9f, Z_BOX = 3.0f, Z_EDGE = 3.05f, Z_FRAME = 3.1f, Z_PORTRAIT = 3.2f, Z_PLATE = 3.3f,
            Z_TEXT = 3.5f, Z_CHOICE = 3.6f, Z_CHOICE_TEXT = 3.8f, Z_BARS = 8.0f;

struct Dlg {
    bool open, closing;
    int  npc, t;
    int  bars_from;            // closing: the bar height when T4 began
    float cam_from;            // closing: the camera weight when T4 began
    bool letterbox;
    int  page_pc, page_t, shown, total, pause;
    int  speaker, variant, slot, slide_t;
    int  last_speaker, last_slot;
    int  choice_pc, sel, choice_t, prev_stick;
    int  fade_level, fade_from, fade_to, fade_t, fade_len;
    int  rowan;                // speaker index of Rowan (his portrait slot is 1)
    int  seq;                  // tick counter for bobbing
};
static Dlg g_d;

static float ease(float u) { u = u < 0 ? 0 : (u > 1 ? 1 : u); return u * u * (3.0f - 2.0f * u); }
static float ease_out(float u) { u = u < 0 ? 0 : (u > 1 ? 1 : u); return 1.0f - (1.0f - u) * (1.0f - u); }

bool dialog_open() { return g_d.open; }
bool dialog_visible() { return g_d.open || (g_d.closing && dialog_bars_px() > 0); }
int  dialog_npc() { return g_d.npc; }
int  dialog_t() { return g_d.t; }

int dialog_bars_px() {
    if (g_d.open) {
        if (!g_d.letterbox) return 0;
        const int k = g_d.t + 1 < DLG_BARS_TICKS ? g_d.t + 1 : DLG_BARS_TICKS;
        return (int)(DLG_BAR_PX * ease_out((float)k / DLG_BARS_TICKS) + 0.5f);
    }
    if (g_d.closing) {
        const int k = g_d.t + 1 < DLG_BARS_TICKS ? g_d.t + 1 : DLG_BARS_TICKS;
        return (int)((float)g_d.bars_from * (1.0f - ease_out((float)k / DLG_BARS_TICKS)) + 0.5f);
    }
    return 0;
}

float dialog_cam_weight() {
    if (g_d.npc < 0) return 0.0f;
    const int k = g_d.t < DLG_CAM_TICKS ? g_d.t : DLG_CAM_TICKS;
    if (g_d.open) return ease((float)k / DLG_CAM_TICKS);
    if (g_d.closing) return g_d.cam_from * (1.0f - ease((float)k / DLG_CAM_TICKS));
    return 0.0f;
}

bool dialog_hides_hud() { return g_d.open || (g_d.closing && dialog_bars_px() > 0); }

// ---------------------------------------------------------------------------- text helpers
// The k-th visible character of s (colour codes and '\n' skipped); 0 past the end.
static char visible_char(const char* s, int k) {
    for (int i = 0; s[i];) {
        if (s[i] == '{') { const char* e = strchr(s + i, '}'); if (e) { i = (int)(e - s) + 1; continue; } }
        if (s[i] == '\n') { ++i; continue; }
        if (k == 0) return s[i];
        --k; ++i;
    }
    return 0;
}
static bool stop_char(char c) { return c == '.' || c == '!' || c == '?'; }

// The op reached from pc by following unconditional jumps (what comes "next" after a page).
static int follow(int pc) {
    for (int guard = 0; guard < 16; ++guard) {
        const ScrOp& o = script_op(pc);
        if (o.code != SC_JMP) return pc;
        pc = o.b;
    }
    return pc;
}

static void start_page(int pc) {
    const ScrOp& o = script_op(pc);
    g_d.page_pc = pc; g_d.page_t = 0; g_d.shown = 0; g_d.pause = 0; g_d.total = o.c;
    if (o.code == SC_PAGE) {
        g_d.speaker = o.a; g_d.variant = o.b;
        g_d.slot = (o.a == g_d.rowan) ? 1 : 0;
        portrait_request(g_d.slot, o.a, o.b);
        g_d.slide_t = (g_d.slot != g_d.last_slot || g_d.speaker != g_d.last_speaker) ? 0 : DLG_SLIDE_TICKS;
        g_d.last_slot = g_d.slot; g_d.last_speaker = g_d.speaker;
    } else {
        g_d.speaker = -1; g_d.variant = 0; g_d.slot = -1; g_d.slide_t = DLG_SLIDE_TICKS;
        g_d.last_slot = -1; g_d.last_speaker = -1;
    }
}

static void end_session() {
    g_d.bars_from = dialog_bars_px();
    g_d.cam_from = dialog_cam_weight();
    g_d.open = false; g_d.closing = true; g_d.t = 0;
    g_d.page_pc = -1; g_d.choice_pc = -1;
}

void dialog_begin(int npc) {
    memset(&g_d, 0, sizeof g_d);
    g_d.open = true; g_d.npc = npc; g_d.t = 0; g_d.letterbox = true;
    g_d.page_pc = -1; g_d.choice_pc = -1; g_d.speaker = -1; g_d.slot = -1; g_d.last_speaker = -1; g_d.last_slot = -1;
    g_d.rowan = script_speaker_find("rowan");
    g_d.fade_len = 0;
    // preload the first page's portrait while the bars come in (T3)
    const int pc = script_blocking_pc();
    if (pc >= 0 && script_op(pc).code == SC_PAGE) {
        const ScrOp& o = script_op(pc);
        portrait_request(o.a == g_d.rowan ? 1 : 0, o.a, o.b);
    }
}

void dialog_reset() { memset(&g_d, 0, sizeof g_d); g_d.npc = -1; g_d.page_pc = -1; g_d.choice_pc = -1; }

void dialog_fade(bool out, int ticks) {
    g_d.fade_from = g_d.fade_level; g_d.fade_to = out ? 255 : 0; g_d.fade_t = 0; g_d.fade_len = ticks > 0 ? ticks : 1;
}
void dialog_letterbox(bool on) { g_d.letterbox = on; }

static void fade_step() {
    if (g_d.fade_t < g_d.fade_len) {
        ++g_d.fade_t;
        g_d.fade_level = g_d.fade_from + (g_d.fade_to - g_d.fade_from) * g_d.fade_t / g_d.fade_len;
    }
}

static void typewriter(bool fast) {
    if (g_d.shown >= g_d.total) return;
    if (g_d.pause > 0) { --g_d.pause; return; }
    const char* text = script_str(script_op(g_d.page_pc).str);
    const int n = fast ? DLG_FAST : 1;
    for (int k = 0; k < n && g_d.shown < g_d.total; ++k) {
        const char c = visible_char(text, g_d.shown);
        ++g_d.shown;
        if (g_d.shown % 3 == 1) eng::audio_event("text_blip");      // slice 3D-6: audio hooks (S12)
        if (g_d.shown >= g_d.total) break;                          // no pause at the end of a page
        const char nx = visible_char(text, g_d.shown);
        if (stop_char(c) && !stop_char(nx)) { g_d.pause = fast ? (DLG_PAUSE_STOP + DLG_FAST - 1) / DLG_FAST : DLG_PAUSE_STOP; break; }
        if (c == ',' && nx != ',') { g_d.pause = fast ? (DLG_PAUSE_COMMA + DLG_FAST - 1) / DLG_FAST : DLG_PAUSE_COMMA; break; }
    }
}

// A page shows its first glyph on the first tick it is visible, however it was reached (an A press, a
// choice, the end of a wait or a fade): the typewriter's first step runs in that tick.
static void first_glyph() {
    if (g_d.t < DLG_BOX_AT) return;
    ++g_d.page_t;
    typewriter(input_held(eng::KEY_B));
}

static bool dialog_tick_inner();

bool dialog_tick() {
    const bool open = dialog_tick_inner();
    if (open) fade_step();          // last: a fade started anywhere in this tick (script or input) takes its first step now
    return open;
}

static bool dialog_tick_inner() {
    if (!g_d.open) return false;
    ++g_d.t; ++g_d.seq;
    if (g_d.slide_t < DLG_SLIDE_TICKS && g_d.t >= DLG_BOX_AT) ++g_d.slide_t;
    if (!script_running()) { end_session(); return false; }
    const bool box = g_d.t >= DLG_BOX_AT;
    int pc = script_blocking_pc();
    if (pc < 0) { end_session(); return false; }
    uint8_t code = script_op(pc).code;
    if (code == SC_PAGE || code == SC_SYS) {
        if (pc != g_d.page_pc) start_page(pc);
        if (box) {
            ++g_d.page_t;
            if (input_pressed(eng::KEY_FIRE)) {
                input_consume(eng::KEY_FIRE);                           // the press is used up (GDD 4)
                if (g_d.shown < g_d.total) { g_d.shown = g_d.total; g_d.pause = 0; }   // A completes the page...
                else {                                                  // ...then A advances
                    script_page_done();                                 // the VM runs on until it blocks or ends
                    if (!script_running()) { end_session(); return false; }
                    const int p2 = script_blocking_pc();
                    const uint8_t c2 = script_op(p2).code;
                    if (c2 == SC_PAGE || c2 == SC_SYS) { start_page(p2); first_glyph(); }
                    else g_d.page_pc = -1;
                    return true;
                }
            } else {
                typewriter(input_held(eng::KEY_B));
            }
            // a page followed by a choice: the choice opens as soon as the page is complete (the page stays)
            if (g_d.shown >= g_d.total && script_op(follow(pc + 1)).code == SC_CHOICE) script_page_done();
        }
        pc = script_blocking_pc();
        if (pc < 0) return script_running();
        code = script_op(pc).code;
        if (code != SC_CHOICE) return true;
    }
    if (code == SC_CHOICE) {
        const ScrOp& c = script_op(pc);
        if (pc != g_d.choice_pc) { g_d.choice_pc = pc; g_d.sel = 0; g_d.choice_t = 0; g_d.prev_stick = input_stick(eng::AXIS_Y); return true; }
        if (!box) return true;
        ++g_d.choice_t;
        const int sy = input_stick(eng::AXIS_Y);
        const bool up = input_pressed(eng::KEY_UP) || (sy <= -DLG_STICK_STEP && g_d.prev_stick > -DLG_STICK_STEP);
        const bool down = input_pressed(eng::KEY_DOWN) || (sy >= DLG_STICK_STEP && g_d.prev_stick < DLG_STICK_STEP);
        g_d.prev_stick = sy;
        if (up && g_d.sel > 0) --g_d.sel;                       // no wrap
        if (down && g_d.sel < c.a - 1) ++g_d.sel;
        int pick = -1;
        if (input_pressed(eng::KEY_FIRE)) { input_consume(eng::KEY_FIRE); pick = g_d.sel; }
        else if (input_pressed(eng::KEY_B) && c.b != NO_CANCEL) pick = c.b;   // a fresh B only (an edge after it opened)
        if (pick >= 0) {
            g_d.choice_pc = -1; g_d.page_pc = -1;
            script_choose(pick);
            if (!script_running()) { end_session(); return false; }
            const int p2 = script_blocking_pc();
            const uint8_t c2 = script_op(p2).code;
            if (c2 == SC_PAGE || c2 == SC_SYS) { start_page(p2); first_glyph(); }
        }
        return true;
    }
    // WAIT / FADE: the VM counts (script_tick); the box is hidden
    g_d.page_pc = -1;
    return true;
}

void dialog_tick_closing() {
    if (!g_d.closing) return;
    ++g_d.t; ++g_d.seq;
    if (g_d.fade_level > 0) g_d.fade_level = g_d.fade_level > 24 ? g_d.fade_level - 24 : 0;
    if (g_d.t >= DLG_CAM_TICKS && g_d.t >= DLG_BARS_TICKS && g_d.fade_level == 0) { g_d.closing = false; g_d.npc = -1; }
}

// ---------------------------------------------------------------------------- drawing
static void draw_lines(const char* s, float x, float y, int shown, uint32_t argb, bool centred) {
    // line by line (centring); the glyph budget runs across lines
    int budget = shown;
    const char* line = s;
    for (;;) {
        const char* nl = strchr(line, '\n');
        const int len = nl ? (int)(nl - line) : (int)strlen(line);
        char buf[200];
        const int L = len < (int)sizeof buf - 1 ? len : (int)sizeof buf - 1;
        memcpy(buf, line, (size_t)L); buf[L] = '\0';
        const float lx = centred ? (float)(int)(320.0f - (float)text_width(buf) * 0.5f) : x;
        text_draw(lx, y, buf, argb, Z_TEXT, budget, true);
        budget -= text_glyph_count(buf);
        if (!nl || budget <= 0) break;
        line = nl + 1;
        y += (float)text_line_pitch();
    }
}

static void rect_frame(float x, float y, float w, float h, float t, uint32_t argb, float z) {
    eng::gfx_rect_tr(x, y, w, t, argb, z);
    eng::gfx_rect_tr(x, y + h - t, w, t, argb, z);
    eng::gfx_rect_tr(x, y + t, t, h - 2 * t, argb, z);
    eng::gfx_rect_tr(x + w - t, y + t, t, h - 2 * t, argb, z);
}

static void draw_choice(const ScrOp& c, int pc) {
    int widest = 0;
    for (int k = 0; k < c.a; ++k) { const int w = text_width(script_str(script_op(pc + 1 + k).str)); widest = w > widest ? w : widest; }
    const float w = (float)widest + 44.0f > CHOICE_MIN_W ? (float)widest + 44.0f : CHOICE_MIN_W;
    const float h = 14.0f + CHOICE_ROW * (float)c.a;
    const float x = CHOICE_RIGHT - w, y = BOX_Y - CHOICE_GAP - h;
    eng::gfx_rect_tr(x, y, w, h, BOX_ARGB, Z_CHOICE);
    rect_frame(x, y, w, h, 2, BOX_EDGE, Z_CHOICE + 0.05f);
    for (int k = 0; k < c.a; ++k) {
        const float ty = y + 7.0f + CHOICE_ROW * (float)k + 6.0f;
        const bool sel = k == g_d.sel;
        text_draw(x + 28.0f, ty, script_str(script_op(pc + 1 + k).str), sel ? OPT_SEL_ARGB : OPT_ARGB, Z_CHOICE_TEXT, -1, true);
        if (sel) {
            const float bob = (float)((g_d.seq / 8) % 2);
            text_icon_draw(TI_CURSOR, x + 14.0f + bob, ty + 7.0f, 1.0f, 0xFFFFFFFFu, Z_CHOICE_TEXT);
        }
    }
}

void dialog_draw() {
    const int bars = dialog_bars_px();
    if (bars > 0) {
        eng::gfx_rect(0, 0, 640, (float)bars, 0xFF000000u, Z_BARS);
        eng::gfx_rect(0, 480.0f - (float)bars, 640, (float)bars, 0xFF000000u, Z_BARS);
    }
    if (g_d.fade_level > 0) eng::gfx_rect_tr(0, 0, 640, 480, (uint32_t)g_d.fade_level << 24, Z_FADE);
    if (!g_d.open || g_d.t < DLG_BOX_AT) return;
    const int bpc = script_blocking_pc();
    if (bpc < 0) return;
    const ScrOp& bo = script_op(bpc);
    const bool choice = bo.code == SC_CHOICE;
    if (g_d.page_pc < 0 && !choice) return;                      // a wait or a fade: no box
    eng::gfx_rect_tr(BOX_X, BOX_Y, BOX_W, BOX_H, BOX_ARGB, Z_BOX);
    rect_frame(BOX_X, BOX_Y, BOX_W, BOX_H, 2, BOX_EDGE, Z_EDGE);
    if (g_d.page_pc >= 0) {
        const ScrOp& o = script_op(g_d.page_pc);
        const char* text = script_str(o.str);
        if (o.code == SC_PAGE) {
            const float dx = -(1.0f - ease((float)g_d.slide_t / DLG_SLIDE_TICKS)) * 150.0f;
            eng::gfx_rect(PORT_X - FRAME + dx, PORT_Y - FRAME, PORT_S + 2 * FRAME, PORT_S + 2 * FRAME, GOLD, Z_FRAME);
            if (portrait_ready(g_d.slot)) {
                eng::sprite_draw_uv(portrait_sprite(g_d.slot), PORT_X + dx + PORT_S * 0.5f, PORT_Y + PORT_S * 0.5f, PORT_S, PORT_S, 0.0f,
                                    0, 0, 1, 1, 2, 0xFFFFFFFFu, Z_PORTRAIT);
                portrait_drawn(g_d.slot);
            } else {
                eng::gfx_rect(PORT_X + dx, PORT_Y, PORT_S, PORT_S, 0xFF181420u, Z_PORTRAIT);   // blink / missing file
            }
            const Speaker& sp = script_speaker(o.a);
            const float pw = (float)text_width(sp.name) + 20.0f > PLATE_W ? (float)text_width(sp.name) + 20.0f : PLATE_W;
            eng::gfx_rect_tr(PLATE_X, PLATE_Y, pw, PLATE_H, 0xF0000000u | sp.rgb, Z_PLATE);
            text_draw((float)(int)(PLATE_X + (pw - (float)text_width(sp.name)) * 0.5f), PLATE_Y + 4.0f, sp.name, 0xFFFFFFFFu, Z_PLATE + 0.15f, -1, true);
            draw_lines(text, TEXT_X, TEXT_Y, g_d.shown, TEXT_ARGB, false);
        } else {
            int lines = 1;
            for (const char* c = text; *c; ++c) lines += *c == '\n';
            const float block = (float)((lines - 1) * text_line_pitch() + text_ink_height());
            draw_lines(text, 0, (float)(int)(BOX_Y + (BOX_H - block) * 0.5f), g_d.shown, SYS_ARGB, true);
        }
        const bool complete = g_d.shown >= g_d.total;
        if (complete && !choice && script_op(follow(g_d.page_pc + 1)).code != SC_CHOICE) {
            const float bob = (float)(((g_d.seq % 30) < 15) ? 0 : 3);   // 3 px over a 30-tick period
            text_icon_draw(TI_ADVANCE, ADV_X, ADV_Y + bob, 1.0f, 0xFFFFFFFFu, Z_TEXT);
        }
    }
    if (choice) draw_choice(bo, bpc);
}

DialogInfo dialog_info() {
    DialogInfo i;
    memset(&i, 0, sizeof i);
    i.open = g_d.open; i.closing = g_d.closing; i.t = g_d.t; i.bars = dialog_bars_px();
    const int bpc = script_blocking_pc();
    const bool choice = g_d.open && bpc >= 0 && script_op(bpc).code == SC_CHOICE;
    i.box = g_d.open && g_d.t >= DLG_BOX_AT && bpc >= 0 && (g_d.page_pc >= 0 || choice);
    i.page_pc = g_d.page_pc; i.shown = g_d.shown; i.total = g_d.total; i.pause = g_d.pause;
    i.complete = g_d.page_pc >= 0 && g_d.shown >= g_d.total;
    i.marker = i.box && i.complete && !choice && script_op(follow(g_d.page_pc + 1)).code != SC_CHOICE;
    i.choice_pc = choice ? bpc : -1; i.sel = g_d.sel;
    i.n_opts = choice ? script_op(bpc).a : 0; i.cancel = choice ? script_op(bpc).b : 0;
    i.speaker = g_d.page_pc >= 0 ? g_d.speaker : -1; i.variant = g_d.variant; i.slot = g_d.slot;
    i.slide = DLG_SLIDE_TICKS - g_d.slide_t;
    i.portrait_shown = i.box && g_d.page_pc >= 0 && g_d.slot >= 0 && portrait_ready(g_d.slot);
    i.fade = g_d.fade_level;
    i.cam_w1000 = (int)(dialog_cam_weight() * 1000.0f + 0.5f);
    return i;
}

}
