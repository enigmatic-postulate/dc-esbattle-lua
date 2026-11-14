// dmf.h - Dreamcast Mesh Format (.dmf) v1 runtime: loader/validator, animation sampling, skinning
// matrices. The format is specified in tools/dmf/dmf.py (writer + reference implementation);
// tools/dmf/test_dmf.py cross-checks this code against it.
//
// Loading = one file read into a static 32-byte-aligned arena + validation of every offset,
// count and index; the structs below then point straight into the file image (no parsing,
// no heap). A file that fails validation is rejected with a message (dbg_print), never drawn.
#pragma once
#include <stdint.h>
#include "engine.h"
#include "math3d.h"

namespace dmf {

const int MAX_BONES = 32;
const uint8_t NO_BONE = 0xFF;
enum { DMF_SKINNED = 1, DMF_TEXTURED = 2 };
enum { BATCH_DOUBLE_SIDED = 1, BATCH_TRANSLUCENT = 2 };
enum { ANIM_LOOP = 1 };
enum { TEX_RGB565 = 1, TEX_ARGB4444 = 2, TEX_ARGB1555 = 3 };

struct Header {
    char     magic[4];
    uint16_t version, flags;
    uint32_t file_size;
    uint16_t num_verts, num_groups, num_bones, num_anims, num_batches, reserved0;
    uint32_t num_strip_words;
    float    bmin[3], bmax[3];
    uint32_t off_verts, off_groups, off_strips, off_batches, off_bones, off_anims, off_tex;
    uint8_t  reserved[16];
};
struct Vertex { float x, y, z, u, v; int8_t nx, ny, nz; uint8_t w0; uint32_t argb; uint8_t pad[4]; };
struct Group  { uint16_t first, count; uint8_t bone0, bone1; uint16_t pad; };
struct Batch  { uint32_t first_word, num_words; uint16_t num_strips, flags; uint32_t num_tris; };
struct Bone   { char name[16]; int16_t parent; uint16_t flags; float bind_t[3]; float bind_q[4]; float inv_bind[12]; };
struct Anim   { char name[16]; uint16_t num_frames; uint8_t flags, t_bone; float fps, ground_speed; uint32_t off_data; };
struct TexHeader { uint16_t w, h, format, filter; uint32_t byte_count; uint8_t pad[20]; };

struct Model {
    const uint8_t*   base;
    const Header*    h;
    const Vertex*    verts;
    const Group*     groups;
    const uint16_t*  strips;
    const Batch*     batches;
    const Bone*      bones;      // nullptr for static meshes
    const Anim*      anims;
    const TexHeader* tex;        // nullptr if untextured
    const uint16_t*  texels;
    eng::Sprite      texture;    // uploaded texture (0 if none)
    float            radius;     // bounding-sphere radius around the bounds centre (bind pose)
    r3d::Vec3        centre;
    bool             uniform;    // every vertex has the same base colour (uniform_argb)
    uint32_t         uniform_argb;
};

// Validates an in-memory image (used by load() and by the tests). err receives a reason on failure.
bool parse(const void* data, int size, Model* out, char* err, int errlen);
// Reads /rd/... into the model arena, validates, uploads the texture. false (and a log line) on error.
bool load(const char* path, Model* out);
int  loaded_bytes();                       // arena bytes in use (DIAG / tests)

int   anim_find(const Model& m, const char* name);   // -1 if absent
float anim_duration(const Model& m, int anim);       // seconds for one pass

struct Pose { r3d::Quat q[MAX_BONES]; r3d::Vec3 t[MAX_BONES]; };
void pose_bind(const Model& m, Pose* out);
void pose_sample(const Model& m, int anim, float t_sec, Pose* out);
void pose_blend(Pose* a, const Pose& b, int num_bones, float w);    // a = mix(a, b, w)
// Skin matrices per bone: world(b) * inv_bind(b), in model space.
void pose_skin(const Model& m, const Pose& p, r3d::Mat34* skin_out);

} // namespace dmf
