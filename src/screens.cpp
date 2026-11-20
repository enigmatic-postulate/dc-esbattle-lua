// screens.cpp - see screens.h
#include "screens.h"
#include "engine.h"
#include "font.h"
#include "icons.h"
#include "input.h"
#include "math3d.h"
#include "text.h"
#include "uitext.h"
#include "transitions.h"
#include <stdio.h>
#include <string.h>

namespace bt {

// ---------------------------------------------------------------------------- assets
// The logo: tools/make_logo.py typesets "PROJECT HYMN" / "The Restless Dead" with the dialog font's TTF into a 256x64
// ARGB4444 texture (the GDD's A3 stand-in; a real logo replaces the file). Header as romdisk/tex/fx.tex.
const int LOGO_W = 256, LOGO_H = 64;
alignas(32) static uint16_t g_logo_texels[LOGO_W * LOGO_H];
static eng::Sprite g_logo = 0;
// credits.txt: "# heading", a line of text, or a blank line (a gap)
const int CREDIT_LINES = 96;
static char g_credit_buf[4096];
static const char* g_credit[CREDIT_LINES];
static uint8_t g_credit_kind[CREDIT_LINES];          // 0 text, 1 heading, 2 gap
static int g_ncredits = 0;
const int CREDIT_PITCH = 26, CREDIT_GAP = 18;
const float CREDIT_SPEED = 0.5f;                     // px per tick

static void load_credits() {
    g_ncredits = 0;
    const int n = eng::file_read("/rd/scripts/credits.txt", g_credit_buf, sizeof g_credit_buf);
    if (n < 0) { eng::dbg_print("screens: /rd/scripts/credits.txt missing"); return; }
    for (char* line = g_credit_buf; line && *line && g_ncredits < CREDIT_LINES;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        int L = (int)strlen(line);
        while (L > 0 && (line[L - 1] == '\r' || line[L - 1] == ' ')) line[--L] = '\0';
        if (line[0] == '#' && line[1] == ' ') { g_credit[g_ncredits] = line + 2; g_credit_kind[g_ncredits++] = 1; }
        else if (!line[0]) { g_credit[g_ncredits] = ""; g_credit_kind[g_ncredits++] = 2; }
        else if (line[0] != ';') { g_credit[g_ncredits] = line; g_credit_kind[g_ncredits++] = 0; }   // ';' = a comment
        line = next;
    }
}

bool screens_init() {
    static bool done = false;
    if (done) return g_logo != 0;
    done = true;
    static char buf[32 + LOGO_W * LOGO_H * 2 + 4];
    const int n = eng::file_read("/rd/tex/logo.tex", buf, sizeof buf);
    uint16_t h[4] = { 0, 0, 0, 0 };
    if (n == 32 + LOGO_W * LOGO_H * 2) memcpy(h, buf, sizeof h);
    if (n != 32 + LOGO_W * LOGO_H * 2 || h[0] != LOGO_W || h[1] != LOGO_H || h[2] != 2) {
        eng::dbg_print("screens: /rd/tex/logo.tex missing or not 256x64 ARGB4444 (the title shows a text logo)");
    } else {
        memcpy(g_logo_texels, buf + 32, sizeof g_logo_texels);
        g_logo = eng::texture_create(g_logo_texels, LOGO_W, LOGO_H, 2);
    }
    load_credits();
    return g_logo != 0;
}

// Outlined text in the UI font (uitext.h: the outline is baked in, one sprite per glyph). `scale` keeps the 5x7 font's
// sizes: capitals of 7 x scale px.
static void outline_text(float x, float y, const char* s, int scale, uint32_t argb, float z, bool centre = true) {
    ui_text_px(x, y, s, 7.0f * (float)scale, argb, z, centre ? UA_CENTRE : UA_LEFT);
}

static uint32_t with_alpha(uint32_t argb, int a) {
    a = a < 0 ? 0 : (a > 255 ? 255 : a);
    return (uint32_t)(((argb >> 24) * (uint32_t)a) / 255u) << 24 | (argb & 0xFFFFFFu);
}

static void frame_rect(float x, float y, float w, float h, float t, uint32_t argb, float z) {
    eng::gfx_rect_tr(x, y, w, t, argb, z);
    eng::gfx_rect_tr(x, y + h - t, w, t, argb, z);
    eng::gfx_rect_tr(x, y + t, t, h - 2 * t, argb, z);
    eng::gfx_rect_tr(x + w - t, y + t, t, h - 2 * t, argb, z);
}

static void draw_logo(float cy, int alpha) {
    if (alpha <= 0) return;
    if (g_logo) {
        // 2x with a soft shadow (GDD 5)
        eng::sprite_draw_uv(g_logo, 320 + 4, cy + 5, LOGO_W * 2, LOGO_H * 2, 0, 0, 0, 1, 1, 2, with_alpha(0x90000000u, alpha), 6.9f);
        eng::sprite_draw_uv(g_logo, 320, cy, LOGO_W * 2, LOGO_H * 2, 0, 0, 0, 1, 1, 2, with_alpha(0xFFFFFFFFu, alpha), 7.0f);
    } else {
        outline_text(320, cy - 40, "PROJECT HYMN", 6, with_alpha(0xFFFFD890u, alpha), 7.0f);
        outline_text(320, cy + 20, "THE RESTLESS DEAD", 3, with_alpha(0xFFE0C8F0u, alpha), 7.0f);
    }
}

// ---------------------------------------------------------------------------- title
struct Title { int t, menu_t, sel, idle; bool menu, cleared, prev_stick_up, prev_stick_down; };
static Title g_t;

void title_enter(bool cleared) { memset(&g_t, 0, sizeof g_t); g_t.cleared = cleared; }
int title_idle_ticks() { return g_t.idle; }

static bool any_input() {
    for (int k = 0; k < eng::KEY_COUNT; ++k) if (input_held((eng::Key)k)) return true;
    const int x = input_stick(eng::AXIS_X), y = input_stick(eng::AXIS_Y);
    return x * x + y * y > 40 * 40;
}

TitleAction title_tick() {
    ++g_t.t;
    g_t.idle = any_input() ? 0 : g_t.idle + 1;
    if (g_t.t < 45) return TITLE_NONE;                         // the logo fades in
    if (!g_t.menu) {
        if (input_pressed(eng::KEY_START) || input_pressed(eng::KEY_FIRE)) {
            input_consume(eng::KEY_FIRE);
            g_t.menu = true; g_t.menu_t = 0; g_t.sel = 0;
            eng::audio_event("menu_confirm");                  // slice 3D-7: PRESS START answers
        }
        return TITLE_NONE;
    }
    ++g_t.menu_t;
    if (g_t.menu_t < 12) return TITLE_NONE;                    // the box grows
    const int sy = input_stick(eng::AXIS_Y);
    const bool up = input_pressed(eng::KEY_UP) || (sy <= -64 && !g_t.prev_stick_up);
    const bool down = input_pressed(eng::KEY_DOWN) || (sy >= 64 && !g_t.prev_stick_down);
    g_t.prev_stick_up = sy <= -64; g_t.prev_stick_down = sy >= 64;
    if (up && g_t.sel > 0) { --g_t.sel; eng::audio_event("menu_move"); }
    if (down && g_t.sel < 1) { ++g_t.sel; eng::audio_event("menu_move"); }
    if (input_pressed(eng::KEY_FIRE) || input_pressed(eng::KEY_START)) {
        input_consume(eng::KEY_FIRE);
        return g_t.sel == 0 ? TITLE_NEW_GAME : TITLE_CREDITS;
    }
    if (input_pressed(eng::KEY_B)) { g_t.menu = false; eng::audio_event("menu_cancel"); }   // back to PRESS START
    return TITLE_NONE;
}

void title_draw() {
    const int a = g_t.t < 45 ? 255 * g_t.t / 45 : 255;
    draw_logo(132, a);
    if (g_t.cleared && a > 0) {                                // the cleared crest (GDD 5, 14 E7)
        ui_icon(UI_TOKEN, 320 - 48, 222, 24.0f, with_alpha(0xFFFFFFFFu, a), 7.0f);
        if (text_ready()) text_draw(320 - 32, 215, "CLEARED", with_alpha(0xFFFFD070u, a), 7.0f, -1, true, a);
    }
    if (g_t.t < 45) return;
    if (!g_t.menu) {
        if ((g_t.t / 15) % 2 == 0) outline_text(320, 350, "PRESS START", 3, 0xFFF4F4F4u, 7.0f);
        return;
    }
    // menu box 240x88, gold frame (GDD 5); grows over 12 ticks
    const float grow = g_t.menu_t < 12 ? (float)(g_t.menu_t + 1) / 12.0f : 1.0f;
    const float w = 240, h = 88 * grow, x = 200, y = 318 + 44 * (1.0f - grow);
    eng::gfx_rect_tr(x, y, w, h, 0xE0101020u, 7.0f);
    frame_rect(x, y, w, h, 2, 0xFFE8B848u, 7.05f);
    if (g_t.menu_t < 12 || !text_ready()) return;
    static const char* const ITEMS[2] = { "NEW GAME", "CREDITS" };
    for (int i = 0; i < 2; ++i) {
        const float ty = y + 18 + 30 * (float)i;
        const bool sel = i == g_t.sel;
        text_draw(x + 56, ty, ITEMS[i], sel ? 0xFFFFE096u : 0xFFDCDCDCu, 7.2f, -1, true);
        if (sel) text_icon_draw(TI_CURSOR, x + 36 + (float)((g_t.t / 8) % 2), ty + 7, 1.0f, 0xFFFFFFFFu, 7.2f);
    }
}

void title_orbit(int t, float eye[3], float look[3], float* fov) {
    // the square between the houses (the road's bend by the wagon), seen from ~9 units up: a slow sweep from side to
    // side, there and back in 3,600 ticks. Not a full circle: the village's houses and props are VS1's, cut for the
    // field camera (it looks north and never turns: no back walls, no faces turned away - docs/SLICE_3D_VILLAGE.md
    // "camera-cone culling", yaw +-30 degrees), so a camera north of a house looked straight into it, and the full
    // loop also flew through the stone house's roof. The sweep stays inside that cone (+-26 degrees, eased at the ends).
    const float cx = 18.0f, cz = 13.0f, r = 9.5f, h = 7.5f;
    const float SWEEP = 0.45f;                                              // radians either side of due south
    const float a = SWEEP * shz_sinf(6.28318531f * (float)(t % 3600) / 3600.0f);
    const shz_sincos_t sc = shz_sincosf(a);
    eye[0] = cx + sc.sin * r; eye[1] = h + 0.8f * sc.cos; eye[2] = cz + sc.cos * r;
    look[0] = cx; look[1] = 0.8f; look[2] = cz;
    *fov = 40.0f;
}

// ---------------------------------------------------------------------------- cards
void card_draw(const char* text, int t, int len, bool logo) {
    const int a = t < 20 ? 255 * t / 20 : (len - t < 20 ? 255 * (len - t) / 20 : 255);
    eng::gfx_rect_tr(0, 0, 640, 480, with_alpha(0x90000000u, a), 7.5f);
    if (logo) draw_logo(150, a);
    outline_text(320, logo ? 260 : 220, text, 5, with_alpha(0xFFFFE8B0u, a), 7.6f);
}

void namecard_draw(const char* text, int t, int len) {
    const float a = t < 15 ? (float)t / 15.0f : (len - t < 20 ? (float)(len - t) / 20.0f : 1.0f);
    const int ai = (int)(a * 255.0f);
    eng::gfx_rect_tr(0, 150, 640, 70, with_alpha(0xB3080410u, ai), 7.0f);
    outline_text(320, 168, text, 4, with_alpha(0xFFE0B0FFu, ai), 7.1f);
    eng::gfx_rect_tr(160, 206, 320 * (t < 30 ? (float)t / 30.0f : 1.0f), 2, with_alpha(0xFFA060E0u, ai), 7.05f);
}

void t2_card_draw(int t) {
    // T2's text card: on black, 90 ticks (fades in 15, out 15)
    eng::gfx_rect(0, 0, 640, 480, 0xFF000000u, 9.0f);
    const int a = t < 15 ? 255 * t / 15 : (90 - t < 15 ? 255 * (90 - t) / 15 : 255);
    trans_draw_card_text("Hollowmere.", 206, a, 0xE8D8B8);
    trans_draw_card_text("The night the barrow woke.", 236, a, 0xE8D8B8);
}

// ---------------------------------------------------------------------------- death screen
struct Death { DeathTip tip; int sel; bool prev_up, prev_down; };
static Death g_d;

void death_enter(DeathTip tip) { g_d.tip = tip; g_d.sel = 0; g_d.prev_up = g_d.prev_down = false; }
int death_choice() { return g_d.sel; }

DeathAction death_tick(int t) {
    if (t < DEATH_INPUT_AT) return DEATH_NONE;                 // a mashed A does not skip the moment (GDD 13)
    const int sy = input_stick(eng::AXIS_Y);
    const bool up = input_pressed(eng::KEY_UP) || (sy <= -64 && !g_d.prev_up);
    const bool down = input_pressed(eng::KEY_DOWN) || (sy >= 64 && !g_d.prev_down);
    g_d.prev_up = sy <= -64; g_d.prev_down = sy >= 64;
    if (up && g_d.sel > 0) { --g_d.sel; eng::audio_event("menu_move"); }
    if (down && g_d.sel < 1) { ++g_d.sel; eng::audio_event("menu_move"); }
    if (input_pressed(eng::KEY_FIRE) || input_pressed(eng::KEY_START)) {
        input_consume(eng::KEY_FIRE);
        return g_d.sel == 0 ? DEATH_RETRY : DEATH_TITLE;
    }
    return DEATH_NONE;
}

void death_draw(int t) {
    // T9: the frame tints toward grey-red over 40 ticks with a dark vignette; the title fades in over 30
    const float w = t < 40 ? (float)t / 40.0f : 1.0f;
    trans_draw_tint(0xB08C88, w, (uint32_t)(70.0f * w) << 24 | 0x3A0C0C, (int)(210.0f * w));
    if (t >= 40) {
        const int a = t < 70 ? 255 * (t - 40) / 30 : 255;
        outline_text(320, 150, "YOU HAVE FALLEN", 5, with_alpha(0xFFE86050u, a), Z_TRANS_TEXT);
    }
    if (t < DEATH_OPTIONS_AT || !text_ready()) return;
    const int k = t - DEATH_OPTIONS_AT;
    const float grow = k < 12 ? (float)(k + 1) / 12.0f : 1.0f;
    const float bw = 320, bh = 88 * grow, bx = 160, by = 262 + 44 * (1.0f - grow);
    eng::gfx_rect_tr(bx, by, bw, bh, 0xE0100808u, Z_TRANS_TEXT - 0.1f);
    frame_rect(bx, by, bw, bh, 2, 0xFF9A4A40u, Z_TRANS_TEXT - 0.05f);
    if (k < 12) return;
    static const char* const OPT[2] = { "RETRY FROM CHECKPOINT", "RETURN TO TITLE" };
    const bool live = t >= DEATH_INPUT_AT;
    for (int i = 0; i < 2; ++i) {
        const float ty = by + 18 + 30 * (float)i;
        const bool sel = i == g_d.sel;
        const uint32_t c = sel ? 0xFFFFE096u : 0xFFC8C0C0u;
        text_draw(bx + 52, ty, OPT[i], live ? c : with_alpha(c, 150), Z_TRANS_TEXT, -1, true);
        if (sel && live) text_icon_draw(TI_CURSOR, bx + 32 + (float)((t / 8) % 2), ty + 7, 1.0f, 0xFFFFFFFFu, Z_TRANS_TEXT);
    }
    static const char* const TIP[3] = { "When the Warden gathers the dark, raise your shield.", "Fireball burns every enemy at once.",
                                        "Potions restore 12 HP." };
    const char* tip = TIP[g_d.tip];
    text_draw((float)(int)(320.0f - (float)text_width(tip) * 0.5f), by + bh + 22, tip, 0xFFB0A8A8u, Z_TRANS_TEXT, -1, true);
}

// ---------------------------------------------------------------------------- credits
static int credits_height() {
    int h = 0;
    for (int i = 0; i < g_ncredits; ++i) h += g_credit_kind[i] == 2 ? CREDIT_GAP : CREDIT_PITCH;
    return h;
}
int credits_length() { return (int)((float)(480 + credits_height() + 40) / CREDIT_SPEED); }

void credits_enter() {}

bool credits_tick(int t) {
    if (input_pressed(eng::KEY_START)) { input_consume(eng::KEY_START); return true; }   // Start skips to the results
    return t >= credits_length();
}

void credits_draw(int t) {
    eng::gfx_rect_tr(0, 0, 640, 480, 0x60000000u, 7.0f);      // dims the flyover behind the text
    float y = 480.0f + 10.0f - (float)t * CREDIT_SPEED;
    for (int i = 0; i < g_ncredits; ++i) {
        const int kind = g_credit_kind[i];
        const int step = kind == 2 ? CREDIT_GAP : CREDIT_PITCH;
        if (kind != 2 && y > -30.0f && y < 480.0f && text_ready()) {
            // fade near the top and bottom edges
            const int a = y < 60.0f ? (int)(255.0f * (y + 30.0f) / 90.0f) : (y > 400.0f ? (int)(255.0f * (480.0f - y) / 80.0f) : 255);
            const uint32_t c = kind == 1 ? 0xFFFFD070u : 0xFFF0F0F0u;
            trans_draw_card_text(g_credit[i], (float)(int)y, a, c);
        }
        y += (float)step;
    }
    if (t > 60) {                                              // the skip hint, as the battle intro's
        ui_icon(UI_PAD_START, 554, 453, 24.0f, 0xFFFFFFFFu, 7.5f);
        ui_text(UF_S, 570, 446, "SKIP", 0xFFC0C0C0u, 7.5f);
    }
}

void credits_flyover(int t, float eye[3], float look[3], float* fov) {
    // along the village road from the west gate to the east end and back, 12 up, looking north-east (~60 s each way)
    const int T = 3600;
    const int k = t % (2 * T);
    const float u0 = (float)(k < T ? k : 2 * T - k) / (float)T;
    const float u = u0 * u0 * (3.0f - 2.0f * u0);
    const float x = 7.0f + 26.0f * u;
    eye[0] = x - 2.0f; eye[1] = 9.0f; eye[2] = 25.0f;
    look[0] = x + 1.0f; look[1] = 0.5f; look[2] = 13.0f;
    *fov = 38.0f;
}

// ---------------------------------------------------------------------------- results
static SessionStats g_rs;
Rank results_rank(const SessionStats& s) {
    const int min = s.play_ticks / 3600;
    if (min < 20 && s.retries == 0) return RANK_GOLD;
    if (min < 30 && s.retries <= 2) return RANK_SILVER;
    return RANK_BRONZE;
}
void results_enter(const SessionStats& s) { g_rs = s; }
bool results_tick(int t) {
    if (t < 60) return false;
    if (input_pressed(eng::KEY_FIRE)) { input_consume(eng::KEY_FIRE); return true; }
    return false;
}

void results_draw(int t) {
    const int a = t < 30 ? 255 * t / 30 : 255;
    eng::gfx_rect_tr(0, 0, 640, 480, with_alpha(0xC0000000u, a), 7.0f);
    const float x = 140, y = 70, w = 360, h = 340;
    eng::gfx_rect_tr(x, y, w, h, with_alpha(0xF0101020u, a), 7.1f);
    frame_rect(x, y, w, h, 2, with_alpha(0xFFE8B848u, a), 7.15f);
    outline_text(320, y + 18, "RESULTS", 3, with_alpha(0xFFFFD070u, a), 7.3f);
    if (!text_ready()) return;
    char v[7][24];
    const int secs = g_rs.play_ticks / 60;
    snprintf(v[0], sizeof v[0], "%d:%02d", secs / 60, secs % 60);
    snprintf(v[1], sizeof v[1], "%d", g_rs.battles_won);
    snprintf(v[2], sizeof v[2], "%d", g_rs.hero_turns);
    snprintf(v[3], sizeof v[3], "%d", g_rs.dmg_dealt);
    snprintf(v[4], sizeof v[4], "%d", g_rs.dmg_taken);
    snprintf(v[5], sizeof v[5], "%d", g_rs.potions_used);
    snprintf(v[6], sizeof v[6], "%d", g_rs.retries);
    static const char* const K[7] = { "Play time", "Battles won", "Turns taken", "Damage dealt", "Damage taken", "Potions used", "Retries" };
    for (int i = 0; i < 7; ++i) {
        const float ly = y + 62 + 26 * (float)i;
        text_draw(x + 40, ly, K[i], with_alpha(0xFFD8D8D8u, a), 7.3f, -1, true, a);
        text_draw(x + w - 40 - (float)text_width(v[i]), ly, v[i], with_alpha(0xFFFFFFFFu, a), 7.3f, -1, true, a);
    }
    static const char* const RN[3] = { "GOLD", "SILVER", "BRONZE" };
    static const uint32_t RC[3] = { 0xFFFFD050u, 0xFFD8D8E8u, 0xFFD89058u };
    const Rank r = results_rank(g_rs);
    outline_text(320, y + 252, RN[r], 4, with_alpha(RC[r], a), 7.3f);
    trans_draw_card_text("Thank you for playing", y + h - 44, a, 0xE8D8B8);
    if (t >= 60 && (t / 20) % 2 == 0) outline_text(320, y + h + 14, "PRESS A", 2, 0xFFF4F4F4u, 7.3f);
}

}
