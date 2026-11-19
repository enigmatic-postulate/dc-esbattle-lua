// lights.cpp - see lights.h
#include "lights.h"
#include "engine.h"
#include "render3d.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static LightPreset g_l[MAX_LIGHTS];
static int g_n = 0;

static LightPreset day_preset() {
    LightPreset p;
    memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "day");
    // render3d's default_light(), bit for bit: the look of the village since slice 3D-2
    const r3d::Light l = r3d::default_light();
    p.dir[0] = l.dir.x; p.dir[1] = l.dir.y; p.dir[2] = l.dir.z;
    p.ambient = l.ambient; p.diffuse = l.diffuse;
    p.colour = 0xFFFFFF; p.tint = 0xFFFFFF;
    p.clear_ground = true; p.clear = 0;
    p.fog = false;
    return p;
}

int lights_count() { return g_n; }
int light_find(const char* name) {
    for (int i = 0; i < g_n; ++i) if (!strcmp(g_l[i].name, name)) return i;
    return -1;
}
const LightPreset& light_get(int i) { return g_l[(i >= 0 && i < g_n) ? i : 0]; }

static bool parse_hex6(const char* s, uint32_t* out) {
    if (strlen(s) != 6) return false;
    uint32_t v = 0;
    for (int k = 0; k < 6; ++k) {
        const char c = s[k];
        const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (d < 0) return false;
        v = v << 4 | (uint32_t)d;
    }
    *out = v;
    return true;
}

// [-]digits[.digits] (the map files' number syntax: no strtod, exact for the values used)
static bool parse_num(const char* s, float* out) {
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    long whole = 0, frac = 0, scale = 1;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; ++s, ++digits) { if (whole > 100000) return false; whole = whole * 10 + (*s - '0'); }
    if (*s == '.') for (++s; *s >= '0' && *s <= '9'; ++s, ++digits) { if (scale >= 1000000) return false; frac = frac * 10 + (*s - '0'); scale *= 10; }
    if (*s || !digits) return false;
    const float v = (float)whole + (float)frac / (float)scale;
    *out = neg ? -v : v;
    return true;
}

static bool fail(int line, const char* what) {
    char m[120];
    snprintf(m, sizeof m, "lights: lights.txt line %d: %s", line, what);
    eng::dbg_print(m);
    return false;
}

bool lights_load() {
    g_n = 0;
    g_l[g_n++] = day_preset();
    static char buf[4096];
    if (eng::file_read("/rd/maps/lights.txt", buf, sizeof buf) < 0) return true;   // `day` only
    int line_no = 0;
    for (char* line = buf; line && *line;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        ++line_no;
        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char tok[24][16];
        int nt = 0;
        for (char* s = line; *s && nt < 24;) {
            while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
            if (!*s) break;
            int L = 0;
            while (*s && *s != ' ' && *s != '\t' && *s != '\r') { if (L < 15) tok[nt][L++] = *s; ++s; }
            tok[nt++][L] = '\0';
        }
        line = next;
        if (!nt) continue;
        if (strcmp(tok[0], "light") || nt < 2) return fail(line_no, "expected: light <name> ...");
        LightPreset p = day_preset();
        if (strlen(tok[1]) > 11) return fail(line_no, "a preset name has 11 characters at most");
        memcpy(p.name, tok[1], strlen(tok[1]) + 1);
        int k = 2;
        while (k < nt) {
            const char* w = tok[k];
            if (!strcmp(w, "dir") && k + 3 < nt) {
                float v[3];
                for (int a = 0; a < 3; ++a) if (!parse_num(tok[k + 1 + a], &v[a])) return fail(line_no, "bad dir");
                const float L2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
                if (L2 < 1e-6f) return fail(line_no, "dir is zero");
                const float inv = 1.0f / shz_sqrtf(L2);
                for (int a = 0; a < 3; ++a) p.dir[a] = v[a] * inv;
                k += 4;
            } else if ((!strcmp(w, "ambient") || !strcmp(w, "diffuse")) && k + 1 < nt) {
                float v;
                if (!parse_num(tok[k + 1], &v) || v < 0.0f || v > 2.0f) return fail(line_no, "bad ambient / diffuse (0..2)");
                if (w[0] == 'a') p.ambient = v; else p.diffuse = v;
                k += 2;
            } else if ((!strcmp(w, "colour") || !strcmp(w, "tint")) && k + 1 < nt) {
                if (!parse_hex6(tok[k + 1], w[0] == 'c' ? &p.colour : &p.tint)) return fail(line_no, "bad colour (rrggbb)");
                k += 2;
            } else if (!strcmp(w, "clear") && k + 1 < nt) {
                p.clear_ground = !strcmp(tok[k + 1], "ground");
                if (!p.clear_ground && !parse_hex6(tok[k + 1], &p.clear)) return fail(line_no, "bad clear (rrggbb or ground)");
                k += 2;
            } else if (!strcmp(w, "fog") && k + 3 < nt) {
                if (!parse_hex6(tok[k + 1], &p.fog_rgb) || !parse_num(tok[k + 2], &p.fog_start) || !parse_num(tok[k + 3], &p.fog_end) ||
                    p.fog_start < 0.0f || p.fog_end <= p.fog_start) return fail(line_no, "fog <rrggbb> <start> <end> (0 <= start < end)");
                p.fog = true;
                k += 4;
            } else {
                return fail(line_no, "unknown keyword (dir ambient diffuse colour tint clear fog)");
            }
        }
        const int at = light_find(p.name);
        if (at >= 0) g_l[at] = p;                        // `day` may be redefined
        else if (g_n >= MAX_LIGHTS) return fail(line_no, "too many presets (8)");
        else g_l[g_n++] = p;
    }
    char m[96];
    snprintf(m, sizeof m, "lights: %d presets", g_n);
    eng::boot_log(m);                                  // silent in host traces (the goldens' D lines are unchanged)
    return true;
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const float ca = (float)((a >> sh) & 255u), cb = (float)((b >> sh) & 255u);
        int v = (int)(ca + (cb - ca) * t + 0.5f);
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        out |= (uint32_t)v << sh;
    }
    return out;
}

void light_mix(const LightPreset& a, const LightPreset& b, float t, LightPreset* o) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    if (t >= 1.0f) { *o = b; return; }
    if (t <= 0.0f) { *o = a; return; }
    *o = a;
    float d[3], L2 = 0.0f;
    for (int k = 0; k < 3; ++k) { d[k] = a.dir[k] + (b.dir[k] - a.dir[k]) * t; L2 += d[k] * d[k]; }
    const float inv = L2 > 1e-6f ? 1.0f / shz_sqrtf(L2) : 1.0f;
    for (int k = 0; k < 3; ++k) o->dir[k] = d[k] * inv;
    o->ambient = a.ambient + (b.ambient - a.ambient) * t;
    o->diffuse = a.diffuse + (b.diffuse - a.diffuse) * t;
    o->colour = mix_rgb(a.colour, b.colour, t);
    o->tint = mix_rgb(a.tint, b.tint, t);
    o->clear_ground = a.clear_ground && b.clear_ground;
    o->clear = mix_rgb(a.clear, b.clear, t);
    o->fog = a.fog || b.fog;
    if (o->fog) {
        const uint32_t ca = a.fog ? a.fog_rgb : b.fog_rgb, cb = b.fog ? b.fog_rgb : a.fog_rgb;
        const float sa = a.fog ? a.fog_start : 60.0f, ea = a.fog ? a.fog_end : 90.0f;
        const float sb = b.fog ? b.fog_start : 60.0f, eb = b.fog ? b.fog_end : 90.0f;
        o->fog_rgb = mix_rgb(ca, cb, t);
        o->fog_start = sa + (sb - sa) * t;
        o->fog_end = ea + (eb - ea) * t;
    }
}

uint32_t rgb_mul(uint32_t argb, uint32_t rgb) {
    if ((rgb & 0xFFFFFFu) == 0xFFFFFFu) return argb;
    const uint32_t r = (((argb >> 16) & 255u) * ((rgb >> 16) & 255u) + 127u) / 255u;
    const uint32_t g = (((argb >> 8) & 255u) * ((rgb >> 8) & 255u) + 127u) / 255u;
    const uint32_t b = ((argb & 255u) * (rgb & 255u) + 127u) / 255u;
    return (argb & 0xFF000000u) | r << 16 | g << 8 | b;
}

}
