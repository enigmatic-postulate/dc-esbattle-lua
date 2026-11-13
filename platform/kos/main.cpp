// platform/kos/main.cpp - Dreamcast (KallistiOS) backend.
//
// Derived from the original zip/main.cpp (PVR batching, rect queues, input overlay), with
// the Lua VM removed. Implements src/engine.h and runs the game at a fixed 60 Hz tick.
//
// Rendering per frame:   OP list : opaque sprites (tiles) + OP rects (backdrops)
//                        TR list : alpha sprites (characters, icons, font) + TR rects (bars,
//                                  fx, banners, fades) + the debug input overlay
// Depth: every sprite gets z = 1 + seq*1e-4 in call order (later = in front); rects carry
// their own z; larger z is nearer (OP depth compare GREATER, TR autosorted).
#include <kos.h>
#include <png/png.h>
#include <dc/video.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../../src/engine.h"
#include "../../src/game.h"

#ifndef PNG_NO_ALPHA
#define PNG_NO_ALPHA 0
#endif
#ifndef PNG_MASK_ALPHA
#define PNG_MASK_ALPHA 1     // verified in kos-ports png_to_img: mask 1 -> ARGB1555
#endif

// ---------------------------------------------------------------------------
// Crash triage (make DIAG=1). Breadcrumbs go out synchronously over the serial
// port, so the last line printed before a crash marks the failing stage.
// Bisect switches (make DIAG=1 DIAG_SKIP=game|update|draw):
//   game   - never touch the game code: only the renderer + input overlay run
//   update - run game_init + game_draw, never game_tick
//   draw   - run game_init + game_tick, never game_draw
// DIAG builds also print a frame-time report every 300 frames (slice task C2).
// ---------------------------------------------------------------------------
#ifndef BATTLE_DIAG
#define BATTLE_DIAG 0
#endif
#define DIAG(...) do { if (BATTLE_DIAG) { dbgio_printf("[diag] " __VA_ARGS__); dbgio_flush(); } } while (0)
static int g_diag_frame = 0;
#define DIAG_F(...) do { if (BATTLE_DIAG && g_diag_frame <= 3) DIAG(__VA_ARGS__); } while (0)

struct Sprite {
    kos_img_t img;
    pvr_ptr_t tex;
    bool alpha;                    // ARGB1555 in the TR list, else RGB565 in the OP list
    pvr_poly_hdr_t hdr_nearest;
    pvr_poly_hdr_t hdr_bilinear;
};

static Sprite g_sprites[eng::MAX_SPRITES];
static int g_sprite_count = 0;
static uint32 g_vram_bytes = 0;

// --- Sprite batching by texture + filter -------------------------------------
enum SpriteFilterMode : int { SF_NEAREST = 0, SF_BILINEAR = 1 };
struct SpriteCmd { float cx, cy, w, h, a, u0, v0, u1, v1, z; uint32 argb; };

static std::vector<SpriteCmd> g_q_nearest[eng::MAX_SPRITES];
static std::vector<SpriteCmd> g_q_bilinear[eng::MAX_SPRITES];
static int g_seq = 0;              // sprite draw calls this frame (auto depth)

struct RectCmd { int list; float x, y, w, h; float z; uint32 argb; };
static RectCmd g_rect_q[2048];
static int g_rect_qn = 0;

static inline void rectq_push(int list, float x, float y, float w, float h, float z, uint32 argb) {
    if (g_rect_qn >= (int)(sizeof(g_rect_q) / sizeof(g_rect_q[0]))) return;
    g_rect_q[g_rect_qn++] = { list, x, y, w, h, z, argb };
}

static void emit_sprite_quads(const pvr_poly_hdr_t* hdr, std::vector<SpriteCmd>& q) {
    if (q.empty()) return;
    pvr_prim((void*)hdr, sizeof(*hdr));
    for (const auto& c : q) {
        float hw = c.w * 0.5f, hh = c.h * 0.5f;
        float cs = 1.0f, sn = 0.0f;
        if (c.a != 0.0f) { cs = fcos(c.a); sn = fsin(c.a); }
        float lx[4] = { -hw,  hw, -hw,  hw };
        float ly[4] = { -hh, -hh,  hh,  hh };
        float x[4], y[4];
        for (int k = 0; k < 4; ++k) {
            x[k] = c.cx + (lx[k] * cs - ly[k] * sn);
            y[k] = c.cy + (lx[k] * sn + ly[k] * cs);
        }
        pvr_vertex_t v;
        v.argb = c.argb; v.oargb = 0; v.z = c.z;
        v.flags = PVR_CMD_VERTEX;     v.x = x[0]; v.y = y[0]; v.u = c.u0; v.v = c.v0; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX;     v.x = x[1]; v.y = y[1]; v.u = c.u1; v.v = c.v0; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX;     v.x = x[2]; v.y = y[2]; v.u = c.u0; v.v = c.v1; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX_EOL; v.x = x[3]; v.y = y[3]; v.u = c.u1; v.v = c.v1; pvr_prim(&v, sizeof(v));
    }
    q.clear();
}

// Flushes the sprites belonging to one list (alpha=false: OP, alpha=true: TR).
static void flush_sprite_batches(bool alpha) {
    for (int i = 0; i < g_sprite_count; ++i)
        if (g_sprites[i].alpha == alpha) emit_sprite_quads(&g_sprites[i].hdr_nearest, g_q_nearest[i]);
    for (int i = 0; i < g_sprite_count; ++i)
        if (g_sprites[i].alpha == alpha) emit_sprite_quads(&g_sprites[i].hdr_bilinear, g_q_bilinear[i]);
}

// --- Input -----------------------------------------------------------------
struct InputState { uint32 buttons; int joyx; int joyy; };
static InputState g_in = { 0, 0, 0 };   // KOS cont_state_t.buttons is active-high (1 = pressed)

static void poll_input() {
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev) return;
    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    if (!st) return;
    g_in.buttons = st->buttons;
    g_in.joyx = st->joyx;
    g_in.joyy = st->joyy;
}

// KOS's controller driver already inverts the raw active-low bits (cont_reply: buttons = ~raw),
// so 1 = pressed. The original main.cpp tested == 0, so every button read as held at rest.
static bool down_mask(uint32 mask) { return (g_in.buttons & mask) != 0; }

// --- Rect flush ----------------------------------------------------------
static pvr_poly_hdr_t g_rect_hdr_op, g_rect_hdr_tr;
static int g_rect_hdr_ready = 0;

static void rect_flush_list(int list) {
    if (!g_rect_hdr_ready) {
        pvr_poly_cxt_t c;
        pvr_poly_cxt_col(&c, PVR_LIST_OP_POLY); pvr_poly_compile(&g_rect_hdr_op, &c);
        pvr_poly_cxt_col(&c, PVR_LIST_TR_POLY); pvr_poly_compile(&g_rect_hdr_tr, &c);
        g_rect_hdr_ready = 1;
    }
    if (list == PVR_LIST_TR_POLY) pvr_prim(&g_rect_hdr_tr, sizeof(g_rect_hdr_tr));
    else                          pvr_prim(&g_rect_hdr_op, sizeof(g_rect_hdr_op));

    for (int i = 0; i < g_rect_qn; ++i) {
        const RectCmd& r = g_rect_q[i];
        if (r.list != list) continue;
        pvr_vertex_t v;
        v.argb = r.argb; v.oargb = 0; v.z = r.z;
        v.flags = PVR_CMD_VERTEX;     v.x = r.x;       v.y = r.y;       pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX;     v.x = r.x + r.w; v.y = r.y;       pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX;     v.x = r.x;       v.y = r.y + r.h; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX_EOL; v.x = r.x + r.w; v.y = r.y + r.h; pvr_prim(&v, sizeof(v));
    }
}

// --- Debug input overlay (top-left; on top of everything) -------------------
static void draw_input_overlay() {
    auto col = [](bool pressed) { return pressed ? 0xFF00FF00 : 0xFF202020; };
    const int LIST = PVR_LIST_TR_POLY;
    const float Z = 9.5f;
    rectq_push(LIST, 20, 20, 20, 20, Z, col(down_mask(CONT_DPAD_UP)));
    rectq_push(LIST, 20, 45, 20, 20, Z, col(down_mask(CONT_DPAD_DOWN)));
    rectq_push(LIST, 45, 45, 20, 20, Z, col(down_mask(CONT_DPAD_LEFT)));
    rectq_push(LIST, 70, 45, 20, 20, Z, col(down_mask(CONT_DPAD_RIGHT)));
    float ax = (float)g_in.joyx / 128.0f, ay = (float)g_in.joyy / 128.0f;
    rectq_push(LIST, 120 + ax * 30.0f - 3.0f, 45 + ay * 30.0f - 3.0f, 6, 6, Z, 0xFFFF0000);
}

static void check_file(const char* path) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) dbgio_printf("MISSING: %s\n", path);
    else { dbgio_printf("FOUND:   %s\n", path); fs_close(f); }
}

// ============================================================================
// engine.h implementation
// ============================================================================
namespace eng {

Sprite sprite_load(const char* path, bool alpha) {
    DIAG("sprite_load(%s, alpha=%d) slot %d\n", path, alpha ? 1 : 0, g_sprite_count);
    if (g_sprite_count >= MAX_SPRITES) { dbglog(DBG_ERROR, "sprite limit reached\n"); return 0; }
    ::Sprite* s = &g_sprites[g_sprite_count];
    memset(&s->img, 0, sizeof(s->img));
    s->tex = NULL;
    s->alpha = alpha;

    int rc = png_to_img(path, alpha ? PNG_MASK_ALPHA : PNG_NO_ALPHA, &s->img);
    if (rc < 0 || !s->img.data || s->img.byte_count == 0) {
        dbglog(DBG_ERROR, "png_to_img failed for %s\n", path);
        return 0;
    }
    DIAG("  png ok: %lux%lu fmt=%lx bytes=%lu\n", (unsigned long)s->img.w, (unsigned long)s->img.h,
         (unsigned long)s->img.fmt, (unsigned long)s->img.byte_count);
    s->tex = pvr_mem_malloc(s->img.byte_count);
    if (!s->tex) { dbglog(DBG_ERROR, "pvr_mem_malloc failed\n"); return 0; }
    pvr_txr_load_kimg(&s->img, s->tex, PVR_TXRLOAD_16BPP);
    g_vram_bytes += s->img.byte_count;

    const pvr_list_t list = alpha ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;   // KOS 2.1: typed enum
    const int fmt  = (alpha ? (int)PVR_TXRFMT_ARGB1555 : (int)PVR_TXRFMT_RGB565) | (int)PVR_TXRFMT_TWIDDLED;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, list, fmt, s->img.w, s->img.h, s->tex, PVR_FILTER_NONE);
    pvr_poly_compile(&s->hdr_nearest, &cxt);
    pvr_poly_cxt_txr(&cxt, list, fmt, s->img.w, s->img.h, s->tex, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&s->hdr_bilinear, &cxt);

    DIAG("  sprite %d ready, VRAM textures total %lu KiB\n", g_sprite_count + 1, (unsigned long)(g_vram_bytes / 1024));
    return ++g_sprite_count;   // handle 1..N
}

void sprite_draw_uv(Sprite handle, float x, float y, float w, float h, float a,
                    float u0, float v0, float u1, float v1, int filter, uint32_t argb, float z) {
    float zz = (z > 0.0f) ? z : 1.0f + (float)g_seq * 0.0001f;
    ++g_seq;
    if (handle < 1 || handle > g_sprite_count) return;
    SpriteCmd cmd = { x, y, w, h, a, u0, v0, u1, v1, zz, argb };
    if (filter == 2) g_q_bilinear[handle - 1].push_back(cmd);
    else             g_q_nearest[handle - 1].push_back(cmd);
}

void sprite_draw(Sprite handle, float x, float y, float w, float h, float a, int filter) {
    sprite_draw_uv(handle, x, y, w, h, a, 0.0f, 0.0f, 1.0f, 1.0f, filter);
}

void gfx_rect(float x, float y, float w, float h, uint32_t argb, float z)    { rectq_push(PVR_LIST_OP_POLY, x, y, w, h, z, argb); }
void gfx_rect_tr(float x, float y, float w, float h, uint32_t argb, float z) { rectq_push(PVR_LIST_TR_POLY, x, y, w, h, z, argb); }

bool input_down(Key k) {
    switch (k) {
    case KEY_UP:    return down_mask(CONT_DPAD_UP);
    case KEY_DOWN:  return down_mask(CONT_DPAD_DOWN);
    case KEY_LEFT:  return down_mask(CONT_DPAD_LEFT);
    case KEY_RIGHT: return down_mask(CONT_DPAD_RIGHT);
    case KEY_FIRE:  return down_mask(CONT_A);
    case KEY_B:     return down_mask(CONT_B);
    case KEY_Y:     return down_mask(CONT_Y);
    case KEY_START: return down_mask(CONT_START);
    default:        return false;
    }
}

// Same 24-bit LCG as the verified host build (platform/host/host_engine.cpp); no newlib rand().
static uint32_t g_rng = 12345;
float random01() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;
}

void dbg_print(const char* s) { dbglog(DBG_INFO, "%s\n", s); }

int file_read(const char* path, char* buf, int cap) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) return -1;
    int n = (int)fs_read(f, buf, (size_t)cap);
    fs_close(f);
    if (n < 0 || n >= cap) return -1;
    buf[n] = '\0';
    return n;
}

void seed_random(uint32_t seed) { g_rng = seed ? seed : 12345u; }

} // namespace eng

// ============================================================================
int main(int, char**) {
    pvr_init_defaults();
    dbgio_init();
    dbgio_dev_select("scif");
    dbglog(DBG_INFO, "=== DREAMCAST BOOT (C++ field + battle) ===\n");
    DIAG("main: pvr+dbgio up. sizeof(pvr_vertex_t)=%u sizeof(::Sprite)=%u\n",
         (unsigned)sizeof(pvr_vertex_t), (unsigned)sizeof(::Sprite));

    check_file("/rd/maps/test_field.txt");
    check_file("/rd/tiles.png");
    check_file("/rd/hero.png");
    check_file("/rd/ui_icons.png");
    check_file("/rd/font.png");

    eng::seed_random((uint32_t)timer_ms_gettime64());
#if !defined(DIAG_SKIP_GAME)
    DIAG("main: game_init begin\n");
    if (!bt::game_init("/rd/maps/test_field.txt")) dbglog(DBG_ERROR, "game_init: map failed to load\n");
    DIAG("main: game_init done, VRAM textures %lu KiB in %d sprite slots\n",
         (unsigned long)(g_vram_bytes / 1024), g_sprite_count);
#else
    DIAG("main: DIAG_SKIP_GAME - game code disabled\n");
#endif

    // Fixed 60 Hz timestep (slice task A5): accumulate elapsed microseconds x60 and run one
    // game tick per 1,000,000 (exact, no drift). At most 4 catch-up ticks per displayed frame.
    uint64 last = timer_us_gettime64();
    uint64 acc60 = 0;
    uint32 worst_us = 0, over_budget = 0;          // frame-time probe (DIAG report)
    int heap_prev = mallinfo().uordblks;           // heap-in-use probe (DoD T3: no per-frame allocation)
    while (1) {
        ++g_diag_frame;
        poll_input();

        uint64 now = timer_us_gettime64();
        acc60 += (now - last) * 60u;
        last = now;
        uint64 work_start = now;
        int steps = 0;
        while (acc60 >= 1000000u && steps < 4) {
#if !defined(DIAG_SKIP_GAME) && !defined(DIAG_SKIP_UPDATE)
            bt::game_tick();
#endif
            acc60 -= 1000000u;
            ++steps;
        }
        if (steps == 4) acc60 = 0;               // fell far behind (e.g. after loading): drop the backlog
        DIAG_F("frame %d: %d tick(s)\n", g_diag_frame, steps);

        DIAG_F("frame %d: wait_ready\n", g_diag_frame);
        pvr_wait_ready();
        uint64 draw_start = timer_us_gettime64();
        pvr_scene_begin();
        g_seq = 0;
#if !defined(DIAG_SKIP_GAME) && !defined(DIAG_SKIP_DRAW)
        bt::game_draw();                          // queues sprites + rects
#endif
        draw_input_overlay();

        DIAG_F("frame %d: OP list (rects queued=%d)\n", g_diag_frame, g_rect_qn);
        pvr_list_begin(PVR_LIST_OP_POLY);
        flush_sprite_batches(false);
        rect_flush_list(PVR_LIST_OP_POLY);
        pvr_list_finish();

        DIAG_F("frame %d: TR list\n", g_diag_frame);
        pvr_list_begin(PVR_LIST_TR_POLY);
        flush_sprite_batches(true);
        rect_flush_list(PVR_LIST_TR_POLY);
        pvr_list_finish();

        pvr_scene_finish();
        uint64 done = timer_us_gettime64();
        g_rect_qn = 0;

        // CPU time spent on this frame = game ticks + building/submitting the scene
        // (excludes waiting for the PVR / vblank).
        uint32 work_us = (uint32)((draw_start - work_start) + (done - draw_start));
        if (work_us > worst_us) worst_us = work_us;
        if (work_us > 16667) ++over_budget;
        if (BATTLE_DIAG && g_diag_frame % 300 == 0) {
            int heap_now = mallinfo().uordblks;
            DIAG("frames %d: worst CPU frame %lu us, frames over 16.7 ms: %lu, heap in use %d B (%+d since last report)\n",
                 g_diag_frame, (unsigned long)worst_us, (unsigned long)over_budget, heap_now, heap_now - heap_prev);
            heap_prev = heap_now;
            worst_us = 0;
        }
        vid_waitvbl();
    }
}
