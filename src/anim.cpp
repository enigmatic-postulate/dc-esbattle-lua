// anim.cpp - see anim.h
#include "anim.h"
#include <stdio.h>
#include <string.h>

namespace anim {

static_assert(sizeof(Header) == 64, "dma header layout");
static_assert(sizeof(Clip) == 64, "dma clip layout");

// The library lives here for the whole session (rig_medium.dma: 252 KiB).
const int ARENA_BYTES = 384 * 1024;
alignas(32) static uint8_t g_arena[ARENA_BYTES];
static int g_used = 0;

static bool fail(char* err, int errlen, const char* msg) {
    if (err && errlen > 0) snprintf(err, (size_t)errlen, "%s", msg);
    return false;
}

static bool range_ok(uint32_t off, uint32_t bytes, uint32_t size, uint32_t align) {
    return off % align == 0 && off <= size && bytes <= size - off;
}

static bool sane(const float* f, int n, float lim) {
    for (int i = 0; i < n; ++i)
        if (!(f[i] > -lim && f[i] < lim)) return false;      // also false for NaN
    return true;
}

static int popcount32(uint32_t v) { int n = 0; while (v) { v &= v - 1; ++n; } return n; }

bool parse(const void* data, int size_i, Library* out, char* err, int errlen) {
    memset(out, 0, sizeof *out);
    if (((uintptr_t)data & 3) != 0) return fail(err, errlen, "image not 4-byte aligned");
    if (size_i < (int)sizeof(Header)) return fail(err, errlen, "file too small");
    const uint32_t size = (uint32_t)size_i;
    const uint8_t* b = (const uint8_t*)data;
    const Header* h = (const Header*)b;
    if (memcmp(h->magic, "DMA1", 4) != 0) return fail(err, errlen, "bad magic");
    if (h->version != 1) return fail(err, errlen, "unsupported version");
    if (h->file_size != size) return fail(err, errlen, "file_size mismatch");
    if (h->num_bones == 0 || h->num_bones > dmf::MAX_BONES) return fail(err, errlen, "bad bone count");
    if (h->num_clips == 0) return fail(err, errlen, "no clips");
    if (!range_ok(h->off_names, 16u * h->num_bones, size, 4)) return fail(err, errlen, "bone names out of range");
    if (!range_ok(h->off_clips, 64u * h->num_clips, size, 4)) return fail(err, errlen, "clips out of range");
    const char* names = (const char*)(b + h->off_names);
    for (int i = 0; i < h->num_bones; ++i)
        if (!memchr(names + 16 * i, 0, 16)) return fail(err, errlen, "bone name not terminated");
    const Clip* clips = (const Clip*)(b + h->off_clips);
    for (int k = 0; k < h->num_clips; ++k) {
        const Clip& c = clips[k];
        if (!memchr(c.name, 0, sizeof c.name)) return fail(err, errlen, "clip name not terminated");
        if (c.num_frames == 0 || !(c.fps > 0.0f) || !(c.fps < 1000.0f)) return fail(err, errlen, "empty clip");
        if (c.num_tbones > MAX_TBONES || popcount32(c.t_mask) != c.num_tbones) return fail(err, errlen, "bad translated-bone mask");
        if (h->num_bones < 32 && (c.t_mask >> h->num_bones)) return fail(err, errlen, "translated bone out of range");
        if (c.stride != 12u * c.num_tbones + 8u * h->num_bones) return fail(err, errlen, "bad frame stride");
        if (!range_ok(c.off_data, c.stride * c.num_frames, size, 4)) return fail(err, errlen, "clip data out of range");
        if (!sane(c.bound, 4, 1.0e4f) || !(c.bound[3] >= 0.0f)) return fail(err, errlen, "bad clip bound");
        for (int f = 0; f < c.num_frames; ++f)
            if (!sane((const float*)(b + c.off_data + c.stride * (uint32_t)f), 3 * c.num_tbones, 1.0e4f))
                return fail(err, errlen, "bad clip translation");
    }
    out->base = b; out->h = h; out->names = names; out->clips = clips;
    out->num_bones = h->num_bones; out->num_clips = h->num_clips;
    return true;
}

bool load(const char* path, Library* out) {
    char msg[160];
    const int room = ARENA_BYTES - g_used;
    uint8_t* dst = g_arena + g_used;
    const int n = room > 1 ? eng::file_read(path, (char*)dst, room) : -1;
    if (n < 0) {
        snprintf(msg, sizeof msg, "anim: cannot read %s (missing, or larger than the %d free arena bytes)", path, room - 1);
        eng::dbg_print(msg);
        return false;
    }
    char err[96];
    if (!parse(dst, n, out, err, sizeof err)) {
        snprintf(msg, sizeof msg, "anim: %s rejected: %s", path, err);
        eng::dbg_print(msg);
        return false;
    }
    g_used += (n + 31) & ~31;
    snprintf(msg, sizeof msg, "anim: %s ok: %d clips, %d bones, %d bytes", path, out->num_clips, out->num_bones, n);
    eng::dbg_print(msg);
    return true;
}

int anim_library_bytes() { return g_used; }

const Library* shared_library() {
    static Library lib;
    static bool tried = false, ok = false;
    if (!tried) { tried = true; ok = load("/rd/anims/rig_medium.dma", &lib); }
    return ok ? &lib : nullptr;
}

int clip_find(const Library& L, const char* name) {
    for (int k = 0; k < L.num_clips; ++k)
        if (strncmp(L.clips[k].name, name, sizeof L.clips[k].name) == 0) return k;
    return -1;
}

bool clip_loops(const Library& L, int k) { return k >= 0 && k < L.num_clips && (L.clips[k].flags & CLIP_LOOP); }

// One pass: looping clips run num_frames / fps (the last frame wraps to the first); one-shots end
// on their last frame, (num_frames - 1) / fps. Integer ticks, rounded (exact for 30 fps).
int clip_ticks(const Library& L, int k) {
    if (k < 0 || k >= L.num_clips) return 0;
    const Clip& c = L.clips[k];
    const int span = (c.flags & CLIP_LOOP) ? c.num_frames : c.num_frames - 1;
    return (int)((float)span * 60.0f / c.fps + 0.5f);
}

int clip_event(const Library& L, int k, Event e) {
    if (k < 0 || k >= L.num_clips || e < 0 || e >= EV_COUNT) return -1;
    const uint16_t v = L.clips[k].ev[e];
    return v == EV_NONE ? -1 : (int)v;
}

bool bind(const Library& L, const dmf::Model& m, Binding* out) {
    memset(out->lib_of, NO_BONE, sizeof out->lib_of);
    memset(out->model_of, NO_BONE, sizeof out->model_of);
    out->matched = 0;
    if (!m.h || !m.bones) return false;
    for (int i = 0; i < m.h->num_bones; ++i)
        for (int j = 0; j < L.num_bones; ++j)
            if (strncmp(m.bones[i].name, L.names + 16 * j, 16) == 0) {
                out->lib_of[i] = (uint8_t)j;
                out->model_of[j] = (uint8_t)i;
                ++out->matched;
                break;
            }
    return out->matched > 0;
}

static inline r3d::Quat q16(const int16_t* s) {      // stored X, Y, Z, W
    const float k = 1.0f / 32767.0f;
    return r3d::quat_xyzw(s[0] * k, s[1] * k, s[2] * k, s[3] * k);
}

void sample(const Library& L, const Binding& b, const dmf::Model& m, int k, float t_sec, dmf::Pose* p) {
    if (k < 0 || k >= L.num_clips || !m.h) { dmf::pose_bind(m, p); return; }
    const Clip& c = L.clips[k];
    const int F = c.num_frames, nb = m.h->num_bones;
    float f = t_sec * c.fps;
    int i0, i1;
    if (c.flags & CLIP_LOOP) {
        f = shz_fmodf(f, (float)F);
        if (f < 0.0f) f += (float)F;
        i0 = (int)f;
        if (i0 >= F) i0 = F - 1;                     // fmodf rounding at the wrap
        i1 = i0 + 1 == F ? 0 : i0 + 1;
    } else {
        if (f < 0.0f) f = 0.0f;
        if (f > (float)(F - 1)) f = (float)(F - 1);
        i0 = (int)f;
        i1 = i0 + 1 < F ? i0 + 1 : i0;
    }
    const float w = f - (float)i0;
    const uint8_t* d0 = L.base + c.off_data + c.stride * (uint32_t)i0;
    const uint8_t* d1 = L.base + c.off_data + c.stride * (uint32_t)i1;
    const int16_t* q0 = (const int16_t*)(d0 + 12 * c.num_tbones);
    const int16_t* q1 = (const int16_t*)(d1 + 12 * c.num_tbones);
    for (int i = 0; i < nb; ++i) {
        const dmf::Bone& bn = m.bones[i];
        const int li = b.lib_of[i];
        p->t[i] = r3d::v3(bn.bind_t[0], bn.bind_t[1], bn.bind_t[2]);
        if (li == NO_BONE) p->q[i] = r3d::quat_xyzw(bn.bind_q[0], bn.bind_q[1], bn.bind_q[2], bn.bind_q[3]);
        else p->q[i] = r3d::quat_nlerp(q16(q0 + 4 * li), q16(q1 + 4 * li), w);
    }
    // translated bones, in ascending library bone order
    const float* t0 = (const float*)d0;
    const float* t1 = (const float*)d1;
    uint32_t mask = c.t_mask;
    for (int j = 0; mask; ++j) {
        const int li = __builtin_ctz(mask);
        mask &= mask - 1;
        const int mi = b.model_of[li];
        if (mi == NO_BONE || mi >= nb) continue;
        p->t[mi] = r3d::lerp(r3d::v3(t0[3 * j], t0[3 * j + 1], t0[3 * j + 2]), r3d::v3(t1[3 * j], t1[3 * j + 1], t1[3 * j + 2]), w);
    }
}

// ---------------------------------------------------------------------------- Animator
static float clip_seconds(const Library& L, int k) {
    if (k < 0 || k >= L.num_clips) return 0.0f;
    const Clip& c = L.clips[k];
    return (float)((c.flags & CLIP_LOOP) ? c.num_frames : c.num_frames - 1) / c.fps;
}

void animator_init(Animator& a, int clip, float t) {
    a.clip = (int16_t)clip; a.prev = -1; a.then = -1;
    a.t = t; a.prev_t = 0.0f;
    a.fade = 1.0f; a.fade_rate = 0.0f; a.then_fade = 0.15f;
    a.hold_at = -1.0f;
    a.done = false;
}

void hold_at(Animator& a, float t_sec) { a.hold_at = t_sec; }

void play(Animator& a, const Library& L, int clip, float fade_sec, int then, float start_sec) {
    (void)L;
    if (fade_sec > 0.0f && a.clip >= 0) {
        a.prev = a.clip; a.prev_t = a.t;
        a.fade = 0.0f; a.fade_rate = 1.0f / fade_sec;
    } else {
        a.prev = -1; a.fade = 1.0f;
    }
    a.clip = (int16_t)clip;
    a.t = start_sec;
    a.then = (int16_t)then;
    a.then_fade = fade_sec > 0.0f ? fade_sec : 0.15f;
    a.hold_at = -1.0f;
    a.done = false;
}

void tick(Animator& a, const Library& L, float dt) {
    if (a.prev >= 0) {
        a.prev_t += dt;
        a.fade += a.fade_rate * dt;
        if (a.fade >= 1.0f) { a.fade = 1.0f; a.prev = -1; }
    }
    if (a.clip < 0) return;
    a.t += dt;
    if (a.hold_at >= 0.0f && a.t >= a.hold_at) { a.t = a.hold_at; return; }
    if (!clip_loops(L, a.clip)) {
        const float end = clip_seconds(L, a.clip);
        if (a.t >= end) {
            if (a.then >= 0) {
                const int next = a.then;
                play(a, L, next, a.then_fade, -1, 0.0f);
                a.prev_t = end;                          // fade out of the held last frame
                a.done = true;
            } else {
                a.t = end;
                a.done = true;
            }
        }
    }
}

void pose(const Animator& a, const Library& L, const Binding& b, const dmf::Model& m, dmf::Pose* out) {
    sample(L, b, m, a.clip, a.t, out);
    if (a.prev >= 0 && a.fade < 1.0f) {
        static dmf::Pose from;
        sample(L, b, m, a.prev, a.prev_t, &from);
        dmf::pose_blend(&from, *out, m.h->num_bones, a.fade);   // from -> clip by fade
        *out = from;
    }
}

} // namespace anim
