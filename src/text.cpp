// text.cpp - see text.h
#include "text.h"
#include <stdio.h>
#include <string.h>

namespace bt {

struct FntHeader { char magic[4]; uint8_t version, line_pitch, ink_h, first, num_glyphs, num_icons, space, shadow;
                   uint16_t tex_w, tex_h; };
struct FntGlyph  { uint8_t u, v, w, h, adv; int8_t x_off, y_off; uint8_t pad; };
struct FntIcon   { uint8_t u, v, w, h; };
static_assert(sizeof(FntHeader) == 16 && sizeof(FntGlyph) == 8 && sizeof(FntIcon) == 4, "dialog.fnt layout");

const int TEX_W = 256, TEX_H = 128;
static FntHeader g_h;
static FntGlyph  g_glyph[96];
static FntIcon   g_icon[16];
static bool      g_ok = false, g_tried = false;
static eng::Sprite g_tex = 0;
alignas(32) static uint16_t g_texels[TEX_W * TEX_H];

bool text_ready() { return g_ok; }
int  text_line_pitch() { return g_ok ? g_h.line_pitch : 22; }
int  text_ink_height() { return g_ok ? g_h.ink_h : 14; }
eng::Sprite text_texture() { return g_tex; }

bool text_init() {
    if (g_tried) return g_ok;
    g_tried = true;
    static char fnt[1024];
    const int n = eng::file_read("/rd/fonts/dialog.fnt", fnt, sizeof fnt);
    const int need = (int)(sizeof(FntHeader) + 96 * sizeof(FntGlyph) + 16 * sizeof(FntIcon));
    if (n != need) { eng::dbg_print("text: /rd/fonts/dialog.fnt missing or wrong size"); return false; }
    memcpy(&g_h, fnt, sizeof g_h);
    if (memcmp(g_h.magic, "FNT1", 4) || g_h.version != 1 || g_h.first != 32 || g_h.num_glyphs != 96 || g_h.num_icons != 16 ||
        g_h.tex_w != TEX_W || g_h.tex_h != TEX_H || g_h.line_pitch == 0) {
        eng::dbg_print("text: dialog.fnt bad header");
        return false;
    }
    memcpy(g_glyph, fnt + sizeof g_h, sizeof g_glyph);
    memcpy(g_icon, fnt + sizeof g_h + sizeof g_glyph, sizeof g_icon);
    for (const FntGlyph& g : g_glyph)
        if (g.u + g.w > TEX_W || g.v + g.h > TEX_H / 2) { eng::dbg_print("text: dialog.fnt glyph outside the atlas"); return false; }
    for (const FntIcon& ic : g_icon)
        if (ic.u + ic.w > TEX_W || ic.v + ic.h > TEX_H) { eng::dbg_print("text: dialog.fnt icon outside the atlas"); return false; }
    // the atlas: .dmf texture header + 256x128 ARGB4444 texels (like romdisk/tex/fx.tex)
    static char buf[32 + TEX_W * TEX_H * 2 + 4];
    const int t = eng::file_read("/rd/fonts/dialog.tex", buf, sizeof buf);
    uint16_t hw[4];
    if (t != 32 + TEX_W * TEX_H * 2) { eng::dbg_print("text: /rd/fonts/dialog.tex missing or wrong size"); return false; }
    memcpy(hw, buf, sizeof hw);
    if (hw[0] != TEX_W || hw[1] != TEX_H || hw[2] != 2) { eng::dbg_print("text: dialog.tex is not 256x128 ARGB4444"); return false; }
    memcpy(g_texels, buf + 32, sizeof g_texels);
    g_tex = eng::texture_create(g_texels, TEX_W, TEX_H, 2);
    // the credits roll draws up to ~690 glyph sprites (shadowed) in a frame: more than an alpha texture's default queue
    // of 640 - reserved at boot so that KOS never grows it (tools/ui/sprite_cost.py: per-texture peaks)
    if (g_tex) eng::sprite_reserve(g_tex, 1024, 0);
    if (!g_tex) { eng::dbg_print("text: no texture slot for the dialog font"); return false; }
    g_ok = true;
    char msg[96];
    snprintf(msg, sizeof msg, "text: dialog font ok (line pitch %d, capitals %d px, space %d px)", g_h.line_pitch, g_h.ink_h, g_h.space);
    eng::boot_log(msg);                   // success is silent in traces (the battle goldens' boot lines are unchanged)
    return true;
}

static const FntGlyph* glyph(char c) {
    const unsigned k = (unsigned char)c;
    if (!g_ok || k < 32 || k >= 128) return nullptr;
    return &g_glyph[k - 32];
}

int text_advance(char c) { const FntGlyph* g = glyph(c); return g ? g->adv : 0; }

// Length of the colour code at s ({gold} {red} {grey} {/}), 0 if s does not start one.
static int code_len(const char* s, uint32_t* colour = nullptr, uint32_t base = 0) {
    if (*s != '{') return 0;
    static const struct { const char* code; uint32_t argb; } CODES[] = {
        { "{gold}", TEXT_GOLD }, { "{red}", TEXT_RED }, { "{grey}", TEXT_GREY }, { "{/}", 0 } };
    for (const auto& c : CODES) {
        const int n = (int)strlen(c.code);
        if (!strncmp(s, c.code, (size_t)n)) { if (colour) *colour = c.argb ? c.argb : base; return n; }
    }
    return 0;
}

int text_width(const char* s, int n) {
    int w = 0;
    for (int i = 0; s[i] && s[i] != '\n' && (n < 0 || i < n);) {
        const int cl = code_len(s + i);
        if (cl) { i += cl; continue; }
        w += text_advance(s[i]);
        ++i;
    }
    return w;
}

int text_glyph_count(const char* s) {
    int k = 0;
    for (int i = 0; s[i];) {
        const int cl = code_len(s + i);
        if (cl) { i += cl; continue; }
        if (s[i] != '\n') ++k;
        ++i;
    }
    return k;
}

int text_wrap(const char* src, int width, char* dst, int cap) {
    // The same algorithm as tools/dialog/lint.py wrap(): greedy, word by word; words are separated by
    // single spaces; '|' forces a break. An empty word (leading, trailing or double space, a space next
    // to '|', "||") is an error, as is a word wider than the line or a character the font cannot show.
    int out = 0, lines = 1, line_w = 0;
    bool line_empty = true;
    const char* p = src;
    for (;;) {
        const char* w = p;
        while (*p && *p != ' ' && *p != '|') {
            if (*p == '{') { const int cl = code_len(p); if (!cl) return -1; p += cl; continue; }
            if ((unsigned char)*p < 33 || (unsigned char)*p > 126) return -1;
            ++p;
        }
        const int wl = (int)(p - w);
        if (wl == 0) return -1;
        const int ww = text_width(w, wl);
        if (ww > width) return -1;
        if (!line_empty) {
            const int sp = text_advance(' ');
            if (line_w + sp + ww > width) { if (out + 1 >= cap) return -1; dst[out++] = '\n'; ++lines; line_w = 0; }
            else { if (out + 1 >= cap) return -1; dst[out++] = ' '; line_w += sp; }
        }
        if (out + wl >= cap) return -1;
        memcpy(dst + out, w, (size_t)wl);
        out += wl; line_w += ww; line_empty = false;
        if (*p == ' ') { ++p; continue; }
        if (*p == '|') { ++p; if (out + 1 >= cap) return -1; dst[out++] = '\n'; ++lines; line_w = 0; line_empty = true; continue; }
        break;
    }
    dst[out] = '\0';
    return lines;
}

int text_draw(float x, float y, const char* s, uint32_t argb, float z, int max_glyphs, bool shadow, int alpha) {
    if (!g_ok || !s) return 0;
    const float inv = 1.0f / (float)TEX_W, invh = 1.0f / (float)TEX_H;
    int queued = 0;
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
        const float off = pass == 0 ? (float)g_h.shadow : 0.0f;
        const float zz = pass == 0 ? z - 0.1f : z;
        uint32_t col = argb;
        float px = x, py = y;
        int shown = 0;
        for (int i = 0; s[i];) {
            uint32_t nc;
            const int cl = code_len(s + i, &nc, argb);
            if (cl) { col = nc; i += cl; continue; }
            const char c = s[i++];
            if (c == '\n') { px = x; py += (float)g_h.line_pitch; continue; }
            if (max_glyphs >= 0 && shown >= max_glyphs) break;
            ++shown;
            const FntGlyph* g = glyph(c);
            if (!g) continue;
            if (g->w) {
                uint32_t a = ((col >> 24) * (uint32_t)alpha) / 255u;
                uint32_t c2 = pass == 0 ? ((a * 3 / 4) << 24) : ((a << 24) | (col & 0x00FFFFFFu));
                const float gx = px + (float)g->x_off + off, gy = py + (float)g->y_off + off;
                eng::sprite_draw_uv(g_tex, gx + (float)g->w * 0.5f, gy + (float)g->h * 0.5f, (float)g->w, (float)g->h, 0.0f,
                                    (float)g->u * inv, (float)g->v * invh, (float)(g->u + g->w) * inv, (float)(g->v + g->h) * invh,
                                    0, c2, zz);
                ++queued;
            }
            px += (float)g->adv;
        }
    }
    return queued;
}

void text_icon_size(TextIcon i, int* w, int* h) {
    if (!g_ok || i < 0 || i >= TI_COUNT) { *w = *h = 0; return; }
    *w = g_icon[i].w; *h = g_icon[i].h;
}

void text_icon_uv(TextIcon i, float* u0, float* v0, float* u1, float* v1) {
    if (!g_ok || i < 0 || i >= TI_COUNT) { *u0 = *v0 = *u1 = *v1 = 0.0f; return; }
    const FntIcon& ic = g_icon[i];
    *u0 = ((float)ic.u + 0.5f) / (float)TEX_W; *v0 = ((float)ic.v + 0.5f) / (float)TEX_H;
    *u1 = ((float)(ic.u + ic.w) - 0.5f) / (float)TEX_W; *v1 = ((float)(ic.v + ic.h) - 0.5f) / (float)TEX_H;
}

void text_icon_draw(TextIcon i, float cx, float cy, float scale, uint32_t argb, float z) {
    if (!g_ok || i < 0 || i >= TI_COUNT || !g_icon[i].w) return;
    const FntIcon& ic = g_icon[i];
    const float inv = 1.0f / (float)TEX_W, invh = 1.0f / (float)TEX_H;
    eng::sprite_draw_uv(g_tex, cx, cy, (float)ic.w * scale, (float)ic.h * scale, 0.0f, (float)ic.u * inv, (float)ic.v * invh,
                        (float)(ic.u + ic.w) * inv, (float)(ic.v + ic.h) * invh, scale == 1.0f ? 0 : 2, argb, z);
}

}
