// platform/kos/main.cpp - Dreamcast (KallistiOS) backend for the C++ battle port.
//
// Derived from the original zip/main.cpp. The PVR sprite batching, rect queues and
// input overlay are unchanged; the Lua VM, the l_* bindings, package.path setup and
// the boot.lua/update()/draw() dispatch are removed. The former Lua bindings are
// now the eng:: functions declared in src/engine.h.
#include <kos.h>
#include <png/png.h>
#include <dc/video.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../../src/engine.h"
#include "../../src/battle.h"

// ---------------------------------------------------------------------------
// Crash triage (make DIAG=1). Breadcrumbs go out synchronously over the serial
// port, so the last line printed before a crash marks the failing stage.
// Bisect switches (make DIAG=1 DIAG_SKIP=game|update|draw):
//   game   - never touch the battle code: only the renderer + input overlay run
//   update - run battle_init + battle_draw, never battle_update
//   draw   - run battle_init + battle_update, never battle_draw
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
    pvr_poly_hdr_t hdr_nearest;
    pvr_poly_hdr_t hdr_bilinear;
};

static Sprite g_sprites[16];
static int g_sprite_count = 0;

// --- Sprite batching by filter ----------------------------------------------
enum SpriteFilterMode : int { SF_NEAREST = 0, SF_BILINEAR = 1 };
struct SpriteCmd { float cx, cy, w, h, a; };

static std::vector<SpriteCmd> g_q_nearest[16];
static std::vector<SpriteCmd> g_q_bilinear[16];

static inline void queue_sprite_cmd(int idx, const SpriteCmd& cmd, SpriteFilterMode f) {
    if (idx < 0 || idx >= 16) return;
    if (f == SF_BILINEAR) g_q_bilinear[idx].push_back(cmd);
    else                  g_q_nearest[idx].push_back(cmd);
}

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
        float cs = fcos(c.a), sn = fsin(c.a);
        float lx[4] = { -hw,  hw, -hw,  hw };
        float ly[4] = { -hh, -hh,  hh,  hh };
        float x[4], y[4];
        for (int k = 0; k < 4; ++k) {
            x[k] = c.cx + (lx[k] * cs - ly[k] * sn);
            y[k] = c.cy + (lx[k] * sn + ly[k] * cs);
        }
        pvr_vertex_t v;
        v.argb = 0xFFFFFFFF; v.oargb = 0; v.z = 1.0f;
        v.flags = PVR_CMD_VERTEX;     v.x = x[0]; v.y = y[0]; v.u = 0.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX;     v.x = x[1]; v.y = y[1]; v.u = 1.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX;     v.x = x[2]; v.y = y[2]; v.u = 0.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
        v.flags = PVR_CMD_VERTEX_EOL; v.x = x[3]; v.y = y[3]; v.u = 1.0f; v.v = 1.0f; pvr_prim(&v, sizeof(v));
    }
    q.clear();
}

static void flush_sprite_batches() {
    for (int i = 0; i < g_sprite_count; ++i) emit_sprite_quads(&g_sprites[i].hdr_nearest,  g_q_nearest[i]);
    for (int i = 0; i < g_sprite_count; ++i) emit_sprite_quads(&g_sprites[i].hdr_bilinear, g_q_bilinear[i]);
}

// --- Input -----------------------------------------------------------------
struct InputState { uint32 buttons; int joyx; int joyy; };
static InputState g_in = { 0xFFFFFFFF, 0, 0 };   // see poll_input(): we store an active-low image

static void poll_input() {
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev) return;
    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    if (!st) return;
    g_in.buttons = st->buttons;
    g_in.joyx = st->joyx;
    g_in.joyy = st->joyy;
}

static bool down_mask(uint32 mask) { return (g_in.buttons & mask) == 0; }   // same test as original main.cpp

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

// --- Debug input overlay (unchanged C feature of the original) --------------
static void draw_input_overlay() {
    auto col = [](bool pressed) { return pressed ? 0xFF00FF00 : 0xFF202020; };
    const int LIST = PVR_LIST_TR_POLY;
    const float Z = 0.01f;
    rectq_push(LIST, 20, 20, 20, 20, Z, col(down_mask(CONT_DPAD_UP)));
    rectq_push(LIST, 20, 45, 20, 20, Z, col(down_mask(CONT_DPAD_DOWN)));
    rectq_push(LIST, 45, 45, 20, 20, Z, col(down_mask(CONT_DPAD_LEFT)));
    rectq_push(LIST, 70, 45, 20, 20, Z, col(down_mask(CONT_DPAD_RIGHT)));
    float ax = (float)g_in.joyx / 128.0f, ay = (float)g_in.joyy / 128.0f;
    rectq_push(LIST, 120 + ax * 30.0f, 45 + ay * 30.0f, 6, 6, Z, 0xFFFF0000);

    // LEGACY: two "ship pose" dots from the unused main.lua demo. dbg.set_pose() was
    // never called, so the pose is the constant (320,240,0) and the dots always sit
    // at the screen centre. Kept for visual parity; safe to delete.
    const float pose_x = 320.0f, pose_y = 240.0f, pose_a = 0.0f;
    float fx = fsin(pose_a), fy = -fcos(pose_a);
    rectq_push(LIST, pose_x + fx * 40.0f - 3.0f, pose_y + fy * 40.0f - 3.0f, 6, 6, Z, 0xFF00FF00);
    rectq_push(LIST, pose_x - fx * 40.0f - 3.0f, pose_y - fy * 40.0f - 3.0f, 6, 6, Z, 0xFFFF0000);
}

static void check_file(const char* path) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) dbgio_printf("MISSING: %s\n", path);
    else { dbgio_printf("FOUND:   %s\n", path); fs_close(f); }
}

// ============================================================================
// engine.h implementation (formerly the Lua bindings l_sprite_*, l_gfx_*, ...)
// ============================================================================
namespace eng {

Sprite sprite_load(const char* path) {                         // was l_sprite_load
    DIAG("sprite_load(%s) slot %d\n", path, g_sprite_count);
    if (g_sprite_count >= 16) { dbglog(DBG_ERROR, "sprite limit reached\n"); return 0; }
    ::Sprite* s = &g_sprites[g_sprite_count];
    memset(&s->img, 0, sizeof(s->img));
    s->tex = NULL;

    int rc = png_to_img(path, 0, &s->img);
    if (rc < 0 || !s->img.data || s->img.byte_count == 0) {
        dbglog(DBG_ERROR, "png_to_img failed for %s\n", path);
        return 0;
    }
    DIAG("  png ok: %lux%lu fmt=%lx bytes=%lu data=%p\n", (unsigned long)s->img.w, (unsigned long)s->img.h,
         (unsigned long)s->img.fmt, (unsigned long)s->img.byte_count, s->img.data);
    s->tex = pvr_mem_malloc(s->img.byte_count);
    if (!s->tex) { dbglog(DBG_ERROR, "pvr_mem_malloc failed\n"); return 0; }
    DIAG("  vram=%p, uploading\n", (void*)s->tex);
    pvr_txr_load_kimg(&s->img, s->tex, PVR_TXRLOAD_16BPP);

    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED,
                     s->img.w, s->img.h, s->tex, PVR_FILTER_NONE);
    pvr_poly_compile(&s->hdr_nearest, &cxt);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED,
                     s->img.w, s->img.h, s->tex, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&s->hdr_bilinear, &cxt);

    DIAG("  sprite handle %d ready (hdr @%p)\n", g_sprite_count + 1, (void*)&s->hdr_nearest);
    return ++g_sprite_count;   // handle 1..N
}

void sprite_draw(Sprite handle, float x, float y, float w, float h, float a, int filter) {   // was l_sprite_draw
    if (handle < 1 || handle > g_sprite_count) return;
    queue_sprite_cmd(handle - 1, SpriteCmd{ x, y, w, h, a }, filter == 2 ? SF_BILINEAR : SF_NEAREST);
}

void gfx_rect(float x, float y, float w, float h, uint32_t argb, float z)    { rectq_push(PVR_LIST_OP_POLY, x, y, w, h, z, argb); }
void gfx_rect_tr(float x, float y, float w, float h, uint32_t argb, float z) { rectq_push(PVR_LIST_TR_POLY, x, y, w, h, z, argb); }

bool input_down(Key k) {                                        // was l_input_down
    switch (k) {
    case KEY_UP:    return down_mask(CONT_DPAD_UP);
    case KEY_DOWN:  return down_mask(CONT_DPAD_DOWN);
    case KEY_LEFT:  return down_mask(CONT_DPAD_LEFT);
    case KEY_RIGHT: return down_mask(CONT_DPAD_RIGHT);
    case KEY_FIRE:  return down_mask(CONT_A);
    // CHANGE vs. shipped Lua binding: l_input_down() had no mapping for "b", "y" or
    // "start" and always returned false, so ui.lua's B-cancel and Y-shortcut were dead.
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

void dbg_print(const char* s) { dbglog(DBG_INFO, "%s\n", s); }   // original buffered it and never displayed it

void seed_random(uint32_t seed) { g_rng = seed ? seed : 12345u; }

} // namespace eng

// ============================================================================
int main(int, char**) {
    pvr_init_defaults();
    dbgio_init();
    dbgio_dev_select("scif");
    dbglog(DBG_INFO, "=== DREAMCAST BOOT (C++ battle) ===\n");
    DIAG("main: pvr+dbgio up. sizeof(BattleCtx)=%u sizeof(pvr_vertex_t)=%u sizeof(::Sprite)=%u\n",
         (unsigned)sizeof(bt::BattleCtx), (unsigned)sizeof(pvr_vertex_t), (unsigned)sizeof(::Sprite));
    DIAG("main: &g_sprites=%p (mod32=%u) &g_rect_hdr_op=%p (mod32=%u) stack~%p\n",
         (void*)g_sprites, (unsigned)((uintptr_t)g_sprites & 31), (void*)&g_rect_hdr_op,
         (unsigned)((uintptr_t)&g_rect_hdr_op & 31), (void*)&g_diag_frame);

    check_file("/rd/es_sprite_64.png");
    check_file("/rd/plasma.png");

    eng::seed_random((uint32_t)timer_ms_gettime64());
#if !defined(DIAG_SKIP_GAME)
    DIAG("main: battle_init begin\n");
    bt::battle_init();   // replaces luaL_dofile("/rd/boot.lua")
    DIAG("main: battle_init done\n");
#else
    DIAG("main: DIAG_SKIP_GAME - battle code disabled\n");
#endif

    uint64 last = timer_ms_gettime64();
    while (1) {
        ++g_diag_frame;
        poll_input();

        uint64 now = timer_ms_gettime64();
        float dt = (float)(now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        DIAG_F("frame %d: update dt=%d ms\n", g_diag_frame, (int)(dt * 1000.0f));
#if !defined(DIAG_SKIP_GAME) && !defined(DIAG_SKIP_UPDATE)
        bt::battle_update(dt);                 // was call_lua_fn_1f(L, "update", dt)
#endif
        DIAG_F("frame %d: wait_ready\n", g_diag_frame);
        pvr_wait_ready();
        pvr_scene_begin();

        pvr_list_begin(PVR_LIST_OP_POLY);
        DIAG_F("frame %d: draw\n", g_diag_frame);
#if !defined(DIAG_SKIP_GAME) && !defined(DIAG_SKIP_DRAW)
        bt::battle_draw();                     // was call_lua_fn_0(L, "draw")
#endif
        DIAG_F("frame %d: flush sprites (rects queued=%d)\n", g_diag_frame, g_rect_qn);
        flush_sprite_batches();
        rect_flush_list(PVR_LIST_OP_POLY);
        pvr_list_finish();

        pvr_list_begin(PVR_LIST_TR_POLY);
        draw_input_overlay();
        rect_flush_list(PVR_LIST_TR_POLY);
        pvr_list_finish();

        DIAG_F("frame %d: scene_finish\n", g_diag_frame);
        pvr_scene_finish();
        vid_waitvbl();
        g_rect_qn = 0;
        if (BATTLE_DIAG && g_diag_frame % 300 == 0) DIAG("alive: frame %d\n", g_diag_frame);
    }
}
