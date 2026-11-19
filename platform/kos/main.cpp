// platform/kos/main.cpp - Dreamcast (KallistiOS) backend.
//
// Derived from the original zip/main.cpp (PVR batching, rect queues, input overlay), with
// the Lua VM removed. Implements src/engine.h and runs the game at a fixed 60 Hz tick.
//
// Rendering per frame:   OP list : 3D batches (field ground, models) + opaque sprites + OP rects
//                        TR list : 3D batches (blob shadows) + alpha sprites (icons, font) +
//                                  TR rects (bars, fx, banners, fades) + the debug input overlay
// Depth: every sprite gets z = 1 + seq*1e-4 in call order (later = in front); rects carry
// their own z; larger z is nearer (OP depth compare GREATER, TR autosorted). 3D vertices carry
// z = 1/w <= 2 (src/render3d.cpp), so they sit behind sprites/rects and in front of the
// background plane (KOS zclip = 0.0001, read from pvr_allocate_buffers in this KOS build).
//
// 3D submission: the game writes pvr_vertex_t-shaped strips (eng::Vert3) into g_arena; each
// batch goes out as one header + ONE pvr_prim() of the whole block (pvr_prim -> sq_fast_cpy of
// size/32 blocks, no lock). Build with R3D_SAFE=1 to submit one vertex per pvr_prim() instead -
// the exact pattern the 2D renderer has always used - if the bulk path is ever suspected.
#include <kos.h>
#include <png/png.h>
#include <dc/video.h>
#include <malloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>

#include "../../src/engine.h"
#include "../../src/hwmath.h"      // SH4ZAM: version check + the library (see src/hwmath.h)
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

// Always-on boot breadcrumbs (every build): the same synchronous dbgio path as the FOUND:/
// MISSING: lines, flushed per line, with milliseconds since boot. Boot prints ~30 lines, the
// first two frames ~6 each, then one line at frames 60 and 600; nothing after that.
static uint64_t g_boot_ms0 = 0;
#define BOOT(...) do { dbgio_printf("[boot %5lu ms] ", (unsigned long)(timer_ms_gettime64() - g_boot_ms0)); \
                       dbgio_printf(__VA_ARGS__); dbgio_flush(); } while (0)
#define BOOT_F(...) do { if (g_diag_frame <= 2) BOOT(__VA_ARGS__); } while (0)

struct Sprite {
    kos_img_t img;
    pvr_ptr_t tex;
    bool alpha;                    // ARGB1555/4444 in the TR list, else RGB565 in the OP list
    int pvr_fmt;                   // PVR_TXRFMT_* (without TWIDDLED)
    pvr_poly_hdr_t hdr_nearest;
    pvr_poly_hdr_t hdr_bilinear;
};

// --- 3D: eng::Vert3 must be byte-identical to pvr_vertex_t (it is submitted as-is) -------
static_assert(sizeof(eng::Vert3) == sizeof(pvr_vertex_t), "Vert3 size");
static_assert(offsetof(eng::Vert3, flags) == offsetof(pvr_vertex_t, flags), "Vert3 flags");
static_assert(offsetof(eng::Vert3, x) == offsetof(pvr_vertex_t, x), "Vert3 x");
static_assert(offsetof(eng::Vert3, z) == offsetof(pvr_vertex_t, z), "Vert3 z");
static_assert(offsetof(eng::Vert3, u) == offsetof(pvr_vertex_t, u), "Vert3 u");
static_assert(offsetof(eng::Vert3, argb) == offsetof(pvr_vertex_t, argb), "Vert3 argb");
static_assert(offsetof(eng::Vert3, oargb) == offsetof(pvr_vertex_t, oargb), "Vert3 oargb");
static_assert(eng::VERT == PVR_CMD_VERTEX && eng::VERT_EOL == PVR_CMD_VERTEX_EOL, "Vert3 commands");

// Slice 3D-6: every material also has a fogged header (PVR table fog), used while eng::set_fog is on (the barrow).
struct Material3 { pvr_poly_hdr_t hdr; pvr_poly_hdr_t hdr_fog; pvr_list_t list; };
static Material3 g_mats[256];       // village ~90 (house variants per yaw x opaque/faded copies) + battle ~30
static int g_nmats = 0;
static bool g_fog_on = false;       // eng::set_fog (never set in NO_FOG builds)

// 640 KiB. Slice 3D-5: was 16,384 (512 KiB) - the untouched 3D-4 build already peaked at 15,844 on the field
// scripts and Wren and Brom took it to 16,363; a full arena drops the last batches (props). 20,480 x 32 B plus
// the 2D overlay stays well inside the 1 MiB TA vertex buffer.
const int ARENA_VERTS = 20480;
static pvr_vertex_t g_arena[ARENA_VERTS] __attribute__((aligned(32)));
struct Batch3 { int mat, first, count; };
static Batch3 g_batches3[512];      // battle: props + characters + weapons + FX batches
static int g_nbatches3 = 0, g_arena_used = 0, g_arena_open = -1;
static int g_arena_peak = 0, g_arena_drops = 0;     // DIAG report

static void flush_3d(pvr_list_t list) {
    for (int i = 0; i < g_nbatches3; ++i) {
        const Batch3& b = g_batches3[i];
        const Material3& m = g_mats[b.mat - 1];
        if (m.list != list) continue;
        const pvr_poly_hdr_t* hdr = g_fog_on ? &m.hdr_fog : &m.hdr;
        pvr_prim(hdr, sizeof(*hdr));
#if defined(R3D_SAFE)
        for (int k = 0; k < b.count; ++k) pvr_prim(&g_arena[b.first + k], sizeof(pvr_vertex_t));
#else
        pvr_prim(&g_arena[b.first], (size_t)b.count * sizeof(pvr_vertex_t));
#endif
    }
}

static Sprite g_sprites[eng::MAX_SPRITES];
static int g_sprite_count = 0;
static uint32_t g_vram_bytes = 0;

// --- Sprite batching by texture + filter -------------------------------------
enum SpriteFilterMode : int { SF_NEAREST = 0, SF_BILINEAR = 1 };
struct SpriteCmd { float cx, cy, w, h, a, u0, v0, u1, v1, z; uint32_t argb; };

static std::vector<SpriteCmd> g_q_nearest[eng::MAX_SPRITES];
static std::vector<SpriteCmd> g_q_bilinear[eng::MAX_SPRITES];

// Slice 3D-5 (DoD T3, no heap allocation per frame): the queues of alpha textures (icons, fonts, the dialog
// font: a dialog page + its shadow + a choice box is ~450 glyph sprites) are reserved when the texture is made,
// so a frame never grows a vector. Opaque textures (portraits, sheets) draw a few sprites at most.
static void reserve_queues(int slot, bool alpha) {
    g_q_nearest[slot].reserve(alpha ? 640 : 16);
    g_q_bilinear[slot].reserve(alpha ? 64 : 16);
}
static int g_seq = 0;              // sprite draw calls this frame (auto depth)

struct RectCmd { int list; float x, y, w, h; float z; uint32_t argb; };
static RectCmd g_rect_q[2048];
static int g_rect_qn = 0;

static inline void rectq_push(int list, float x, float y, float w, float h, float z, uint32_t argb) {
    if (g_rect_qn >= (int)(sizeof(g_rect_q) / sizeof(g_rect_q[0]))) return;
    g_rect_q[g_rect_qn++] = { list, x, y, w, h, z, argb };
}

static void emit_sprite_quads(const pvr_poly_hdr_t* hdr, std::vector<SpriteCmd>& q) {
    if (q.empty()) return;
    pvr_prim((void*)hdr, sizeof(*hdr));
    for (const auto& c : q) {
        float hw = c.w * 0.5f, hh = c.h * 0.5f;
        float cs = 1.0f, sn = 0.0f;
        if (c.a != 0.0f) { const shz_sincos_t r = shz_sincosf(c.a); cs = r.cos; sn = r.sin; }   // one FSCA
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
struct InputState { uint32_t buttons; int joyx; int joyy; };   // joyx/joyy: -128..127, + = right/down
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
static bool down_mask(uint32_t mask) { return (g_in.buttons & mask) != 0; }

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

// --- Debug input overlay (top-left; on top of everything): DIAG / INPUT_OVERLAY builds only ------------
#if BATTLE_DIAG || defined(INPUT_OVERLAY)
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
#endif

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
    BOOT("png %s\n", path);
    if (g_sprite_count >= MAX_SPRITES) { BOOT("sprite limit reached\n"); return 0; }
    ::Sprite* s = &g_sprites[g_sprite_count];
    memset(&s->img, 0, sizeof(s->img));
    s->tex = NULL;
    s->alpha = alpha;

    int rc = png_to_img(path, alpha ? PNG_MASK_ALPHA : PNG_NO_ALPHA, &s->img);
    if (rc < 0 || !s->img.data || s->img.byte_count == 0) {
        BOOT("png_to_img failed for %s\n", path);
        return 0;
    }
    DIAG("  png ok: %lux%lu fmt=%lx bytes=%lu\n", (unsigned long)s->img.w, (unsigned long)s->img.h,
         (unsigned long)s->img.fmt, (unsigned long)s->img.byte_count);
    s->tex = pvr_mem_malloc(s->img.byte_count);
    if (!s->tex) { BOOT("pvr_mem_malloc(%lu) failed\n", (unsigned long)s->img.byte_count); return 0; }
    pvr_txr_load_kimg(&s->img, s->tex, PVR_TXRLOAD_16BPP);
    g_vram_bytes += s->img.byte_count;

    const pvr_list_t list = alpha ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;   // KOS 2.1: typed enum
    s->pvr_fmt = alpha ? (int)PVR_TXRFMT_ARGB1555 : (int)PVR_TXRFMT_RGB565;
    const int fmt  = s->pvr_fmt | (int)PVR_TXRFMT_TWIDDLED;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, list, fmt, s->img.w, s->img.h, s->tex, PVR_FILTER_NONE);
    pvr_poly_compile(&s->hdr_nearest, &cxt);
    pvr_poly_cxt_txr(&cxt, list, fmt, s->img.w, s->img.h, s->tex, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&s->hdr_bilinear, &cxt);

    DIAG("  sprite %d ready, VRAM textures total %lu KiB\n", g_sprite_count + 1, (unsigned long)(g_vram_bytes / 1024));
    reserve_queues(g_sprite_count, alpha);
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

void sprite_reserve(Sprite handle, int nearest, int bilinear) {
    if (handle < 1 || handle > g_sprite_count) return;
    if (nearest > 0) g_q_nearest[handle - 1].reserve((size_t)nearest);
    if (bilinear > 0) g_q_bilinear[handle - 1].reserve((size_t)bilinear);
}

void sprite_draw(Sprite handle, float x, float y, float w, float h, float a, int filter) {
    sprite_draw_uv(handle, x, y, w, h, a, 0.0f, 0.0f, 1.0f, 1.0f, filter);
}

// Memory texture (.dmf): same upload path as the PNGs (kos_img_t + pvr_txr_load_kimg twiddles it).
Sprite texture_create(const uint16_t* texels, int w, int h, int fmt) {
    DIAG("texture_create(%dx%d fmt %d) slot %d\n", w, h, fmt, g_sprite_count);
    if (g_sprite_count >= MAX_SPRITES) { BOOT("sprite limit reached\n"); return 0; }
    if (w < 8 || h < 8 || w > 1024 || h > 1024 || (w & (w - 1)) || (h & (h - 1)) || fmt < 1 || fmt > 3) return 0;
    ::Sprite* s = &g_sprites[g_sprite_count];
    memset(&s->img, 0, sizeof(s->img));
    s->img.data = (void*)texels;                 // lives in the .dmf arena for the whole session
    s->img.w = (uint32_t)w;
    s->img.h = (uint32_t)h;
    s->img.fmt = fmt == 1 ? KOS_IMG_FMT_RGB565 : (fmt == 2 ? KOS_IMG_FMT_ARGB4444 : KOS_IMG_FMT_ARGB1555);
    s->img.byte_count = (uint32_t)(w * h * 2);
    s->alpha = fmt != 1;
    s->pvr_fmt = fmt == 1 ? (int)PVR_TXRFMT_RGB565 : (fmt == 2 ? (int)PVR_TXRFMT_ARGB4444 : (int)PVR_TXRFMT_ARGB1555);
    s->tex = pvr_mem_malloc(s->img.byte_count);
    if (!s->tex) { BOOT("pvr_mem_malloc failed (texture_create)\n"); return 0; }
    pvr_txr_load_kimg(&s->img, s->tex, PVR_TXRLOAD_16BPP);
    g_vram_bytes += s->img.byte_count;
    const pvr_list_t list = s->alpha ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, list, s->pvr_fmt | (int)PVR_TXRFMT_TWIDDLED, w, h, s->tex, PVR_FILTER_NONE);
    pvr_poly_compile(&s->hdr_nearest, &cxt);
    pvr_poly_cxt_txr(&cxt, list, s->pvr_fmt | (int)PVR_TXRFMT_TWIDDLED, w, h, s->tex, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&s->hdr_bilinear, &cxt);
    DIAG("  texture %d ready, VRAM textures total %lu KiB\n", g_sprite_count + 1, (unsigned long)(g_vram_bytes / 1024));
    reserve_queues(g_sprite_count, s->alpha);
    return ++g_sprite_count;
}

// VQ texture (the village ground): the codebook and the twiddled index bytes go to VRAM as they are; the texture
// address is the codebook's. GROUND_NO_VQ: decoded here and uploaded as plain RGB565 (w*h*2 bytes of VRAM) -
// the bisecting build if the ground ever shows scrambled blocks on hardware (TRIAGE).
Sprite texture_create_vq(const void* data, int bytes, int w, int h) {
    DIAG("texture_create_vq(%dx%d, %d bytes) slot %d\n", w, h, bytes, g_sprite_count);
    if (g_sprite_count >= MAX_SPRITES) { BOOT("sprite limit reached\n"); return 0; }
    if (w != h || w < 16 || w > 1024 || (w & (w - 1)) || bytes != VQ_CODEBOOK_BYTES + w * h / 4) return 0;
#if defined(GROUND_NO_VQ)
    uint16_t* lin = (uint16_t*)malloc((size_t)w * h * 2);          // boot time only; kept (KOS keeps the pointer)
    if (!lin) return 0;
    vq_decode((const uint8_t*)data, w, h, lin);
    return texture_create(lin, w, h, 1);
#else
    ::Sprite* s = &g_sprites[g_sprite_count];
    memset(&s->img, 0, sizeof(s->img));
    s->img.w = (uint32_t)w;
    s->img.h = (uint32_t)h;
    s->img.byte_count = 0;                        // not a kos_img: texture_update refuses it
    s->alpha = false;
    s->pvr_fmt = (int)PVR_TXRFMT_RGB565 | (int)PVR_TXRFMT_VQ_ENABLE;
    s->tex = pvr_mem_malloc((size_t)bytes);
    if (!s->tex) { BOOT("pvr_mem_malloc failed (texture_create_vq)\n"); return 0; }
    pvr_txr_load((void*)data, s->tex, (size_t)bytes);
    g_vram_bytes += (uint32_t)bytes;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, s->pvr_fmt | (int)PVR_TXRFMT_TWIDDLED, w, h, s->tex, PVR_FILTER_NONE);
    pvr_poly_compile(&s->hdr_nearest, &cxt);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, s->pvr_fmt | (int)PVR_TXRFMT_TWIDDLED, w, h, s->tex, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&s->hdr_bilinear, &cxt);
    DIAG("  VQ texture %d ready, VRAM textures total %lu KiB\n", g_sprite_count + 1, (unsigned long)(g_vram_bytes / 1024));
    reserve_queues(g_sprite_count, false);
    return ++g_sprite_count;
#endif
}

// Slice 3D-5 (portraits): new texels for a memory texture, into the same VRAM block, through the same
// twiddling upload as texture_create (pvr_txr_load_kimg). The caller (src/portrait.cpp) only rewrites a
// slot that the last 2 frames did not draw, so the PVR never samples a half-written texture.
// twiddled: already in PVR order (src/portrait.cpp twiddles its cache at load time) - a plain 32 KiB copy
// (pvr_txr_load: store queues), no per-texel work in the frame; otherwise KOS twiddles as at creation.
bool texture_update(Sprite handle, const uint16_t* texels, bool twiddled) {
    if (handle < 1 || handle > g_sprite_count || !texels) return false;
    ::Sprite* s = &g_sprites[handle - 1];
    if (!s->tex || s->img.byte_count == 0) return false;
    if (twiddled) {
        pvr_txr_load((void*)texels, s->tex, s->img.byte_count);
        return true;
    }
    kos_img_t img = s->img;                        // same w, h, format and byte count as at creation
    img.data = (void*)texels;
    pvr_txr_load_kimg(&img, s->tex, PVR_TXRLOAD_16BPP);
    return true;
}

Material material_create(Sprite tex, int flags) {
    if (g_nmats >= (int)(sizeof(g_mats) / sizeof(g_mats[0]))) { BOOT("material limit reached\n"); return 0; }
    if (tex < 0 || tex > g_sprite_count) return 0;
    Material3& m = g_mats[g_nmats];
    m.list = (flags & (MAT_TRANSLUCENT | MAT_ADDITIVE | MAT_MULTIPLY)) ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
    pvr_poly_cxt_t cxt;
    if (tex) {
        const ::Sprite& s = g_sprites[tex - 1];
        pvr_poly_cxt_txr(&cxt, m.list, s.pvr_fmt | (int)PVR_TXRFMT_TWIDDLED, (int)s.img.w, (int)s.img.h, s.tex,
                         (flags & MAT_BILINEAR) ? PVR_FILTER_BILINEAR : PVR_FILTER_NONE);
    } else {
        pvr_poly_cxt_col(&cxt, m.list);
    }
    // Front faces are counter-clockwise on screen (render3d.h): cull the clockwise ones.
    cxt.gen.culling = (flags & MAT_CULL) ? PVR_CULLING_CW : (flags & MAT_CULL_FRONT) ? PVR_CULLING_CCW : PVR_CULLING_NONE;
    if (flags & MAT_ADDITIVE) {                   // glows/sparks/fire: dst + src * src_alpha
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_ONE;
    }
    if (flags & MAT_MULTIPLY) {                   // slice 3D-6: a tint over the frame: dst * src colour
        cxt.blend.src = PVR_BLEND_DESTCOLOR;
        cxt.blend.dst = PVR_BLEND_ZERO;
    }
    pvr_poly_compile(&m.hdr, &cxt);
    // Slice 3D-6: the same with PVR table fog (the barrow). Screen tints are never fogged.
    if (!(flags & MAT_MULTIPLY)) cxt.gen.fog_type = PVR_FOG_TABLE;
    pvr_poly_compile(&m.hdr_fog, &cxt);
    DIAG("material %d: tex %d flags %d list %d\n", g_nmats + 1, tex, flags, (int)m.list);
    return ++g_nmats;
}

Vert3* vtx_begin(int max) {
    if (g_arena_open >= 0 || max <= 0 || g_nbatches3 >= (int)(sizeof(g_batches3) / sizeof(g_batches3[0])) ||
        g_arena_used + max > ARENA_VERTS) { ++g_arena_drops; return nullptr; }
    g_arena_open = g_arena_used;
    return reinterpret_cast<Vert3*>(&g_arena[g_arena_used]);
}

int vtx_room() { return g_arena_open >= 0 ? 0 : ARENA_VERTS - g_arena_used; }

void vtx_end(Material m, int used) {
    if (g_arena_open < 0) return;
    const int first = g_arena_open;
    g_arena_open = -1;
    if (used <= 0 || m < 1 || m > g_nmats) return;
    g_arena_used = first + used;
    if (g_arena_used > g_arena_peak) g_arena_peak = g_arena_used;
    g_batches3[g_nbatches3++] = Batch3{ m, first, used };
}

// Slice 3D-6: the barrow's painted ground reuses the village's VQ slot (same size). The caller guarantees the slot
// was not drawn in the last 2 frames (a map change happens under a full fade), as for texture_update.
bool texture_update_vq(Sprite handle, const void* data, int bytes) {
    if (handle < 1 || handle > g_sprite_count || !data) return false;
    ::Sprite* s = &g_sprites[handle - 1];
    if (!s->tex) return false;
    const int w = (int)s->img.w, h = (int)s->img.h;
    if (bytes != VQ_CODEBOOK_BYTES + w * h / 4) return false;
#if defined(GROUND_NO_VQ)
    if (!s->img.data || s->img.byte_count != (uint32_t)(w * h * 2)) return false;
    vq_decode((const uint8_t*)data, w, h, (uint16_t*)s->img.data);   // the slot's own linear buffer (boot-time malloc)
    pvr_txr_load_kimg(&s->img, s->tex, PVR_TXRLOAD_16BPP);
#else
    if (s->img.byte_count != 0 || !(s->pvr_fmt & (int)PVR_TXRFMT_VQ_ENABLE)) return false;
    pvr_txr_load((void*)data, s->tex, (size_t)bytes);
#endif
    DIAG("texture_update_vq(%d, %d bytes)\n", handle, bytes);
    return true;
}

// Slice 3D-6: table fog (the barrow at night). make NO_FOG=1 keeps it off (TRIAGE: if fog looks wrong on hardware).
void set_fog(bool on, uint32_t rgb, float start, float end) {
#if defined(NO_FOG)
    (void)on; (void)rgb; (void)start; (void)end;
    g_fog_on = false;
#else
    // called every field frame: only a change touches the PVR's fog registers and table
    static uint32_t last_rgb = 0xFFFFFFFFu;
    static float last_start = -1.0f, last_end = -1.0f;
    if (on && (rgb != last_rgb || start != last_start || end != last_end)) {
        pvr_fog_table_color(1.0f, (float)((rgb >> 16) & 255) / 255.0f, (float)((rgb >> 8) & 255) / 255.0f, (float)(rgb & 255) / 255.0f);
        pvr_fog_table_linear(start, end > start + 0.1f ? end : start + 0.1f);
        last_rgb = rgb; last_start = start; last_end = end;
    }
    if (on != g_fog_on) DIAG("fog %s: %06lx %.1f..%.1f\n", on ? "on" : "off", (unsigned long)(rgb & 0xFFFFFF), (double)start, (double)end);
    g_fog_on = on;
#endif
}

void audio_event(const char*) {}                 // slice 3D-6: no audio path yet (the hooks only; see engine.h)

void set_clear_color(uint32_t rgb) {
    pvr_set_bg_color((float)((rgb >> 16) & 255) / 255.0f, (float)((rgb >> 8) & 255) / 255.0f, (float)(rgb & 255) / 255.0f);
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

int input_axis(Axis a) { return a == AXIS_X ? g_in.joyx : (a == AXIS_Y ? g_in.joyy : 0); }

// Same 24-bit LCG as the verified host build (platform/host/host_engine.cpp); no newlib rand().
static uint32_t g_rng = 12345;
float random01() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;
}

// Game messages (dmf: ..., map errors) use the same path as the breadcrumbs: dbgio directly,
// independent of dbglog's level filter.
void dbg_print(const char* s) { dbgio_printf("%s\n", s); dbgio_flush(); }
void boot_log(const char* s) { BOOT("%s\n", s); }

int file_read(const char* path, char* buf, int cap) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) return -1;
    int n = (int)fs_read(f, buf, (size_t)cap);
    fs_close(f);
    if (n < 0 || n >= cap) return -1;
    buf[n] = '\0';
    return n;
}

bool file_exists(const char* path) {
    file_t f = fs_open(path, O_RDONLY);
    if (f < 0) return false;
    fs_close(f);
    return true;
}

void seed_random(uint32_t seed) { g_rng = seed ? seed : 12345u; }

} // namespace eng

// ============================================================================
int main(int, char**) {
    g_boot_ms0 = timer_ms_gettime64();
#if defined(PVR_DEFAULTS)
    // make PVR_DEFAULTS=1: the stock init (512 KiB TA vertex buffers), for A/B triage.
    pvr_init_defaults();
    const unsigned vbuf_kib = 512;
#else
    // Same as pvr_init_defaults() in this KOS (read from its code: OP/TR bins 16, overflow 3,
    // autosort on, no DMA/FSAA) except a 1 MiB TA vertex buffer instead of 512 KiB: the village
    // submits up to ~14k vertices per frame (~450 KB + headers); an overflow drops polygons.
    // VRAM (pvr_allocate_buffers): each of the 2 TA buffer sets sits in its own 4 MiB bank -
    // vertex buffer + OPBs + tile matrix + one frame buffer, ~1.8 MiB of 4 - and textures get
    // the rest (~4.4 MiB; the game uses ~0.6 MiB).
    pvr_init_params_t params;
    memset(&params, 0, sizeof params);
    params.opb_sizes[0] = PVR_BINSIZE_16;        // OP
    params.opb_sizes[1] = PVR_BINSIZE_0;         // OP modifiers
    params.opb_sizes[2] = PVR_BINSIZE_16;        // TR
    params.opb_sizes[3] = PVR_BINSIZE_0;         // TR modifiers
    params.opb_sizes[4] = PVR_BINSIZE_0;         // PT
    params.vertex_buf_size = 1024 * 1024;
    params.opb_overflow_count = 3;
    pvr_init(&params);
    const unsigned vbuf_kib = 1024;
#endif
    dbgio_init();
    dbgio_dev_select("scif");
    dbglog(DBG_INFO, "=== DREAMCAST BOOT (C++ field + battle) ===\n");
    {   // SH4ZAM: headers compiled against vs library linked (they must match; see src/hwmath.h)
        uint8_t maj = 0, pat = 0; uint16_t mnr = 0;
        shz_version_fields(shz_version_linked(), &maj, &mnr, &pat);
        BOOT("sh4zam %u.%u.%u linked (headers %u.%u.%u, %s back-end)%s\n", (unsigned)maj, (unsigned)mnr, (unsigned)pat,
             (unsigned)SHZ_VERSION_MAJOR, (unsigned)SHZ_VERSION_MINOR, (unsigned)SHZ_VERSION_PATCH, SHZ_BACKEND == SHZ_SH4 ? "SH4" : "software",
             shz_version_linked() == SHZ_VERSION ? "" : "  ** MISMATCH: rebuild against one SH4ZAM install **");
    }
    BOOT("pvr up: TA vertex buffer %u KiB, texture VRAM free %lu KiB\n", vbuf_kib,
         (unsigned long)(pvr_mem_available() / 1024));
    DIAG("main: pvr+dbgio up. sizeof(pvr_vertex_t)=%u sizeof(::Sprite)=%u\n",
         (unsigned)sizeof(pvr_vertex_t), (unsigned)sizeof(::Sprite));

    check_file("/rd/maps/village.txt");
    check_file("/rd/tiles.png");
    check_file("/rd/hero.png");
    check_file("/rd/ui_icons.png");
    check_file("/rd/font.png");
    check_file("/rd/models/knight.dmf");
    check_file("/rd/models/skel_warrior.dmf");
    check_file("/rd/models/house_stone_r0.dmf");  // the VS1 village art (docs/SLICE_VS1_ART.md)
    check_file("/rd/models/wagon_r0.dmf");
    check_file("/rd/tex/glass.png");
    check_file("/rd/maps/village.gnd");           // the painted ground (docs/GROUND_ART.md)
    check_file("/rd/tex/ground.png");
    check_file("/rd/models/skel_mage.dmf");
    check_file("/rd/anims/rig_medium.dma");       // slice 3D-4: the 3D battle
    check_file("/rd/arenas/village.txt");
    check_file("/rd/arenas/barrow.txt");
    check_file("/rd/models/skel_rogue.dmf");
    check_file("/rd/models/house_stone_b.dmf");
    check_file("/rd/tex/fx.tex");
    check_file("/rd/tex/dirt.png");
    // slice 3D-5: dialog, scripts, portraits, Wren and Brom
    check_file("/rd/fonts/dialog.fnt");
    check_file("/rd/fonts/dialog.tex");
    check_file("/rd/scripts/village.txt");
    check_file("/rd/scripts/speakers.txt");
    check_file("/rd/models/wren.dmf");
    check_file("/rd/models/brom.dmf");
    // slice 3D-6: the barrow, Corwin, the frame around the game
    check_file("/rd/maps/maps.txt");
    check_file("/rd/maps/lights.txt");
    check_file("/rd/maps/barrow.txt");
    check_file("/rd/maps/barrow.gnd");
    check_file("/rd/scripts/barrow.txt");
    check_file("/rd/scripts/ending.txt");
    check_file("/rd/scripts/credits.txt");
    check_file("/rd/models/corwin.dmf");
    check_file("/rd/models/waystone.dmf");
    check_file("/rd/models/dead_tree.dmf");
    check_file("/rd/models/iron_fence.dmf");
    check_file("/rd/tex/logo.tex");
    check_file("/rd/ui/ui.uia");                  // the UI font and icons (docs/UI_ART.md)
    {
        static const char* const PORTRAITS[] = { "rowan", "wren", "brom", "corwin", "warden" };
        static const char* const VARIANTS[] = { "neutral", "happy", "worried" };
        char p[64];
        for (const char* n : PORTRAITS)
            for (const char* v : VARIANTS) { snprintf(p, sizeof p, "/rd/portraits/%s_%s.dtx", n, v); check_file(p); }
    }

    eng::seed_random((uint32_t)timer_ms_gettime64());
#if !defined(DIAG_SKIP_GAME)
    DIAG("main: game_init begin\n");
    BOOT("game_init begin\n");
    if (!bt::game_boot()) BOOT("game_boot: the village or the scripts failed to load (see 'map error' / 'script:' above)\n");
    BOOT("game_init done: %d textures, %lu KiB texture VRAM used, %lu KiB free, heap %d KiB\n", g_sprite_count,
         (unsigned long)(g_vram_bytes / 1024), (unsigned long)(pvr_mem_available() / 1024), mallinfo().uordblks / 1024);
    DIAG("main: game_init done, VRAM textures %lu KiB in %d sprite slots\n",
         (unsigned long)(g_vram_bytes / 1024), g_sprite_count);
#else
    DIAG("main: DIAG_SKIP_GAME - game code disabled\n");
#endif

    // Fixed 60 Hz timestep (slice task A5): accumulate elapsed microseconds x60 and run one
    // game tick per 1,000,000 (exact, no drift). At most 4 catch-up ticks per displayed frame.
    uint64_t last = timer_us_gettime64();
    uint64_t acc60 = 0;
    uint32_t worst_us = 0, over_budget = 0;          // frame-time probe (DIAG report)
    int heap_prev = mallinfo().uordblks;           // heap-in-use probe (DoD T3: no per-frame allocation)
    while (1) {
        ++g_diag_frame;
        poll_input();

        uint64_t now = timer_us_gettime64();
        acc60 += (now - last) * 60u;
        last = now;
        uint64_t work_start = now;
        int steps = 0;
        while (acc60 >= 1000000u && steps < 4) {
#if !defined(DIAG_SKIP_GAME) && !defined(DIAG_SKIP_UPDATE)
            bt::game_tick();
#endif
            acc60 -= 1000000u;
            ++steps;
        }
        if (steps == 4) acc60 = 0;               // fell far behind (e.g. after loading): drop the backlog
        BOOT_F("frame %d: %d tick(s) done\n", g_diag_frame, steps);
        DIAG_F("frame %d: %d tick(s)\n", g_diag_frame, steps);

        DIAG_F("frame %d: wait_ready\n", g_diag_frame);
        pvr_wait_ready();
        BOOT_F("frame %d: PVR ready\n", g_diag_frame);
        uint64_t draw_start = timer_us_gettime64();
        pvr_scene_begin();
        g_seq = 0;
        g_arena_used = 0; g_nbatches3 = 0; g_arena_open = -1;
#if !defined(DIAG_SKIP_GAME) && !defined(DIAG_SKIP_DRAW)
        bt::game_draw();                          // queues sprites + rects
#endif
#if BATTLE_DIAG || defined(INPUT_OVERLAY)
        draw_input_overlay();       // debug: pad state top left (make DIAG=1 or INPUT_OVERLAY=1); it sits on the objective HUD
#endif
        BOOT_F("frame %d: drawn (%d 3D verts in %d batches, %d rects)\n", g_diag_frame, g_arena_used, g_nbatches3, g_rect_qn);

        DIAG_F("frame %d: OP list (rects queued=%d)\n", g_diag_frame, g_rect_qn);
        pvr_list_begin(PVR_LIST_OP_POLY);
        flush_3d(PVR_LIST_OP_POLY);
        flush_sprite_batches(false);
        rect_flush_list(PVR_LIST_OP_POLY);
        pvr_list_finish();

        DIAG_F("frame %d: TR list\n", g_diag_frame);
        pvr_list_begin(PVR_LIST_TR_POLY);
        flush_3d(PVR_LIST_TR_POLY);
        flush_sprite_batches(true);
        rect_flush_list(PVR_LIST_TR_POLY);
        pvr_list_finish();

        BOOT_F("frame %d: lists submitted\n", g_diag_frame);
        pvr_scene_finish();
        uint64_t done = timer_us_gettime64();
        BOOT_F("frame %d: scene finished\n", g_diag_frame);
        if (g_diag_frame == 60 || g_diag_frame == 600) {
            pvr_stats_t ps;
            pvr_get_stats(&ps);
            BOOT("frame %d: running, %.1f fps, TA vertex buffer peak %lu KiB of %u\n", g_diag_frame,
                 (double)ps.frame_rate, (unsigned long)(ps.vtx_buffer_used_max / 1024), vbuf_kib);
        }
        g_rect_qn = 0;

        // CPU time spent on this frame = game ticks + building/submitting the scene
        // (excludes waiting for the PVR / vblank).
        uint32_t work_us = (uint32_t)((draw_start - work_start) + (done - draw_start));
        if (work_us > worst_us) worst_us = work_us;
        if (work_us > 16667) ++over_budget;
        if (BATTLE_DIAG && g_diag_frame % 300 == 0) {
            int heap_now = mallinfo().uordblks;
            DIAG("frames %d: worst CPU frame %lu us, frames over 16.7 ms: %lu, heap in use %d B (%+d since last report)\n",
                 g_diag_frame, (unsigned long)worst_us, (unsigned long)over_budget, heap_now, heap_now - heap_prev);
            pvr_stats_t ps;
            pvr_get_stats(&ps);
            DIAG("  3D: arena peak %d/%d verts, drops %d; PVR vtx buffer peak %lu B, %.1f fps\n",
                 g_arena_peak, ARENA_VERTS, g_arena_drops, (unsigned long)ps.vtx_buffer_used_max, (double)ps.frame_rate);
            g_arena_peak = 0;
            heap_prev = heap_now;
            worst_us = 0;
        }
        vid_waitvbl();
    }
}
