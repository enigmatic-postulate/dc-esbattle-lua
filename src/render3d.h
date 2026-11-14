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

// Model draw. skin = per-bone skin matrices (dmf::pose_skin) or nullptr for static meshes.
// tint multiplies the lit vertex colour (0xAARRGGBB, white = none). Returns false if culled,
// not loaded, or the vertex arena was full.
struct ModelMats { eng::Material single, doubled; };   // single-sided / double-sided batches
bool draw_model(const Camera& cam, const Light& light, const dmf::Model& m, const ModelMats& mats,
                const Mat34& world, const Mat34* skin, uint32_t tint = 0xFFFFFFFFu);

// A convex polygon in *world* space (up to 8 corners, in order), clipped against the near plane
// and emitted as one strip. Returns the number of vertices written (0 if fully clipped).
struct PolyVert { Vec3 p; float u, v; uint32_t argb; };
int emit_poly(const Camera& cam, const PolyVert* in, int n, eng::Vert3* out);

// Soft blob shadow on the ground plane (y = 0), TR material, alpha at the centre.
void draw_blob_shadow(const Camera& cam, eng::Material tr_untextured, Vec3 centre, float radius, int alpha);

// Per-frame statistics (reset by stats_reset; read by DIAG builds and the host harness).
struct Stats { int models, models_culled, tris, verts_xf, verts_out; };
void stats_reset();
const Stats& stats();

} // namespace r3d
