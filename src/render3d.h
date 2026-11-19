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
    Mat34 screen;        // viewport * proj_view: world -> (X', Y', W) with screen = (X'/W, Y'/W), z = 1/W.
                         // Loaded into XMTRX, one FTRV + one FSRRA give a vertex's screen position.
    Vec3  eye;
    float focal;         // pixels
    float cx, cy;
    float near_w;        // minimum W kept (geometry closer than this is clipped)
    float tan_x, tan_y;  // half-FOV tangents, for sphere culling
    float cos_x, cos_y;  // 1 / sqrt(1 + tan^2): side-plane normal scale (sphere culling)
    float sec_x, sec_y;  // sqrt(1 + tan^2)
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
    bool hull_ok;                 // cel prototype: gfx_init wrote each vertex's welded normal (the normal averaged over
                                  // every vertex at the same position, so the outline hull has no cracks at hard edges)
                                  // into the vertex's 4 pad bytes (x, y, z as int8) - no extra RAM, same cache line
    const ModelGfx* hull;         // cel prototype: a decimated hull mesh (romdisk/models/hull/<name>.dmf, same skeleton,
                                  // tools/cel/hull_lab.py) drawn instead of the full model when cel().lod is on
};

// ---- Cel-shading feasibility prototype (docs/CEL_SHADING_STUDY.md). Everything is off by default: the game draws
// exactly as before unless `outline_px` or `toon` is set (host: --tune cel_outline_px=1.5 ...; KOS: make CEL=1).
//   outline: an inverted hull - the model again, every vertex pushed out along its welded normal by `outline_px`
//            pixels (at the model's depth), untextured in `outline_argb`, FRONT faces culled (MAT_CULL_FRONT), so
//            only the rim that sticks out behind the silhouette shows. Costs one more transform + strip copy per
//            vertex of every outlined model.
//   toon:    banded lighting - n.l snapped to 3 levels (shadow / mid / lit) before ambient + diffuse. Applied where
//            lighting is computed (per frame, or baked at load / per yaw bucket): no extra per-frame cost.
struct CelParams {
    float outline_px;        // outline width in pixels (0 = off)
    float chars;             // 1 = characters (and their weapons) get outlines, 2 = the hero only
    float props;             // 0 none, 1 = small static props (< PROP_MAX_VERTS vertices), 2 = every static prop
    float toon;              // 1 = banded lighting
    float band_lo, band_hi;  // n.l thresholds of the 3 bands
    float outline_rgb;       // the outline colour as 0xRRGGBB in a float (tunable from the command line); 0 = black
    float lod;               // 1 = draw a character's decimated hull mesh (ModelGfx::hull) when it has one
    float zpush;             // hull depth push in world units (z = 1/(W + zpush)): hides a coarse hull poking through
    float weapons;           // 1 = battle weapons get (full) hulls too
};
CelParams& cel();
const int CEL_PROP_MAX_VERTS = 600;
// Outlines for the draws that follow (the caller marks characters / props; cleared by stats_reset each frame).
void cel_hull_next(bool characters, bool props);
// Snaps n.l to the toon bands when cel().toon is on (identity otherwise).
float cel_ndl(float ndl);
bool gfx_init(ModelGfx* g, const dmf::Model& m, bool cull);

// Skinned or rigid model, lit per vertex every frame. skin = per-bone skin matrices
// (dmf::pose_skin) or nullptr for static meshes. tint multiplies the lit vertex colour
// (0xAARRGGBB, white = none). Returns false if culled, not loaded, or the vertex arena was full.
bool draw_model(const Camera& cam, const Light& light, const ModelGfx& g, const Mat34& world,
                const Mat34* skin, uint32_t tint = 0xFFFFFFFFu, const uint32_t* prelit = nullptr);
// prelit (optional): per-vertex colours baked with prelight_posed() - skips per-vertex lighting
// (about half the cost of a skinned vertex). Used for the enemies, which only turn in yaw.
// The same with the culling sphere given in world space (battle: clips such as the skeletons'
// death slide move the body out of the bind-pose sphere; the caller bounds the posed model).
bool draw_model_in(const Camera& cam, const Light& light, const ModelGfx& g, const Mat34& world, const Mat34* skin,
                   Vec3 bound_centre, float bound_radius, uint32_t tint = 0xFFFFFFFFu, const uint32_t* prelit = nullptr);

// Bakes lighting for a skinned model seen at `yaw`, with normals in the pose given by `skin`
// (e.g. the idle pose, closer to how limbs hang than the T-pose bind pose).
void prelight_posed(const dmf::Model& m, const Mat34* skin, float yaw, const Light& light, uint32_t* colors_out);
// The same for the vertices [v_begin, v_end) only (baking spread over several frames).
void prelight_posed_range(const dmf::Model& m, const Mat34* skin, float yaw, const Light& light, uint32_t* colors_out,
                          int v_begin, int v_end);

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

// ---- slice 3D-4 (battle) helpers
// World point -> screen (x, y) and z = 1/W; false if behind the near plane.
bool project(const Camera& cam, Vec3 p, float* sx, float* sy, float* z = nullptr);
// Screen-aligned billboards (particles, markers): one strip of 4 vertices each. z_bias > 0 pulls
// the quad toward the camera (glows drawn over the body they sit on). Returns the number drawn.
struct Billboard { Vec3 p; float w, h, rot; float u0, v0, u1, v1; uint32_t argb; float z_bias; };
int draw_billboards(const Camera& cam, eng::Material mat, const Billboard* b, int n);
// Decals on a horizontal plane at height c.y: centre, half size, rotation about +Y, uv rect, colour.
struct GroundQuad { Vec3 c; float half, rot; float u0, v0, u1, v1; uint32_t argb; };
int draw_ground_quads(const Camera& cam, eng::Material mat, const GroundQuad* q, int n);

// Debug/test switch: per-strip off-screen culling of static props (default on). Turning it off
// must not change the picture (tests/slice3d: pixel-identical frames), only the work done.
void set_strip_culling(bool on);
// Debug/test switch: a static prop crossing the near plane sends its in-front triangles as runs of its own strips
// (default on) or, off, each triangle as its own strip (the former path). Must not change the picture.
void set_near_strips(bool on);

// Per-frame statistics (reset by stats_reset; read by DIAG builds and the host harness).
struct Stats { int models, models_culled, statics, statics_culled, statics_near, strips_culled, tris, verts_xf, verts_out;
               int hull_models, hull_xf, hull_out; };   // cel prototype: outline hulls drawn, their transforms and written vertices
void stats_reset();
const Stats& stats();
void stats_add(int verts_out, int tris);   // geometry written by callers straight into the arena

} // namespace r3d
