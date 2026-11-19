/* raster.c - a small PVR-like software rasterizer for previews and QC (host tools only).
 *
 * Input triangles are already in screen space, exactly like the PVR's TA vertices:
 *   x, y in pixels (y down), z = 1/w (larger = nearer), u, v, argb (vertex colour).
 * Per pixel: perspective-correct u, v and colour (interpolated as attribute * z, divided by z),
 * texture MODULATE by the vertex colour (or the vertex colour alone when untextured),
 * nearest or bilinear filtering with wrap, depth test GREATER-OR-EQUAL.
 *   mode 0: opaque (OP list)  - writes colour and depth
 *   mode 1: alpha blend (TR)  - src*a + dst*(1-a), depth test only
 *   mode 2: additive (TR)     - dst + src*a, depth test only
 * The caller sorts translucent triangles back to front (the PVR sorts them per pixel).
 * Build: cc -O2 -shared -fPIC raster.c -o raster.so   (tools/battle3d/raster.py does it)
 */
#include <math.h>
#include <stdint.h>

typedef struct { float x, y, z, u, v; uint32_t argb; } RV;

static inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static inline void tex_sample(const uint8_t* tex, int tw, int th, float u, float v, int bilinear, float out[4]) {
    if (!bilinear) {
        int x = (int)floorf(u * tw), y = (int)floorf(v * th);
        x %= tw; if (x < 0) x += tw;
        y %= th; if (y < 0) y += th;
        const uint8_t* p = tex + 4 * (y * tw + x);
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
        return;
    }
    float fx = u * tw - 0.5f, fy = v * th - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    for (int c = 0; c < 4; ++c) out[c] = 0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            int x = (x0 + i) % tw; if (x < 0) x += tw;
            int y = (y0 + j) % th; if (y < 0) y += th;
            float w = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
            const uint8_t* p = tex + 4 * (y * tw + x);
            for (int c = 0; c < 4; ++c) out[c] += w * p[c];
        }
}

/* Slice 3D-6: PVR table fog (linear in W = 1/z between start and end, towards rgb), applied to the triangles of
 * the next raster_tris calls when on (render_frame.py: the 3D batches of a fogged frame); mode 3 multiplies the
 * frame by the source colour (MAT_MULTIPLY). */
static int g_fog_on = 0;
static float g_fog_rgb[3], g_fog_start = 0, g_fog_end = 1;
void raster_fog(int on, float r, float g, float b, float start, float end) {
    g_fog_on = on; g_fog_rgb[0] = r; g_fog_rgb[1] = g; g_fog_rgb[2] = b; g_fog_start = start; g_fog_end = end > start ? end : start + 0.1f;
}

/* color: W*H*3 floats (0..255), depth: W*H floats (initialised to 0 = far) */
void raster_tris(const RV* v, int ntris, const uint8_t* tex, int tw, int th, int bilinear, int mode, int cull,
                 float* color, float* depth, int W, int H) {
    for (int t = 0; t < ntris; ++t) {
        const RV* a = &v[3 * t];
        const RV* b = &v[3 * t + 1];
        const RV* c = &v[3 * t + 2];
        float area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
        if (fabsf(area) < 1e-8f) continue;
        /* screen y is down: counter-clockwise on screen (front, render3d.h) has area < 0 here */
        if (cull == 1 && area > 0) continue;
        if (cull == 2 && area < 0) continue;           /* cull front faces (MAT_CULL_FRONT: the outline hull) */
        float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
        float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
        int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
        if (x0 > x1 || y0 > y1) continue;
        float ca[4], cb[4], cc[4];
        const RV* vs[3] = { a, b, c };
        float* cs[3] = { ca, cb, cc };
        for (int k = 0; k < 3; ++k) {
            uint32_t col = vs[k]->argb;
            cs[k][0] = (float)((col >> 16) & 255); cs[k][1] = (float)((col >> 8) & 255);
            cs[k][2] = (float)(col & 255); cs[k][3] = (float)(col >> 24);
        }
        float inv_area = 1.0f / area;
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                float sx = px + 0.5f, sy = py + 0.5f;
                float w0 = edge(b->x, b->y, c->x, c->y, sx, sy) * inv_area;
                float w1 = edge(c->x, c->y, a->x, a->y, sx, sy) * inv_area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;
                float z = w0 * a->z + w1 * b->z + w2 * c->z;
                int idx = py * W + px;
                if (z < depth[idx]) continue;
                /* perspective-correct attributes */
                float pz0 = w0 * a->z / z, pz1 = w1 * b->z / z, pz2 = w2 * c->z / z;
                float col[4];
                for (int k = 0; k < 4; ++k) col[k] = pz0 * ca[k] + pz1 * cb[k] + pz2 * cc[k];
                float src[4];
                if (tex) {
                    float u = pz0 * a->u + pz1 * b->u + pz2 * c->u;
                    float vv = pz0 * a->v + pz1 * b->v + pz2 * c->v;
                    float tx[4];
                    tex_sample(tex, tw, th, u, vv, bilinear, tx);
                    for (int k = 0; k < 3; ++k) src[k] = tx[k] * col[k] / 255.0f;
                    src[3] = tx[3] * col[3] / 255.0f;
                } else {
                    for (int k = 0; k < 4; ++k) src[k] = col[k];
                }
                if (g_fog_on && mode != 3) {
                    float f = (1.0f / (z > 1e-6f ? z : 1e-6f) - g_fog_start) / (g_fog_end - g_fog_start);
                    f = f < 0 ? 0 : (f > 1 ? 1 : f);
                    for (int k = 0; k < 3; ++k) src[k] = mode == 2 ? src[k] * (1 - f) : src[k] * (1 - f) + g_fog_rgb[k] * f;
                }
                float* d = color + 3 * idx;
                if (mode == 3) {
                    for (int k = 0; k < 3; ++k) d[k] = d[k] * src[k] / 255.0f;
                } else if (mode == 0) {
                    d[0] = src[0]; d[1] = src[1]; d[2] = src[2];
                    depth[idx] = z;
                } else if (mode == 1) {
                    float al = src[3] / 255.0f;
                    for (int k = 0; k < 3; ++k) d[k] = d[k] * (1 - al) + src[k] * al;
                } else {
                    float al = src[3] / 255.0f;
                    for (int k = 0; k < 3; ++k) { d[k] += src[k] * al; if (d[k] > 255) d[k] = 255; }
                }
            }
        }
    }
}

/* ID buffer: for each pixel, the index of the nearest triangle (depth GREATER-OR-EQUAL, no culling
 * unless cull = 1). ids: W*H ints (initialised to -1), depth: W*H floats (0 = far). Used for view
 * culling (which triangles can any camera see). */
void raster_ids(const RV* v, int ntris, int id_base, int cull, int* ids, float* depth, int W, int H) {
    for (int t = 0; t < ntris; ++t) {
        const RV* a = &v[3 * t];
        const RV* b = &v[3 * t + 1];
        const RV* c = &v[3 * t + 2];
        float area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
        if (fabsf(area) < 1e-8f) continue;
        if (cull && area > 0) continue;
        float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
        float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
        int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
        float inv_area = 1.0f / area;
        for (int py = y0; py <= y1; ++py)
            for (int px = x0; px <= x1; ++px) {
                float sx = px + 0.5f, sy = py + 0.5f;
                float w0 = edge(b->x, b->y, c->x, c->y, sx, sy) * inv_area;
                float w1 = edge(c->x, c->y, a->x, a->y, sx, sy) * inv_area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;
                float z = w0 * a->z + w1 * b->z + w2 * c->z;
                int idx = py * W + px;
                if (z < depth[idx]) continue;
                depth[idx] = z;
                ids[idx] = id_base + t;
            }
    }
}

/* Visibility without writes: vis[t] = 1 if any pixel of triangle t passes the depth test against
 * `depth` (which is left untouched). For translucent geometry, which never occludes. */
void raster_vis(const RV* v, int ntris, const float* depth, unsigned char* vis, int W, int H) {
    for (int t = 0; t < ntris; ++t) {
        const RV* a = &v[3 * t];
        const RV* b = &v[3 * t + 1];
        const RV* c = &v[3 * t + 2];
        float area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
        if (fabsf(area) < 1e-8f) continue;
        float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
        float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
        int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
        float inv_area = 1.0f / area;
        for (int py = y0; py <= y1 && !vis[t]; ++py)
            for (int px = x0; px <= x1; ++px) {
                float sx = px + 0.5f, sy = py + 0.5f;
                float w0 = edge(b->x, b->y, c->x, c->y, sx, sy) * inv_area;
                float w1 = edge(c->x, c->y, a->x, a->y, sx, sy) * inv_area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;
                float z = w0 * a->z + w1 * b->z + w2 * c->z;
                if (z >= depth[py * W + px]) { vis[t] = 1; break; }
            }
    }
}
