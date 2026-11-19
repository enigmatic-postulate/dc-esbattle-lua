// host_engine.cpp - engine.h implementation for the host harness. Records every engine
// call as a text trace (format documented in harness/compare.py / tools/render_trace.py).
#include "../../src/engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <algorithm>
#include <vector>

// Allocation counter (slice 3D-5, DoD T3 "no heap allocation per frame"): built with -DHOST_ALLOC_COUNT,
// malloc/calloc/realloc (and operator new, which uses malloc) are counted while g_alloc_watch is set -
// trace_main sets it around game_tick() + game_draw() after the boot. glibc only (ELF interposition).
int  g_alloc_watch = 0;
long g_alloc_count = 0;
#ifdef HOST_ALLOC_COUNT
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void* malloc(size_t n) { if (g_alloc_watch) ++g_alloc_count; return __libc_malloc(n); }
extern "C" void* calloc(size_t a, size_t b) { if (g_alloc_watch) ++g_alloc_count; return __libc_calloc(a, b); }
extern "C" void* realloc(void* p, size_t n) { if (g_alloc_watch) ++g_alloc_count; return __libc_realloc(p, n); }
#endif

FILE*       g_trace = nullptr;
bool        g_audio_trace = false;        // slice 3D-6: audio hooks (no audio yet) - `A` lines in --story traces only
bool        g_trace_draws = true;         // false: --state-only (only F and X lines)
uint32_t    g_buttons = 0;                // active-high bitmask set by trace_main
int         g_axis[2] = { 0, 0 };         // analog stick (-128..127) set by trace_main
const char* g_romdisk = "romdisk";        // host directory that stands in for /rd
static uint32_t g_rng = 12345;            // shared LCG (see lua_ref.py)

static const char* g_paths[eng::MAX_SPRITES];
static bool g_alpha[eng::MAX_SPRITES];
static int g_nsprites = 0;

// ---- frame dumps (tools/render_frame.py): every draw of selected ticks, 3D vertices included
static FILE*       g_dump = nullptr;
const char*        g_dump_dir = nullptr;  // set by trace_main (--dump DIR)
static bool        g_tex_written[eng::MAX_SPRITES];
struct MemTex { int w, h, fmt, version; std::vector<uint16_t> texels; int vq_bytes; };   // vq_bytes: VQ (VRAM = that)
static MemTex      g_memtex[eng::MAX_SPRITES];

// ---- 3D: materials + per-frame vertex arena (same limits as the KOS backend)
struct Mat { eng::Sprite tex; int flags; };
static Mat g_mats[256];
static int g_nmats = 0;
static const int ARENA_VERTS = 20480;   // as the KOS backend (slice 3D-5: was 16,384)
alignas(32) static eng::Vert3 g_arena[ARENA_VERTS];   // 32-byte vertices, like the PVR arena (shz_memcpy32_1 contract)
static int g_arena_used = 0, g_open = -1, g_batches = 0, g_batch_verts = 0;
static uint32_t g_clear = 0;

static bool g_fog_on = false;           // slice 3D-6: eng::set_fog, written to the frame dumps
static uint32_t g_fog_rgb = 0;
static float g_fog_start = 0, g_fog_end = 0;
bool host_fog_on() { return g_fog_on; }
static int g_drops = 0;                 // batches refused this frame (arena full), like the KOS backend's drops
int host_arena_used() { return g_arena_used; }
// Resource report (slice 3D-5 budgets): texture slots and their VRAM as the KOS backend would allocate it
// (16-bit texels; PNG sizes from their IHDR), materials; per frame: 2D sprites and rects queued.
static int g_sprites_frame = 0, g_rects_frame = 0;
int host_sprites() { return g_sprites_frame; }
// The KOS backend's per-texture sprite queues (platform/kos/main.cpp reserve_queues / sprite_reserve), mirrored: a frame
// that queues more sprites on a texture than KOS reserved would make its std::vector grow - a heap allocation after
// the boot on the Dreamcast, invisible to the host's allocation counter. Counted here; --sprite-queues reports them.
static int g_q_res[eng::MAX_SPRITES][2], g_q_n[eng::MAX_SPRITES][2], g_q_peak[eng::MAX_SPRITES][2];
static long g_q_over = 0;
static void queue_defaults(int slot, bool alpha) {
    g_q_res[slot][0] = alpha ? 640 : 16; g_q_res[slot][1] = alpha ? 64 : 16;
}
long host_queue_overflows() { return g_q_over; }
void host_queue_report(FILE* f) {
    fprintf(f, "D sprite queues: overflow draws %ld", g_q_over);
    for (int i = 0; i < eng::MAX_SPRITES; ++i)
        if (g_q_peak[i][0] || g_q_peak[i][1])
            fprintf(f, "; tex %d peak %d/%d nearest, %d/%d bilinear", i + 1, g_q_peak[i][0], g_q_res[i][0], g_q_peak[i][1], g_q_res[i][1]);
    fprintf(f, "\n");
}
int host_rects() { return g_rects_frame; }
int host_materials() { return g_nmats; }
int host_textures() { return g_nsprites; }
extern const char* g_romdisk;
long host_vram_bytes() {
    long total = 0;
    for (int i = 0; i < g_nsprites; ++i) {
        if (!g_paths[i]) { total += g_memtex[i].vq_bytes ? g_memtex[i].vq_bytes : (long)g_memtex[i].w * g_memtex[i].h * 2; continue; }
        char p[512];
        snprintf(p, sizeof p, "%s/%s", g_romdisk, strncmp(g_paths[i], "/rd/", 4) == 0 ? g_paths[i] + 4 : g_paths[i]);
        FILE* f = fopen(p, "rb");
        unsigned char h[24] = { 0 };
        if (f) { if (fread(h, 1, 24, f) != 24) h[0] = 0; fclose(f); }
        const long w = (long)h[16] << 24 | h[17] << 16 | h[18] << 8 | h[19], hh = (long)h[20] << 24 | h[21] << 16 | h[22] << 8 | h[23];
        total += w * hh * 2;
    }
    return total;
}
int host_drops() { return g_drops; }
int host_batches() { return g_batches; }

void host_frame_begin(int tick, bool dump) {
    g_arena_used = 0; g_open = -1; g_batches = 0; g_batch_verts = 0; g_drops = 0;
    g_sprites_frame = 0; g_rects_frame = 0;
    memset(g_q_n, 0, sizeof g_q_n);
    if (dump && g_dump_dir) {
        char p[512];
        snprintf(p, sizeof p, "%s/frame_%05d.txt", g_dump_dir, tick);
        g_dump = fopen(p, "w");
        if (g_dump) {
            fprintf(g_dump, "frame %d\nclear %06X\n", tick, (unsigned)g_clear);
            if (g_fog_on) fprintf(g_dump, "fog %06X %.4g %.4g\n", (unsigned)g_fog_rgb, g_fog_start, g_fog_end);
            for (int i = 0; i < g_nsprites; ++i) {
                if (g_paths[i]) { fprintf(g_dump, "tex %d file %s %d\n", i + 1, g_paths[i], g_alpha[i] ? 1 : 0); continue; }
                const MemTex& t = g_memtex[i];
                // a texture_update()d slot (portraits) gets a file per version: tex_<i>[_v<k>].bin
                char name[48];
                if (t.version) snprintf(name, sizeof name, "tex_%d_v%d.bin", i + 1, t.version);
                else snprintf(name, sizeof name, "tex_%d.bin", i + 1);
                snprintf(p, sizeof p, "%s/%s", g_dump_dir, name);
                if (!g_tex_written[i]) {
                    FILE* f = fopen(p, "wb");
                    if (f) { fwrite(t.texels.data(), 2, t.texels.size(), f); fclose(f); g_tex_written[i] = true; }
                }
                fprintf(g_dump, "tex %d mem %d %d %d %s\n", i + 1, t.w, t.h, t.fmt, name);
            }
            for (int i = 0; i < g_nmats; ++i) fprintf(g_dump, "mat %d %d %d\n", i + 1, g_mats[i].tex, g_mats[i].flags);
        }
    }
}

void host_frame_end() {
    if (g_dump) { fclose(g_dump); g_dump = nullptr; }
}

namespace eng {

Sprite sprite_load(const char* path, bool alpha) {
    if (g_nsprites >= MAX_SPRITES) return 0;
    g_alpha[g_nsprites] = alpha;
    queue_defaults(g_nsprites, alpha);
    g_paths[g_nsprites++] = path;
    if (g_trace_draws) fprintf(g_trace, "L %s %d\n", path, alpha ? 1 : 0);
    return g_nsprites;
}

Sprite texture_create(const uint16_t* texels, int w, int h, int fmt) {
    if (g_nsprites >= MAX_SPRITES || w < 8 || h < 8 || (w & (w - 1)) || (h & (h - 1)) || fmt < 1 || fmt > 3) return 0;
    MemTex& t = g_memtex[g_nsprites];
    t.w = w; t.h = h; t.fmt = fmt; t.version = 0; t.texels.assign(texels, texels + w * h); t.vq_bytes = 0;
    g_alpha[g_nsprites] = fmt != 1;
    queue_defaults(g_nsprites, fmt != 1);
    g_paths[g_nsprites++] = nullptr;
    if (g_trace_draws) fprintf(g_trace, "L mem:%dx%d:fmt%d %d\n", w, h, fmt, fmt != 1 ? 1 : 0);
    return g_nsprites;
}

// VQ: decoded for the frame dumps (tools/render_frame.py reads linear RGB565); VRAM counted as the PVR holds it.
Sprite texture_create_vq(const void* data, int bytes, int w, int h) {
    if (g_nsprites >= MAX_SPRITES || w != h || w < 16 || w > 1024 || (w & (w - 1)) || bytes != VQ_CODEBOOK_BYTES + w * h / 4) return 0;
    std::vector<uint16_t> lin((size_t)w * h);
    vq_decode((const uint8_t*)data, w, h, lin.data());
    const Sprite s = texture_create(lin.data(), w, h, 1);
    if (s) g_memtex[s - 1].vq_bytes = bytes;
    return s;
}

bool texture_update(Sprite s, const uint16_t* texels, bool twiddled) {
    if (s < 1 || s > g_nsprites || g_paths[s - 1] || g_memtex[s - 1].vq_bytes) return false;
    MemTex& t = g_memtex[s - 1];
    if (twiddled && t.w == t.h) {                                    // back to linear for the dumps (no allocation)
        for (int y = 0; y < t.h; ++y)
            for (int x = 0; x < t.w; ++x) t.texels[(size_t)(y * t.w + x)] = texels[twiddle_index((uint32_t)x, (uint32_t)y)];
    } else {
        std::copy(texels, texels + t.w * t.h, t.texels.begin());      // same size: no allocation
    }
    ++t.version;
    g_tex_written[s - 1] = false;
    if (g_trace_draws) fprintf(g_trace, "L update:%d:v%d\n", s, t.version);
    return true;
}

bool texture_update_vq(Sprite s, const void* data, int bytes) {
    if (s < 1 || s > g_nsprites || g_paths[s - 1] || !g_memtex[s - 1].vq_bytes || bytes != g_memtex[s - 1].vq_bytes) return false;
    MemTex& t = g_memtex[s - 1];
    vq_decode((const uint8_t*)data, t.w, t.h, t.texels.data());      // same size: no allocation
    ++t.version;
    g_tex_written[s - 1] = false;
    if (g_trace_draws) fprintf(g_trace, "L update_vq:%d:v%d\n", s, t.version);
    return true;
}

void set_fog(bool on, uint32_t rgb, float start, float end) {
    g_fog_on = on; g_fog_rgb = rgb & 0xFFFFFFu; g_fog_start = start; g_fog_end = end;
}

Material material_create(Sprite tex, int flags) {
    if (g_nmats >= 256 || tex < 0 || tex > g_nsprites) return 0;
    g_mats[g_nmats] = Mat{ tex, flags };
    return ++g_nmats;
}

Vert3* vtx_begin(int max) {
    if (g_open >= 0 || max <= 0 || g_arena_used + max > ARENA_VERTS) { if (max > 0) ++g_drops; return nullptr; }
    g_open = g_arena_used;
    return g_arena + g_arena_used;
}

int vtx_room() { return g_open >= 0 ? 0 : ARENA_VERTS - g_arena_used; }

void vtx_end(Material m, int used) {
    if (g_open < 0) return;
    const int first = g_open;
    g_open = -1;
    if (used <= 0 || m < 1 || m > g_nmats) return;
    g_arena_used = first + used;
    ++g_batches; g_batch_verts += used;
    if (g_trace_draws) fprintf(g_trace, "M %d %d\n", m, used);
    if (g_dump) {
        fprintf(g_dump, "b %d %d\n", m, used);
        for (int i = first; i < first + used; ++i) {
            const Vert3& v = g_arena[i];
            fprintf(g_dump, "%c %.7g %.7g %.7g %.7g %.7g %08X\n", v.flags == VERT_EOL ? 'e' : 'v', v.x, v.y, v.z, v.u, v.v, (unsigned)v.argb);
        }
    }
}

void set_clear_color(uint32_t rgb) { g_clear = rgb & 0xFFFFFFu; }

void sprite_draw_uv(Sprite s, float x, float y, float w, float h, float a,
                    float u0, float v0, float u1, float v1, int filter, uint32_t argb, float z) {
    if (s < 1 || s > g_nsprites) return;
    ++g_sprites_frame;
    {   // the KOS queue mirror
        const int q = filter == 2 ? 1 : 0;
        const int c = ++g_q_n[s - 1][q];
        if (c > g_q_peak[s - 1][q]) g_q_peak[s - 1][q] = c;
        if (c > g_q_res[s - 1][q]) ++g_q_over;
    }
    const char* name = g_paths[s - 1] ? g_paths[s - 1] : "<mem>";
    if (g_trace_draws)
        fprintf(g_trace, "S %s %.9g %.9g %.9g %.9g %.9g %d %.9g %.9g %.9g %.9g %08X %.9g\n", name,
                x, y, w, h, a, filter == 2 ? 2 : 0, u0, v0, u1, v1, (unsigned)argb, z);
    if (g_dump)
        fprintf(g_dump, "s %d %.9g %.9g %.9g %.9g %.9g %d %.9g %.9g %.9g %.9g %08X %.9g\n", s,
                x, y, w, h, a, filter == 2 ? 2 : 0, u0, v0, u1, v1, (unsigned)argb, z);
}

void sprite_reserve(Sprite s, int nearest, int bilinear) {   // KOS: reserves the queues; here: the mirror's capacity
    if (s < 1 || s > g_nsprites) return;
    if (nearest > g_q_res[s - 1][0]) g_q_res[s - 1][0] = nearest;
    if (bilinear > g_q_res[s - 1][1]) g_q_res[s - 1][1] = bilinear;
}

void sprite_draw(Sprite s, float x, float y, float w, float h, float a, int filter) {
    sprite_draw_uv(s, x, y, w, h, a, 0, 0, 1, 1, filter);
}

void gfx_rect(float x, float y, float w, float h, uint32_t argb, float z) {
    ++g_rects_frame;
    if (g_trace_draws) fprintf(g_trace, "R OP %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
    if (g_dump) fprintf(g_dump, "r OP %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
}

void gfx_rect_tr(float x, float y, float w, float h, uint32_t argb, float z) {
    ++g_rects_frame;
    if (g_trace_draws) fprintf(g_trace, "R TR %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
    if (g_dump) fprintf(g_dump, "r TR %.9g %.9g %.9g %.9g %08X %.9g\n", x, y, w, h, (unsigned)argb, z);
}

bool input_down(Key k) { return (g_buttons >> k) & 1u; }   // bit order == Key enum
int  input_axis(Axis a) { return (a == AXIS_X || a == AXIS_Y) ? g_axis[a] : 0; }

float random01() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;   // 24-bit: exact in float and double
}

void dbg_print(const char* s) { fprintf(g_trace, "D %s\n", s); }
void audio_event(const char* name) { if (g_trace && g_audio_trace) fprintf(g_trace, "A %s\n", name); }
// Silent unless BOOT_LOG=1 (then stderr): shows the boot breadcrumb sequence the KOS build prints.
void boot_log(const char* s) { static const char* e = getenv("BOOT_LOG"); if (e && *e == '1') fprintf(stderr, "[boot] %s\n", s); }

// Slice 3D-6: a map change reads its map, its painted ground and new props from the romdisk. The C library's stdio (like
// KOS's fs_open) allocates a file handle and frees it at close: not the game's allocation (net 0) - the allocation
// counter is paused around the platform's file calls.
struct AllocPause { int saved; AllocPause() : saved(g_alloc_watch) { g_alloc_watch = 0; } ~AllocPause() { g_alloc_watch = saved; } };

int file_read(const char* path, char* buf, int cap) {
    AllocPause pause;
    char real_path[512];
    if (strncmp(path, "/rd/", 4) == 0) snprintf(real_path, sizeof real_path, "%s/%s", g_romdisk, path + 4);
    else snprintf(real_path, sizeof real_path, "%s", path);
    FILE* f = fopen(real_path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    if (n >= cap) return -1;
    buf[n] = '\0';
    return n;
}

bool file_exists(const char* path) {
    AllocPause pause;
    char real_path[512];
    if (strncmp(path, "/rd/", 4) == 0) snprintf(real_path, sizeof real_path, "%s/%s", g_romdisk, path + 4);
    else snprintf(real_path, sizeof real_path, "%s", path);
    FILE* f = fopen(real_path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

} // namespace eng
