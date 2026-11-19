// transitions.cpp - see transitions.h
#include "transitions.h"
#include "engine.h"
#include "math3d.h"
#include "text.h"

namespace bt {

// GDD 6. T3/T4 are the dialog's (dialog_ui.cpp), T8 the boss intro script (romdisk/scripts/barrow.txt): listed here
// so the catalogue is complete and traces can name them.
static const TransDef TRANS[T_COUNT] = {
    { "-",   0,   0,  0, "none" },
    { "T1",  0,   0, 45, "fade from black" },                                   // Boot -> Title
    { "T2", 30, 105, 30, "fade to black, text card (90 t, A skips), fade in" },   // Title -> Intro (hold 15 + card 90)
    { "T3",  0,   0, 20, "letterbox in, camera to the dialog shot" },
    { "T4",  0,   0, 20, "letterbox out, camera back" },
    { "T5", 32,   0, 0,  "two white flashes (4 t each), closing diamond iris (24 t)" },   // Field -> Battle
    { "T6", 24,   0, 12, "fade to black, fade in" },                            // Battle -> Field
    { "T7", 20,   0, 20, "fade to black, swap map, fade in (+30 t walk-in)" },  // Map -> Map
    { "T8",  0,   0, 0,  "boss intro: letterbox, crane, name card (script)" },
    { "T9", 100,  0, 0,  "slow motion 30 t, tint grey-red 40 t, title 30 t" },  // Battle lost -> Death
    { "T10", 48,  0, 40, "white flash 8 t, fade to white 40 t, fade from white" }, // Boss won -> Ending
    { "T11", 60,  0, 0,  "fade to black" },                                     // Results / Death -> Title
};
const TransDef& trans_def(int id) { return TRANS[(id >= 0 && id < T_COUNT) ? id : 0]; }

static eng::Material g_op = 0, g_tr = 0, g_mul = 0;

void trans_init() {
    if (g_op) return;
    g_op = eng::material_create(0, 0);                          // opaque, untextured (the iris)
    g_tr = eng::material_create(0, eng::MAT_TRANSLUCENT);       // vignette
    g_mul = eng::material_create(0, eng::MAT_MULTIPLY);         // the death screen's tint
}

void trans_draw_fade(uint32_t rgb, int alpha) {
    if (alpha <= 0) return;
    if (alpha > 255) alpha = 255;
    eng::gfx_rect_tr(0, 0, 640, 480, (uint32_t)alpha << 24 | (rgb & 0xFFFFFFu), Z_FADE_TOP);   // over every card and title too
}

static void put(eng::Vert3& v, uint32_t flags, float x, float y, float z, uint32_t argb) {
    v.flags = flags; v.x = x; v.y = y; v.z = z; v.u = 0.0f; v.v = 0.0f; v.argb = argb; v.oargb = 0;
}

// A diamond |x - 320| / 320 + |y - 240| / 240 <= open, drawn as the ring between it and a frame well outside the
// screen: 32 segments (8 per diamond edge), one triangle strip.
void trans_draw_iris(float open) {
    if (open >= 1.0f || !g_op) return;
    if (open < 0.0f) open = 0.0f;
    const int N = 32;
    eng::Vert3* v = eng::vtx_begin(2 * (N + 1));
    if (!v) return;
    int k = 0;
    for (int i = 0; i <= N; ++i) {
        // a point on the unit diamond, walking its 4 edges 8 segments each
        const int e = (i / 8) & 3, j = i % 8;
        const float t = (float)j / 8.0f;
        static const float C[5][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 } };
        const int e2 = i == N ? 0 : e;
        const float dx = i == N ? 1.0f : C[e2][0] + (C[e2 + 1][0] - C[e2][0]) * t;
        const float dy = i == N ? 0.0f : C[e2][1] + (C[e2 + 1][1] - C[e2][1]) * t;
        const float ix = 320.0f + dx * 640.0f * open, iy = 240.0f + dy * 480.0f * open;   // the diamond through the corners at open = 1
        const float ox = 320.0f + dx * 1400.0f, oy = 240.0f + dy * 1100.0f;
        put(v[k++], eng::VERT, ox, oy, Z_TRANS, 0xFF000000u);
        put(v[k++], i == N ? eng::VERT_EOL : eng::VERT, ix, iy, Z_TRANS, 0xFF000000u);
    }
    eng::vtx_end(g_op, k);
}

void trans_draw_flash_iris(int t, int iris_ticks) {
    if (t < 8) {                                                // two 4-tick flashes: up, down; up, down
        const int p = t % 4;
        const int a = p < 2 ? 200 * (p + 1) / 2 : 200 * (4 - p) / 2;
        trans_draw_fade(0xFFFFFF, a);
        return;
    }
    const int k = t - 8;
    const float u = iris_ticks > 0 ? (float)k / (float)iris_ticks : 1.0f;
    trans_draw_iris(1.0f - (u > 1.0f ? 1.0f : u));
}

void trans_draw_tint(uint32_t tint_rgb, float w, uint32_t overlay_argb, int vig_alpha) {
    if (w > 0.0f && g_mul) {
        if (w > 1.0f) w = 1.0f;
        const uint32_t r = (uint32_t)(255.0f + (float)((int)((tint_rgb >> 16) & 255u) - 255) * w);
        const uint32_t g = (uint32_t)(255.0f + (float)((int)((tint_rgb >> 8) & 255u) - 255) * w);
        const uint32_t b = (uint32_t)(255.0f + (float)((int)(tint_rgb & 255u) - 255) * w);
        eng::Vert3* v = eng::vtx_begin(4);
        if (v) {
            const uint32_t c = 0xFF000000u | r << 16 | g << 8 | b;
            put(v[0], eng::VERT, 0, 0, Z_TRANS - 0.1f, c);
            put(v[1], eng::VERT, 640, 0, Z_TRANS - 0.1f, c);
            put(v[2], eng::VERT, 0, 480, Z_TRANS - 0.1f, c);
            put(v[3], eng::VERT_EOL, 640, 480, Z_TRANS - 0.1f, c);
            eng::vtx_end(g_mul, 4);
        }
    }
    if (overlay_argb >> 24) eng::gfx_rect_tr(0, 0, 640, 480, overlay_argb, Z_TRANS - 0.05f);
    if (vig_alpha > 0 && g_tr) {
        // a ring of 32 segments: clear inside an ellipse, vig_alpha at the screen's edge (gouraud)
        const int N = 32;
        eng::Vert3* v = eng::vtx_begin(2 * (N + 1));
        if (!v) return;
        const uint32_t outer = (uint32_t)(vig_alpha > 255 ? 255 : vig_alpha) << 24;
        for (int i = 0; i <= N; ++i) {
            const float a = 6.28318531f * (float)(i % N) / (float)N;
            const shz_sincos_t sc = shz_sincosf(a);
            put(v[2 * i], eng::VERT, 320.0f + sc.cos * 470.0f, 240.0f + sc.sin * 380.0f, Z_TRANS - 0.04f, outer);
            put(v[2 * i + 1], i == N ? eng::VERT_EOL : eng::VERT, 320.0f + sc.cos * 250.0f, 240.0f + sc.sin * 170.0f, Z_TRANS - 0.04f, 0x00000000u);
        }
        eng::vtx_end(g_tr, 2 * (N + 1));
        // the corners beyond the ellipse's outer edge
        eng::gfx_rect_tr(0, 0, 640, 30, outer, Z_TRANS - 0.04f);
        eng::gfx_rect_tr(0, 450, 640, 30, outer, Z_TRANS - 0.04f);
    }
}

void trans_draw_card_text(const char* text, float y, int alpha, uint32_t rgb, float) {
    if (alpha <= 0 || !text_ready()) return;
    const float x = (float)(int)(320.0f - (float)text_width(text) * 0.5f);
    text_draw(x, y, text, (rgb & 0xFFFFFFu) | 0xFF000000u, Z_TRANS_TEXT, -1, true, alpha > 255 ? 255 : alpha);
}

}
