// dmf.cpp - see dmf.h
#include "dmf.h"
#include <stdio.h>
#include <string.h>

namespace dmf {

static_assert(sizeof(Header) == 96, "dmf header layout");
static_assert(sizeof(Vertex) == 32, "dmf vertex layout");
static_assert(sizeof(Group) == 8, "dmf group layout");
static_assert(sizeof(Batch) == 16, "dmf batch layout");
static_assert(sizeof(Bone) == 96, "dmf bone layout");
static_assert(sizeof(Anim) == 32, "dmf anim layout");
static_assert(sizeof(TexHeader) == 32, "dmf texture header layout");
static_assert(sizeof(Material) == 64, "dmf material layout");

// Model files live here for the whole session (load-time only; no per-frame allocation).
const int ARENA_BYTES = 1024 * 1024;     // village: 541 KiB (hero, 2 skeletons, props, 4 house variants) + room for the all-yaw house fallbacks
alignas(32) static uint8_t g_arena[ARENA_BYTES];
static int g_arena_used = 0;

int loaded_bytes() { return g_arena_used; }

static bool fail(char* err, int errlen, const char* msg) {
    if (err && errlen > 0) snprintf(err, (size_t)errlen, "%s", msg);
    return false;
}

static bool range_ok(uint32_t off, uint32_t bytes, uint32_t size, uint32_t align) {
    return off % align == 0 && off <= size && bytes <= size - off;
}

// Every float the runtime reads must be finite and of sane magnitude, so a corrupted file can
// never feed NaN/inf into the transform (garbage vertices to the PVR, UB in float->int casts).
static bool sane(const float* f, int n, float lim = 1.0e4f) {
    for (int i = 0; i < n; ++i)
        if (!(f[i] > -lim && f[i] < lim)) return false;      // also false for NaN
    return true;
}

bool parse(const void* data, int size_i, Model* out, char* err, int errlen) {
    memset(out, 0, sizeof *out);
    if (((uintptr_t)data & 31) != 0) return fail(err, errlen, "image not 32-byte aligned");
    if (size_i < (int)sizeof(Header)) return fail(err, errlen, "file too small");
    const uint32_t size = (uint32_t)size_i;
    const uint8_t* b = (const uint8_t*)data;
    const Header* h = (const Header*)b;
    if (memcmp(h->magic, "DMF1", 4) != 0) return fail(err, errlen, "bad magic");
    if (h->version != 1 && h->version != 2) return fail(err, errlen, "unsupported version");
    if (h->version == 1 && (h->num_materials || h->off_materials)) return fail(err, errlen, "v1 with materials");
    if (h->num_materials > MAX_MATERIALS) return fail(err, errlen, "too many materials");
    if (h->file_size != size || size % 32) return fail(err, errlen, "file_size mismatch");
    if (h->num_verts == 0 || h->num_groups == 0 || h->num_batches == 0) return fail(err, errlen, "empty model");
    if (h->num_bones > MAX_BONES) return fail(err, errlen, "too many bones");
    if (!range_ok(h->off_verts, 32u * h->num_verts, size, 4) || !range_ok(h->off_groups, 8u * h->num_groups, size, 4) ||
        !range_ok(h->off_strips, 2u * h->num_strip_words, size, 4) || !range_ok(h->off_batches, 16u * h->num_batches, size, 4))
        return fail(err, errlen, "section out of range");
    if (!sane(h->bmin, 3) || !sane(h->bmax, 3)) return fail(err, errlen, "bad bounds");
    out->base = b;
    out->h = h;
    out->verts = (const Vertex*)(b + h->off_verts);
    out->groups = (const Group*)(b + h->off_groups);
    out->strips = (const uint16_t*)(b + h->off_strips);
    out->batches = (const Batch*)(b + h->off_batches);

    // skeleton first (groups reference bones)
    if (h->num_bones) {
        if (!(h->flags & DMF_SKINNED)) return fail(err, errlen, "bones without SKINNED flag");
        if (!range_ok(h->off_bones, 96u * h->num_bones, size, 4)) return fail(err, errlen, "bones out of range");
        out->bones = (const Bone*)(b + h->off_bones);
        for (int i = 0; i < h->num_bones; ++i) {
            const Bone& bn = out->bones[i];
            if (bn.parent >= i || bn.parent < -1) return fail(err, errlen, "bone parents must precede children");
            if (!sane(bn.bind_t, 3) || !sane(bn.bind_q, 4, 2.0f) || !sane(bn.inv_bind, 12)) return fail(err, errlen, "bad bone transform");
        }
    } else if (h->flags & DMF_SKINNED) {
        return fail(err, errlen, "SKINNED flag without bones");
    }
    // skin groups tile the vertex array
    uint32_t expect = 0;
    for (int g = 0; g < h->num_groups; ++g) {
        const Group& gr = out->groups[g];
        if (gr.first != expect || gr.count == 0) return fail(err, errlen, "groups must tile the vertex array");
        if (h->num_bones) {
            if (gr.bone0 >= h->num_bones) return fail(err, errlen, "group bone0 out of range");
            if (gr.bone1 != NO_BONE && gr.bone1 >= h->num_bones) return fail(err, errlen, "group bone1 out of range");
        } else if (gr.bone0 != NO_BONE || gr.bone1 != NO_BONE) {
            return fail(err, errlen, "static mesh group references bones");
        }
        expect += gr.count;
    }
    if (expect != h->num_verts) return fail(err, errlen, "groups do not cover all vertices");
    out->uniform = true;
    out->uniform_argb = out->verts[0].argb;
    for (int i = 0; i < h->num_verts; ++i) {
        if (!sane(&out->verts[i].x, 5)) return fail(err, errlen, "bad vertex position/uv");
        if (out->verts[i].argb != out->uniform_argb) out->uniform = false;
    }
    // strips
    for (int k = 0; k < h->num_batches; ++k) {
        const Batch& bt = out->batches[k];
        if (bt.first_word > h->num_strip_words || bt.num_words > h->num_strip_words - bt.first_word)
            return fail(err, errlen, "batch out of range");
        uint32_t i = bt.first_word, end = bt.first_word + bt.num_words, strips = 0, tris = 0;
        while (i < end) {
            uint32_t len = out->strips[i];
            if (len < 3 || len > end - i - 1) return fail(err, errlen, "bad strip length");
            for (uint32_t j = 1; j <= len; ++j)
                if (out->strips[i + j] >= h->num_verts) return fail(err, errlen, "strip index out of range");
            ++strips; tris += len - 2; i += 1 + len;
        }
        if (strips != bt.num_strips || tris != bt.num_tris) return fail(err, errlen, "batch counts mismatch");
        if ((bt.flags >> 8) >= (h->num_materials ? h->num_materials : 1u)) return fail(err, errlen, "batch material out of range");
    }
    // animations
    if (h->num_anims) {
        if (!h->num_bones) return fail(err, errlen, "animations need bones");
        if (!range_ok(h->off_anims, 32u * h->num_anims, size, 4)) return fail(err, errlen, "anims out of range");
        out->anims = (const Anim*)(b + h->off_anims);
        for (int k = 0; k < h->num_anims; ++k) {
            const Anim& a = out->anims[k];
            uint32_t stride = 12u + 8u * h->num_bones;
            if (a.num_frames == 0 || !(a.fps > 0.0f) || !(a.fps < 1000.0f)) return fail(err, errlen, "empty animation");
            if (a.t_bone != NO_BONE && a.t_bone >= h->num_bones) return fail(err, errlen, "anim t_bone out of range");
            if (!range_ok(a.off_data, stride * a.num_frames, size, 4)) return fail(err, errlen, "anim data out of range");
            if (!(a.ground_speed >= 0.0f && a.ground_speed < 100.0f)) return fail(err, errlen, "bad ground speed");
            for (int f = 0; f < a.num_frames; ++f)
                if (!sane((const float*)(b + a.off_data + stride * (uint32_t)f), 3)) return fail(err, errlen, "bad anim translation");
        }
    }
    // texture
    if (h->flags & DMF_TEXTURED) {
        if (!h->off_tex || !range_ok(h->off_tex, 32, size, 32)) return fail(err, errlen, "texture header out of range");
        const TexHeader* t = (const TexHeader*)(b + h->off_tex);
        bool pow2 = t->w >= 8 && t->w <= 1024 && t->h >= 8 && t->h <= 1024 && !(t->w & (t->w - 1)) && !(t->h & (t->h - 1));
        if (!pow2 || t->byte_count != 2u * t->w * t->h || t->format < TEX_RGB565 || t->format > TEX_ARGB1555)
            return fail(err, errlen, "bad texture");
        if (!range_ok(h->off_tex + 32, t->byte_count, size, 32)) return fail(err, errlen, "texels out of range");
        out->tex = t;
        out->texels = (const uint16_t*)(b + h->off_tex + 32);
    } else if (h->off_tex) {
        return fail(err, errlen, "texture without TEXTURED flag");
    }
    // materials (v2). v1: one implicit material = the embedded texture, or untextured.
    if (h->num_materials) {
        if (!range_ok(h->off_materials, 64u * h->num_materials, size, 4)) return fail(err, errlen, "materials out of range");
        const Material* mt = (const Material*)(b + h->off_materials);
        for (int k = 0; k < h->num_materials; ++k) {
            if (!memchr(mt[k].tex, 0, sizeof mt[k].tex)) return fail(err, errlen, "material name not terminated");
            const char* t = mt[k].tex;
            if (t[0] == '@' && t[1] == 0 && !out->tex) return fail(err, errlen, "material uses a missing embedded texture");
            if (t[0] != 0 && !(t[0] == '@' && t[1] == 0) && t[0] != '/') return fail(err, errlen, "bad texture reference");
        }
        out->num_mats = h->num_materials;
    } else if (h->off_materials) {
        return fail(err, errlen, "material offset without materials");
    } else {
        out->num_mats = 1;
    }
    // bounding sphere (bind pose); animation stays close to it, callers add margin
    out->centre = r3d::v3((h->bmin[0] + h->bmax[0]) * 0.5f, (h->bmin[1] + h->bmax[1]) * 0.5f, (h->bmin[2] + h->bmax[2]) * 0.5f);
    r3d::Vec3 ext = r3d::v3(h->bmax[0] - h->bmin[0], h->bmax[1] - h->bmin[1], h->bmax[2] - h->bmin[2]);
    out->radius = 0.5f * sqrtf(r3d::dot(ext, ext));        // load time, conservative bound: exact sqrtf
    return true;
}

// Shared textures (v2 materials), cached by path for the whole session: the village kit's wood
// trim is referenced by doors, windows, crates, fences and the wagon but uploaded once.
struct SharedTex { const char* path; bool alpha; eng::Sprite spr; };
static SharedTex g_shared[24];
static int g_nshared = 0;

eng::Sprite shared_texture(const char* path, bool alpha) {
    for (int i = 0; i < g_nshared; ++i)
        if (g_shared[i].alpha == alpha && strcmp(g_shared[i].path, path) == 0) return g_shared[i].spr;
    eng::Sprite s = eng::sprite_load(path, alpha);       // path lives in the model arena (persistent)
    if (!s) {
        char msg[96];
        snprintf(msg, sizeof msg, "dmf: shared texture %s failed to load", path);
        eng::dbg_print(msg);
    }
    if (g_nshared < (int)(sizeof g_shared / sizeof g_shared[0])) g_shared[g_nshared++] = SharedTex{ path, alpha, s };
    return s;
}

static void resolve_materials(Model* m) {
    if (!m->h->num_materials) {                           // v1: the embedded texture or nothing
        m->mat_tex[0] = m->texture;
        m->mat_flags[0] = (m->tex && m->tex->filter) ? MAT_BILINEAR : 0;
        return;
    }
    const Material* mt = (const Material*)(m->base + m->h->off_materials);
    for (int k = 0; k < m->num_mats; ++k) {
        const char* t = mt[k].tex;
        m->mat_flags[k] = mt[k].flags;
        if (t[0] == 0) m->mat_tex[k] = 0;
        else if (t[0] == '@') m->mat_tex[k] = m->texture;
        else m->mat_tex[k] = shared_texture(t, (mt[k].flags & MAT_ALPHA) != 0);
    }
}

bool load(const char* path, Model* out) {
    char msg[160];
    int room = ARENA_BYTES - g_arena_used;
    uint8_t* dst = g_arena + g_arena_used;
    int n = eng::file_read(path, (char*)dst, room);     // NUL-terminates: needs one spare byte
    if (n < 0) {
        snprintf(msg, sizeof msg, "dmf: cannot read %s (missing, or larger than the %d free arena bytes)", path, room - 1);
        eng::dbg_print(msg);
        return false;
    }
    char err[96];
    if (!parse(dst, n, out, err, sizeof err)) {
        snprintf(msg, sizeof msg, "dmf: %s rejected: %s", path, err);
        eng::dbg_print(msg);
        return false;
    }
    g_arena_used += (n + 31) & ~31;
    if (out->tex) {
        out->texture = eng::texture_create(out->texels, out->tex->w, out->tex->h, out->tex->format);
        if (!out->texture) {
            snprintf(msg, sizeof msg, "dmf: %s texture upload failed", path);
            eng::dbg_print(msg);
        }
    }
    resolve_materials(out);
    snprintf(msg, sizeof msg, "dmf: %s ok: %u verts, %u bones, %u anims, %d bytes", path, out->h->num_verts,
             out->h->num_bones, out->h->num_anims, n);
    eng::dbg_print(msg);
    return true;
}

int anim_find(const Model& m, const char* name) {
    for (int k = 0; k < (m.h ? m.h->num_anims : 0); ++k)
        if (strncmp(m.anims[k].name, name, 16) == 0) return k;
    return -1;
}

float anim_duration(const Model& m, int k) {
    if (k < 0 || k >= m.h->num_anims) return 0.0f;
    return (float)m.anims[k].num_frames / m.anims[k].fps;
}

void pose_bind(const Model& m, Pose* p) {
    for (int i = 0; i < m.h->num_bones; ++i) {
        const Bone& b = m.bones[i];
        p->q[i] = r3d::quat_xyzw(b.bind_q[0], b.bind_q[1], b.bind_q[2], b.bind_q[3]);
        p->t[i] = r3d::v3(b.bind_t[0], b.bind_t[1], b.bind_t[2]);
    }
}

static inline r3d::Quat q16(const int16_t* s) {      // stored X, Y, Z, W
    const float k = 1.0f / 32767.0f;
    return r3d::quat_xyzw(s[0] * k, s[1] * k, s[2] * k, s[3] * k);
}

void pose_sample(const Model& m, int k, float t_sec, Pose* p) {
    if (k < 0 || k >= m.h->num_anims) { pose_bind(m, p); return; }
    const Anim& a = m.anims[k];
    const int F = a.num_frames, nb = m.h->num_bones;
    float f = t_sec * a.fps;
    int i0, i1;
    if (a.flags & ANIM_LOOP) {
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
    const uint32_t stride = 12u + 8u * (uint32_t)nb;
    const uint8_t* d0 = m.base + a.off_data + stride * (uint32_t)i0;
    const uint8_t* d1 = m.base + a.off_data + stride * (uint32_t)i1;
    const int16_t* q0 = (const int16_t*)(d0 + 12);
    const int16_t* q1 = (const int16_t*)(d1 + 12);
    for (int i = 0; i < nb; ++i) {
        p->q[i] = r3d::quat_nlerp(q16(q0 + 4 * i), q16(q1 + 4 * i), w);
        const Bone& b = m.bones[i];
        p->t[i] = r3d::v3(b.bind_t[0], b.bind_t[1], b.bind_t[2]);
    }
    if (a.t_bone != NO_BONE) {
        const float* t0 = (const float*)d0;
        const float* t1 = (const float*)d1;
        p->t[a.t_bone] = r3d::lerp(r3d::v3(t0[0], t0[1], t0[2]), r3d::v3(t1[0], t1[1], t1[2]), w);
    }
}

void pose_blend(Pose* a, const Pose& b, int nb, float w) {
    if (w <= 0.0f) return;
    for (int i = 0; i < nb; ++i) {
        a->q[i] = r3d::quat_nlerp(a->q[i], b.q[i], w);
        a->t[i] = r3d::lerp(a->t[i], b.t[i], w);
    }
}

// Per bone, all in XMTRX (SH4ZAM): world = parent * T(t) * R(q), then skin = world * inv_bind.
// The rotation is built from the quaternion inside the matrix bank; the .dmf inverse bind matrix
// is 12 floats, row-major - the Mat34 layout.
void pose_skin(const Model& m, const Pose& p, r3d::Mat34* skin) {
    r3d::Mat34 world[MAX_BONES];
    for (int i = 0; i < m.h->num_bones; ++i) {
        const Bone& b = m.bones[i];
        if (b.parent < 0) {
            shz_xmtrx_init_rotation_quat(p.q[i]);                    // XMTRX = R
            shz_xmtrx_set_translation(p.t[i].x, p.t[i].y, p.t[i].z); // XMTRX = T * R
        } else {
            r3d::xmtrx_load(world[b.parent]);                        // XMTRX = parent
            shz_xmtrx_translate(p.t[i].x, p.t[i].y, p.t[i].z);       // XMTRX = parent * T
            shz_xmtrx_rotate_quat(p.q[i]);                           // XMTRX = parent * T * R
        }
        r3d::xmtrx_store(&world[i]);
        r3d::Mat34 inv;
        memcpy(inv.m, b.inv_bind, sizeof inv.m);                     // file floats, row-major == Mat34
        r3d::xmtrx_apply(inv);                                       // XMTRX = world * inv_bind
        r3d::xmtrx_store(&skin[i]);
    }
}

} // namespace dmf
