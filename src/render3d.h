// render3d.h - CPU transform + lighting for the PVR (the Dreamcast has no hardware T&L).
// Portable C++: the same code runs on the host harness, so every frame can be previewed
// (tools/render_frame.py) and regression-checked without hardware.
//
// Screen convention (matches engine.h): 640x480, y down, z = 1/w, larger z nearer.
// Winding: model triangles are counter-clockwise seen from their front (right-handed, outward
// normals); after projection that is counter-clockwise *on screen*, so MAT_CULL materials cull
// on-screen clockwise triangles (KOS PVR_CULLING_CW). The KOS default (PVR_CULLING_CCW) keeps
// the on-screen-clockwise sprite quads, which is how this convention was pinned down.
#pragma once
#include "dmf.h"
#include "engine.h"
#include "math3d.h"

namespace r3d {

struct Camera {
    Mat34 view;          // world -> view (camera looks down -Z)
    Mat34 proj_view;     // world -> (X, Y, W) with screen = (cx + X/W, cy + Y/W), z = 1/W
    Vec3  eye;
    float focal;         // pixels
    float cx, cy;
    float near_w;        // minimum W kept (geometry closer than this is clipped)
    float tan_x, tan_y;  // half-FOV tangents, for sphere culling
};

void camera_look(Camera* c, Vec3 eye, Vec3 target, float fov_y_deg);

struct Light {
    Vec3  dir;           // unit vector *towards* the light, world space
    float ambient, diffuse;
};
Light default_light();

// PVR materials for a loaded model: one per .dmf material, single-sided (culled when `cull`) and
// double-sided variants. Alpha materials go to the TR list.
struct ModelGfx {
    const dmf::Model* m;
    eng::Material single[dmf::MAX_MATERIALS], doubled[dmf::MAX_MATERIALS];
    const float* strip_spheres;   // static models: x, y, z, r per strip (model space), for culling
};
bool gfx_init(ModelGfx* g, const dmf::Model& m, bool cull);

// Skinned or rigid model, lit per vertex every frame. skin = per-bone skin matrices
// (dmf::pose_skin) or nullptr for static meshes. tint multiplies the lit vertex colour
// (0xAARRGGBB, white = none). Returns false if culled, not loaded, or the vertex arena was full.
bool draw_model(const Camera& cam, const Light& light, const ModelGfx& g, const Mat34& world,
                const Mat34* skin, uint32_t tint = 0xFFFFFFFFu, const uint32_t* prelit = nullptr);
// prelit (optional): per-vertex colours baked with prelight_posed() - skips per-vertex lighting
// (about half the cost of a skinned vertex). Used for the enemies, which only turn in yaw.

// Bakes lighting for a skinned model seen at `yaw`, with normals in the pose given by `skin`
// (e.g. the idle pose, closer to how limbs hang than the T-pose bind pose).
void prelight_posed(const dmf::Model& m, const Mat34* skin, float yaw, const Light& light, uint32_t* colors_out);

// Static props: lighting is baked once per placed instance (prelight writes num_verts colours),
// so a frame only transforms, projects and copies. Props crossing the near plane are clipped per
// triangle. Returns false if culled or the arena was full.
void prelight(const dmf::Model& m, const Mat34& world, const Light& light, uint32_t* colors_out);
bool draw_static(const Camera& cam, const ModelGfx& g, const Mat34& world, const uint32_t* colors);

// A convex polygon in *world* space (up to 8 corners, in order), clipped against the near plane
// and emitted as one strip. Returns the number of vertices written (0 if fully clipped).
struct PolyVert { Vec3 p; float u, v; uint32_t argb; };
int emit_poly(const Camera& cam, const PolyVert* in, int n, eng::Vert3* out);

// Soft blob shadow on the ground plane (y = 0), TR material, alpha at the centre.
void draw_blob_shadow(const Camera& cam, eng::Material tr_untextured, Vec3 centre, float radius, int alpha);

// Debug/test switch: per-strip off-screen culling of static props (default on). Turning it off
// must not change the picture (tests/slice3d: pixel-identical frames), only the work done.
void set_strip_culling(bool on);

// Per-frame statistics (reset by stats_reset; read by DIAG builds and the host harness).
struct Stats { int models, models_culled, statics, statics_culled, statics_near, strips_culled, tris, verts_xf, verts_out; };
void stats_reset();
const Stats& stats();

} // namespace r3d
