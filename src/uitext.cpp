// uitext.cpp - see uitext.h
#include "uitext.h"
#include "engine.h"
#include "font.h"
#include "icons.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

namespace bt {

namespace {
struct Glyph { uint16_t x, y; uint8_t w, h; int8_t ox, oy; uint16_t adv16, pad; };
struct FontHdr { uint16_t em, cap, line, outline; uint8_t pad[8]; };
struct IconRec { uint16_t x, y, w, h; };
static_assert(sizeof(Glyph) == 12 && sizeof(FontHdr) == 16 && sizeof(IconRec) == 8, "ui.uia record sizes");

const int NFONTS = 2, NGLYPHS = 64, FIRST = 32;
const char* const PATH = "/rd/ui/ui.uia";

eng::Sprite g_tex = 0;
int g_w = 0, g_h = 0;
float g_iu = 0, g_iv = 0;                    // 1 / atlas size
FontHdr g_font[NFONTS];
Glyph g_glyph[NFONTS][NGLYPHS];
IconRec g_icon[UI_ICON_COUNT];
}

// LZ16 (tools/ui/make_ui_atlas.py): t < 128: t + 1 literal u16 words; t >= 128: t - 126 words copied from d words back.
static bool lz16_unpack(const uint8_t* in, int n, uint16_t* out, int words) {
    int i = 0, o = 0;
    while (i < n) {
        const int t = in[i++];
        if (t < 128) {
            const int k = t + 1;
            if (o + k > words || i + 2 * k > n) return false;
            for (int j = 0; j < k; ++j, i += 2) out[o++] = (uint16_t)(in[i] | (in[i + 1] << 8));
        } else {
            if (i + 2 > n) return false;
            int l = t - 126;
            const int d = in[i] | (in[i + 1] << 8);
            i += 2;
            if (d == 0 || d > o || o + l > words) return false;
            for (; l > 0; --l, ++o) out[o] = out[o - d];
        }
    }
    return o == words;
}

bool ui_init() {
    static bool done = false;
    if (done) return g_tex != 0;
    done = true;
    // The file (~50 KB: the texels are LZ16-packed) and the unpacked texels (256 KiB) live in boot-time buffers, freed
    // once the atlas is uploaded: the texture lives in VRAM only (KOS keeps a memory texture's pointer for
    // texture_update, which this atlas never uses; the host copies it).
    const int cap = 96 * 1024;
    char* buf = (char*)malloc(cap);
    if (!buf) { eng::dbg_print("ui: no memory for /rd/ui/ui.uia (the old font and icons are used)"); return false; }
    const int n = eng::file_read(PATH, buf, cap);
    bool ok = n >= 32 && !memcmp(buf, "UIA1", 4);
    uint16_t h[6] = { 0, 0, 0, 0, 0, 0 };
    uint32_t off = 0, bytes = 0, packed = 0;
    if (ok) { memcpy(h, buf + 4, sizeof h); memcpy(&off, buf + 16, 4); memcpy(&bytes, buf + 20, 4); memcpy(&packed, buf + 24, 4); }
    // h: version, w, h, nfonts, nglyphs, nicons
    ok = ok && h[0] == 2 && h[3] == NFONTS && h[4] == NGLYPHS && h[5] == UI_ICON_COUNT && h[1] >= 8 && h[2] >= 8 &&
         h[1] <= 1024 && h[2] <= 1024 && (off & 31) == 0 && bytes == (uint32_t)h[1] * h[2] * 2 && (uint32_t)n == off + packed &&
         off >= 32 + sizeof g_font + sizeof g_glyph + sizeof g_icon;
    if (ok) {
        const char* p = buf + 32;
        memcpy(g_font, p, sizeof g_font); p += sizeof g_font;
        memcpy(g_glyph, p, sizeof g_glyph); p += sizeof g_glyph;
        memcpy(g_icon, p, sizeof g_icon);
        g_w = h[1]; g_h = h[2];
        for (int f = 0; f < NFONTS && ok; ++f)
            for (int i = 0; i < NGLYPHS; ++i)
                if (g_glyph[f][i].x + g_glyph[f][i].w > g_w || g_glyph[f][i].y + g_glyph[f][i].h > g_h) ok = false;
        for (int i = 0; i < UI_ICON_COUNT && ok; ++i)
            if (g_icon[i].x + g_icon[i].w > g_w || g_icon[i].y + g_icon[i].h > g_h) ok = false;
    }
    uint16_t* texels = ok ? (uint16_t*)malloc(bytes) : nullptr;
    ok = ok && texels && lz16_unpack((const uint8_t*)buf + off, (int)packed, texels, (int)(bytes / 2));
    if (ok) {
        g_tex = eng::texture_create(texels, g_w, g_h, 2);
        ok = g_tex != 0;
        if (ok) {
            g_iu = 1.0f / (float)g_w; g_iv = 1.0f / (float)g_h;
            eng::sprite_reserve(g_tex, 0, 1024);       // KOS: the per-frame bilinear queue never grows (no heap after boot)
        }
    }
    free(texels);
    free(buf);
    if (!ok) eng::dbg_print("ui: /rd/ui/ui.uia missing or invalid (the old font and icons are used)");
    return ok;
}

bool ui_ready() { return g_tex != 0; }

int ui_cap(UiFont f) { return g_tex ? g_font[f].cap : (f == UF_L ? 28 : 14); }

static inline int glyph_index(char c) {
    unsigned char u = (unsigned char)c;
    if (u >= 'a' && u <= 'z') u = (unsigned char)(u - 32);
    if (u < FIRST || u >= FIRST + NGLYPHS) return -1;
    return u - FIRST;
}

// The glyph of c in font f, falling back to S at twice the scale when L lacks it (L carries numbers and capitals).
static inline const Glyph* glyph(UiFont f, char c, float* scale) {
    const int i = glyph_index(c);
    if (i < 0) return nullptr;
    if (f == UF_L && g_glyph[UF_L][i].adv16 == 0 && g_glyph[UF_S][i].adv16 != 0) {
        *scale *= (float)g_font[UF_L].cap / (float)g_font[UF_S].cap;
        return &g_glyph[UF_S][i];
    }
    return &g_glyph[f][i];
}

float ui_text_width(UiFont f, const char* s, float scale) {
    if (!g_tex) return (float)font_width(s, f == UF_L ? 4 : 2) * scale;
    float w = 0;
    for (const char* p = s; *p; ++p) {
        float sc = scale;
        const Glyph* g = glyph(f, *p, &sc);
        if (g) w += (float)g->adv16 * (1.0f / 16.0f) * sc;
    }
    return w;
}

int ui_text(UiFont f, float x, float y, const char* s, uint32_t argb, float z, float scale, UiAlign al) {
    if (!s || !*s) return 0;
    if (al != UA_LEFT) x -= ui_text_width(f, s, scale) * (al == UA_CENTRE ? 0.5f : 1.0f);
    if (!g_tex) {                                          // fallback: the 5x7 font, shadowed
        const int fs = f == UF_L ? (scale >= 0.875f ? (int)(4.0f * scale + 0.5f) : 2) : (scale >= 1.5f ? 3 : 2);
        font_draw(x, y, s, fs, argb, true, z);
        return 0;
    }
    const bool snap = scale > 0.999f && scale < 1.001f;
    if (snap) { x = (float)(int)(x + (x >= 0 ? 0.5f : -0.5f)); y = (float)(int)(y + (y >= 0 ? 0.5f : -0.5f)); }
    float pen = x;
    int n = 0;
    for (const char* p = s; *p; ++p) {
        float sc = scale;
        const Glyph* g = glyph(f, *p, &sc);
        if (!g) continue;
        if (g->w && g->h) {
            float gx = pen + (float)g->ox * sc, gy = y + (float)g->oy * sc;
            if (snap && sc == scale) gx = (float)(int)(gx + (gx >= 0 ? 0.5f : -0.5f));
            const float gw = (float)g->w * sc, gh = (float)g->h * sc;
            const float u0 = (float)g->x * g_iu, v0 = (float)g->y * g_iv;
            const float u1 = (float)(g->x + g->w) * g_iu, v1 = (float)(g->y + g->h) * g_iv;
            eng::sprite_draw_uv(g_tex, gx + gw * 0.5f, gy + gh * 0.5f, gw, gh, 0.0f, u0, v0, u1, v1, 2, argb, z);
            ++n;
        }
        pen += (float)g->adv16 * (1.0f / 16.0f) * sc;
    }
    return n;
}

int ui_text_px(float x, float y, const char* s, float cap_px, uint32_t argb, float z, UiAlign al) {
    const UiFont f = cap_px <= 17.0f ? UF_S : UF_L;
    return ui_text(f, x, y, s, argb, z, cap_px / (float)ui_cap(f), al);
}

void ui_icon(UiIcon i, float cx, float cy, float size, uint32_t argb, float z) {
    if (i < 0 || i >= UI_ICON_COUNT) return;
    if (!g_tex) {                                          // fallback: the pixel icons
        static const Icon OLD[UI_ICON_COUNT] = { ICON_SWORD, ICON_FLAME, ICON_SHIELD, ICON_POTION, ICON_PAD_A, ICON_PAD_B,
                                                 ICON_PAD_Y, ICON_PAD_Y, ICON_PAD_START, ICON_PAD_LEFT, ICON_PAD_RIGHT,
                                                 ICON_PAD_UP, ICON_PAD_DOWN, ICON_COUNT, ICON_POTION, ICON_COUNT };
        if (i == UI_LOCK) { text_icon_draw(TI_LOCK, cx, cy, 1.0f, argb, z); return; }
        if (i == UI_TOKEN) { text_icon_draw(TI_TOKEN, cx, cy, 1.0f, argb, z); return; }
        icon_draw_tint(OLD[i], cx, cy, size <= 24.0f ? 16.0f : size, argb, z);
        return;
    }
    const IconRec& r = g_icon[i];
    // the cell's art is r.w px: `size` is the drawn cell (crisp when size == r.w and the centre is on the pixel grid)
    if (size > (float)r.w - 0.01f && size < (float)r.w + 0.01f) {
        const float h = size * 0.5f;
        cx = (float)(int)(cx - h + (cx - h >= 0 ? 0.5f : -0.5f)) + h;
        cy = (float)(int)(cy - h + (cy - h >= 0 ? 0.5f : -0.5f)) + h;
    }
    eng::sprite_draw_uv(g_tex, cx, cy, size, size, 0.0f, (float)r.x * g_iu, (float)r.y * g_iv,
                        (float)(r.x + r.w) * g_iu, (float)(r.y + r.h) * g_iv, 2, argb, z);
}

}
