// field.cpp - see field.h
#include "field.h"
#include "encounters.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

namespace bt {

static FieldState  g_f;
static FieldState  s_empty;   // pristine state for field_load (file scope: no function-local static guard)
static const int   DX[4] = { 0, 0, -1, 1 };   // DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT
static const int   DY[4] = { 1, -1, 0, 0 };

const FieldState& field_state() { return g_f; }
const char* dir_name(Dir d) { static const char* N[4] = { "down", "up", "left", "right" }; return N[d & 3]; }
bool tile_walkable(uint8_t t) { return t == TILE_GRASS || t == TILE_FLOWERS || t == TILE_PATH; }

// ---------------------------------------------------------------------------- loading
static bool tile_from_char(char c, uint8_t* out) {
    switch (c) {
    case '.': *out = TILE_GRASS;   return true;
    case ',': *out = TILE_FLOWERS; return true;
    case '=': *out = TILE_PATH;    return true;
    case '#': *out = TILE_WALL;    return true;
    case 'T': *out = TILE_TREE;    return true;
    case '~': *out = TILE_WATER;   return true;
    default:  return false;
    }
}

static bool parse_dir(const char* s, Dir* d) {
    for (int i = 0; i < 4; ++i) if (strcmp(s, dir_name((Dir)i)) == 0) { *d = (Dir)i; return true; }
    return false;
}

static void report(int line, const char* msg, const char* detail) {
    char buf[160];
    snprintf(buf, sizeof buf, "map error line %d: %s %s", line, msg, detail ? detail : "");
    eng::dbg_print(buf);
}

// Splits s at spaces/tabs into at most max tokens of < 16 chars. Returns the token count, or
// -1 if there are more than max tokens or one is too long.
static int split_tokens(const char* s, char (*tok)[16], int max) {
    int n = 0;
    for (;;) {
        while (*s == ' ' || *s == '\t') ++s;
        if (!*s) return n;
        if (n == max) return -1;
        int len = 0;
        while (*s && *s != ' ' && *s != '\t') {
            if (len == 15) return -1;
            tok[n][len++] = *s++;
        }
        tok[n++][len] = '\0';
    }
}

// Copies a token (< 16 characters, split_tokens) into a field of at least 16 bytes.
static void copy_token(char* dst, const char* tok) { memcpy(dst, tok, 15); dst[15] = '\0'; }

// [-]digits[.digits] -> float, exactly for the values maps use (halves, whole degrees).
static bool parse_decimal(const char* s, float* out) {
    bool neg = false;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    long whole = 0, frac = 0, scale = 1;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; ++s, ++digits) { if (whole > 100000) return false; whole = whole * 10 + (*s - '0'); }
    if (*s == '.') {
        for (++s; *s >= '0' && *s <= '9'; ++s, ++digits) { if (scale >= 1000000) return false; frac = frac * 10 + (*s - '0'); scale *= 10; }
    }
    if (*s || digits == 0) return false;
    const float v = (float)whole + (float)frac / (float)scale;
    *out = neg ? -v : v;
    return true;
}

static bool parse_int(const char* s, int* out) {
    float f;
    if (!parse_decimal(s, &f) || f != (float)(int)f || strchr(s, '.')) return false;
    *out = (int)f;
    return true;
}

static uint8_t tile_at(int x, int y) { return g_f.tiles[y * g_f.w + x]; }
static bool in_bounds(int x, int y) { return x >= 0 && y >= 0 && x < g_f.w && y < g_f.h; }
bool cell_walkable(int x, int y) {
    return in_bounds(x, y) && tile_walkable(tile_at(x, y)) && !g_f.solid[y * g_f.w + x] && !g_f.closed[y * g_f.w + x];
}

// Splits off a trailing " #comment" and an " if <cond>" clause (the rest of the line) into cond (cap bytes).
static bool split_cond(char* body, char* cond, int cap) {
    char* hash = strstr(body, " #");
    if (hash) *hash = 0;
    cond[0] = 0;
    char* c = strstr(body, " if ");
    if (!c) return true;
    *c = 0;
    const char* v = c + 4;
    while (*v == ' ') ++v;
    if (!*v || (int)strlen(v) >= cap) return false;
    snprintf(cond, (size_t)cap, "%s", v);
    return true;
}

static void walker_place(Walker& w, int x, int y, Dir d, int step_len) {
    w.tx = w.nx = x; w.ty = w.ny = y; w.moving = false; w.step = 0; w.step_len = step_len; w.dir = d; w.phase = false;
}

bool field_load(const char* path) {
    static char buf[16384];
    if (eng::file_read(path, buf, sizeof buf) < 0) { report(0, "cannot read", path); return false; }

    g_f = s_empty;                                      // no ~15 KB stack temporary
    eng::boot_log("field_load: parsing");
    int rows_left = 0, row = 0, line_no = 0;
    bool have_map = false, have_spawn = false;

    for (char* line = buf; line && *line;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        ++line_no;
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';

        if (rows_left > 0) {                                   // map rows
            if ((int)n != g_f.w) { report(line_no, "row width mismatch", line); return false; }
            for (int x = 0; x < g_f.w; ++x)
                if (!tile_from_char(line[x], &g_f.tiles[row * g_f.w + x])) { report(line_no, "unknown tile char", line); return false; }
            ++row; --rows_left;
        } else if (n == 0 || line[0] == '#') {
            // blank / comment
        } else if (strncmp(line, "name ", 5) == 0) {
            snprintf(g_f.name, sizeof g_f.name, "%s", line + 5);
        } else if (strncmp(line, "map ", 4) == 0) {
            if (sscanf(line + 4, "%d %d", &g_f.w, &g_f.h) != 2 || g_f.w < 1 || g_f.h < 1 ||
                g_f.w > MAX_MAP_W || g_f.h > MAX_MAP_H) { report(line_no, "bad map size", line); return false; }
            rows_left = g_f.h; row = 0; have_map = true;
        } else if (strncmp(line, "spawn ", 6) == 0) {
            char d[16];
            if (sscanf(line + 6, "%d %d %15s", &g_f.spawn_x, &g_f.spawn_y, d) != 3 || !parse_dir(d, &g_f.spawn_dir)) {
                report(line_no, "bad spawn", line); return false;
            }
            have_spawn = true;
        } else if (strncmp(line, "prop ", 5) == 0) {
            // prop <model> <x> <y> <rot_deg> [solid <w> <h>]   (footprint centred on x, y)
            Prop p;
            memset(&p, 0, sizeof p);
            // Hand-parsed (no %f / strtod): tokens are whitespace separated, numbers are
            // [-]digits[.digits], which is all the map format uses.
            char tok[7][16];
            const int got = split_tokens(line + 5, tok, 7);
            int fw = 0, fh = 0;
            if (got >= 4) {
                snprintf(p.model, sizeof p.model, "%s", tok[0]);
                if (!parse_decimal(tok[1], &p.x) || !parse_decimal(tok[2], &p.y) || !parse_decimal(tok[3], &p.rot_deg)) {
                    report(line_no, "bad prop number", line); return false;
                }
            }
            if (got == 7 && (!parse_int(tok[5], &fw) || !parse_int(tok[6], &fh))) { report(line_no, "bad prop footprint", line); return false; }
            const char* extra = got == 7 ? tok[4] : "";
            if (got != 4 && got != 7) { report(line_no, "bad prop", line); return false; }
            if (!have_map || rows_left > 0) { report(line_no, "prop before the map block", line); return false; }
            if (got == 7) {
                if (strcmp(extra, "solid") != 0 || fw < 1 || fh < 1) { report(line_no, "bad prop footprint", line); return false; }
                const float x0 = p.x - fw * 0.5f, y0 = p.y - fh * 0.5f;
                if (x0 != (float)(int)x0 || y0 != (float)(int)y0) { report(line_no, "footprint not tile-aligned", line); return false; }
                p.solid = true; p.tx = (int)x0; p.ty = (int)y0; p.tw = fw; p.th = fh;
                if (!in_bounds(p.tx, p.ty) || !in_bounds(p.tx + fw - 1, p.ty + fh - 1)) { report(line_no, "footprint outside the map", line); return false; }
                for (int y = p.ty; y < p.ty + fh; ++y)
                    for (int x = p.tx; x < p.tx + fw; ++x) g_f.solid[y * g_f.w + x] = 1;
            }
            if (!g_f.props.push(p)) { report(line_no, "too many props", ""); return false; }
        } else if (strncmp(line, "entity ", 7) == 0) {
            // entity <id> npc   <look> <x> <y> <facing> talk <script> [patrol dx dy len] [if <cond...>]
            // entity <id> enemy <look> <x> <y> <facing> <encounter> [patrol dx dy len] [after a [b]] [win <flag>] [if <cond...>]
            Entity e;
            memset(&e, 0, sizeof e);
            char body[200];
            snprintf(body, sizeof body, "%s", line + 7);
            char* hash = strstr(body, " #");                   // trailing comment
            if (hash) *hash = 0;
            char* cond = strstr(body, " if ");                 // the rest of the line is a condition
            if (cond) {
                *cond = 0;
                const char* c = cond + 4;
                while (*c == ' ') ++c;
                if (!*c || strlen(c) >= sizeof e.cond) { report(line_no, "bad 'if' condition (1..63 characters)", line); return false; }
                snprintf(e.cond, sizeof e.cond, "%s", c);
            }
            char tok[16][16];
            const int nt = split_tokens(body, tok, 16);
            if (nt < 7) { report(line_no, "bad entity", line); return false; }
            copy_token(e.id, tok[0]);
            if (strcmp(tok[1], "enemy") == 0) e.kind = EntityKind::Enemy;
            else if (strcmp(tok[1], "npc") == 0) e.kind = EntityKind::Npc;
            else { report(line_no, "entity kind must be enemy|npc", tok[1]); return false; }
            bool look_ok;
            e.look = look_by_name(tok[2], &look_ok);
            if (!look_ok) { report(line_no, "unknown look (slime, guard, mage, smith, watch, warden, stone)", tok[2]); return false; }
            e.sheet = sheet_by_name(e.look == ELOOK_SLIME ? "slime" : "guard");
            int x, y;
            if (!parse_int(tok[3], &x) || !parse_int(tok[4], &y)) { report(line_no, "bad entity position", line); return false; }
            if (!parse_dir(tok[5], &e.home_dir)) { report(line_no, "bad direction", tok[5]); return false; }
            int k = 6;
            if (e.kind == EntityKind::Npc) {
                if (strcmp(tok[k], "talk") || k + 1 >= nt) { report(line_no, "an npc needs 'talk <script>' (slice 3D-5: touching NPCs no longer starts battles)", line); return false; }
                copy_token(e.talk, tok[k + 1]);
                k += 2;
            } else {
                if (!encounter_by_id(tok[k])) { report(line_no, "unknown encounter", tok[k]); return false; }
                copy_token(e.encounter, tok[k]);
                ++k;
            }
            while (k < nt) {
                if (!strcmp(tok[k], "patrol") && k + 3 < nt) {
                    int pdx, pdy, plen;
                    if (!parse_int(tok[k + 1], &pdx) || !parse_int(tok[k + 2], &pdy) || !parse_int(tok[k + 3], &plen) ||
                        plen < 1 || (pdx == 0) == (pdy == 0) || pdx < -1 || pdx > 1 || pdy < -1 || pdy > 1) { report(line_no, "bad patrol", line); return false; }
                    e.patrol = true; e.pdx = pdx; e.pdy = pdy; e.plen = plen;
                    k += 4;
                } else if (!strcmp(tok[k], "after") && k + 1 < nt && e.kind == EntityKind::Enemy) {
                    e.n_after = 0;
                    while (k + 1 < nt && e.n_after < 2 && strcmp(tok[k + 1], "patrol") && strcmp(tok[k + 1], "win"))
                        copy_token(e.after[e.n_after++], tok[++k]);
                    ++k;
                } else if (!strcmp(tok[k], "win") && k + 1 < nt && e.kind == EntityKind::Enemy) {
                    copy_token(e.win, tok[k + 1]);
                    k += 2;
                } else if (!strcmp(tok[k], "asleep") && e.kind == EntityKind::Enemy) {   // slice 3D-6
                    e.sleeper = true;
                    ++k;
                } else { report(line_no, "bad entity clause", tok[k]); return false; }
            }
            e.home_x = x; e.home_y = y;
            if (!g_f.ents.push(e)) { report(line_no, "too many entities", ""); return false; }
        } else if (strncmp(line, "trigger ", 8) == 0) {
            // slice 3D-6: trigger <id> <x> <y> <w> <h> <script> [once] [if <cond...>]
            Trigger t;
            memset(&t, 0, sizeof t);
            char body[200], tok[8][16];
            snprintf(body, sizeof body, "%s", line + 8);
            if (!split_cond(body, t.cond, sizeof t.cond)) { report(line_no, "bad 'if' condition (1..63 characters)", line); return false; }
            const int nt = split_tokens(body, tok, 8);
            if (nt < 6 || nt > 7 || (nt == 7 && strcmp(tok[6], "once"))) { report(line_no, "trigger <id> <x> <y> <w> <h> <script> [once] [if <cond>]", line); return false; }
            copy_token(t.id, tok[0]);
            if (!parse_int(tok[1], &t.x) || !parse_int(tok[2], &t.y) || !parse_int(tok[3], &t.w) || !parse_int(tok[4], &t.h) || t.w < 1 || t.h < 1) {
                report(line_no, "bad trigger rectangle", line); return false;
            }
            copy_token(t.script, tok[5]);
            t.once = nt == 7;
            if (!have_map || rows_left > 0 || !in_bounds(t.x, t.y) || !in_bounds(t.x + t.w - 1, t.y + t.h - 1)) { report(line_no, "trigger outside the map", line); return false; }
            if (!g_f.triggers.push(t)) { report(line_no, "too many triggers (8)", ""); return false; }
        } else if (strncmp(line, "exit ", 5) == 0) {
            // slice 3D-6: exit <id> <x> <y> <w> <h> <map> <tx> <ty> <dir> [if <cond...>]
            Exit x;
            memset(&x, 0, sizeof x);
            char body[200], tok[10][16];
            snprintf(body, sizeof body, "%s", line + 5);
            if (!split_cond(body, x.cond, sizeof x.cond)) { report(line_no, "bad 'if' condition (1..63 characters)", line); return false; }
            const int nt = split_tokens(body, tok, 10);
            if (nt != 9) { report(line_no, "exit <id> <x> <y> <w> <h> <map> <tx> <ty> <dir> [if <cond>]", line); return false; }
            copy_token(x.id, tok[0]);
            if (!parse_int(tok[1], &x.x) || !parse_int(tok[2], &x.y) || !parse_int(tok[3], &x.w) || !parse_int(tok[4], &x.h) || x.w < 1 || x.h < 1 ||
                !parse_int(tok[6], &x.tx) || !parse_int(tok[7], &x.ty) || !parse_dir(tok[8], &x.dir)) { report(line_no, "bad exit", line); return false; }
            copy_token(x.map, tok[5]);
            x.open = true;
            if (!have_map || rows_left > 0 || !in_bounds(x.x, x.y) || !in_bounds(x.x + x.w - 1, x.y + x.h - 1)) { report(line_no, "exit outside the map", line); return false; }
            if (!g_f.exits.push(x)) { report(line_no, "too many exits (4)", ""); return false; }
        } else if (strncmp(line, "shot ", 5) == 0) {
            // slice 3D-6: shot <id> <eye x y z> <look x y z> <fov>
            Shot sh;
            memset(&sh, 0, sizeof sh);
            char body[200], tok[10][16];
            snprintf(body, sizeof body, "%s", line + 5);
            char* hash = strstr(body, " #");
            if (hash) *hash = 0;
            const int nt = split_tokens(body, tok, 10);
            bool ok = nt == 8;
            for (int k = 0; k < 3 && ok; ++k) ok = parse_decimal(tok[1 + k], &sh.eye[k]) && parse_decimal(tok[4 + k], &sh.look[k]);
            ok = ok && parse_decimal(tok[7], &sh.fov) && sh.fov >= 10.0f && sh.fov <= 90.0f;
            if (!ok) { report(line_no, "shot <id> <eye x y z> <look x y z> <fov 10..90>", line); return false; }
            copy_token(sh.id, tok[0]);
            if (!g_f.shots.push(sh)) { report(line_no, "too many shots (12)", ""); return false; }
        } else if (strncmp(line, "glow ", 5) == 0) {
            // slice 3D-6: glow <x> <y> <radius> <rrggbb> [ring] [if <cond...>]
            Glow gl;
            memset(&gl, 0, sizeof gl);
            char body[200], tok[6][16];
            snprintf(body, sizeof body, "%s", line + 5);
            if (!split_cond(body, gl.cond, sizeof gl.cond)) { report(line_no, "bad 'if' condition (1..63 characters)", line); return false; }
            const int nt = split_tokens(body, tok, 6);
            bool ok = (nt == 4 || (nt == 5 && !strcmp(tok[4], "ring"))) && parse_decimal(tok[0], &gl.x) && parse_decimal(tok[1], &gl.z) &&
                      parse_decimal(tok[2], &gl.r) && gl.r > 0.0f && strlen(tok[3]) == 6;
            for (int k = 0; ok && k < 6; ++k) {
                const char c = tok[3][k];
                const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
                ok = d >= 0;
                gl.rgb = gl.rgb << 4 | (uint32_t)(d < 0 ? 0 : d);
            }
            if (!ok) { report(line_no, "glow <x> <y> <radius> <rrggbb> [ring] [if <cond>]", line); return false; }
            gl.ring = nt == 5;
            gl.on = gl.cond[0] == 0;
            if (!g_f.glows.push(gl)) { report(line_no, "too many glows (6)", ""); return false; }
        } else if (strncmp(line, "trees ", 6) == 0 || strncmp(line, "fences ", 7) == 0) {
            // slice 3D-6: the models the T tiles and # runs use on this map
            char tok[3][16];
            const bool trees = line[0] == 't';
            if (split_tokens(line + (trees ? 6 : 7), tok, 3) != 1) { report(line_no, "trees <model> | fences <model>", line); return false; }
            copy_token(trees ? g_f.tree_model : g_f.fence_model, tok[0]);
        } else if (strncmp(line, "light ", 6) == 0) {
            // slice 3D-6: light <preset> (romdisk/maps/lights.txt)
            char body[64], tok[2][16];
            snprintf(body, sizeof body, "%s", line + 6);
            char* hash = strstr(body, " #");
            if (hash) *hash = 0;
            if (split_tokens(body, tok, 2) != 1) { report(line_no, "light <preset>", line); return false; }
            copy_token(g_f.light, tok[0]);
        } else {
            report(line_no, "unknown directive", line); return false;
        }
        line = next;
    }
    if (!have_map || rows_left > 0) { report(line_no, "missing/short map block", ""); return false; }
    if (!have_spawn || !cell_walkable(g_f.spawn_x, g_f.spawn_y)) {
        report(line_no, "spawn missing or not walkable", ""); return false;
    }
    for (const Entity& e : g_f.ents) {
        if (!cell_walkable(e.home_x, e.home_y)) {
            report(0, "entity not on a walkable tile:", e.id); return false;
        }
        for (int k = 0; k < e.n_after; ++k) {
            bool found = false;
            for (const Entity& o : g_f.ents) found = found || (&o != &e && strcmp(o.id, e.after[k]) == 0);
            if (!found) { report(0, "'after' names an unknown entity:", e.after[k]); return false; }
        }
    }
    for (int i = 0; i < g_f.shots.size(); ++i)
        for (int j = 0; j < i; ++j)
            if (!strcmp(g_f.shots[i].id, g_f.shots[j].id)) { report(0, "duplicate shot:", g_f.shots[i].id); return false; }
    field_reset();
    char msg[128];
    snprintf(msg, sizeof msg, "field_load: ok, %dx%d, %d props, %d entities, %d triggers, %d exits, %d shots", g_f.w, g_f.h, g_f.props.size(),
             g_f.ents.size(), g_f.triggers.size(), g_f.exits.size(), g_f.shots.size());
    eng::boot_log(msg);
    return true;
}

// ---------------------------------------------------------------------------- simulation
static void walker_pixel(const Walker& w, int* px, int* py) {
    *px = w.tx * TILE_PX + (w.nx - w.tx) * TILE_PX * w.step / w.step_len;
    *py = w.ty * TILE_PX + (w.ny - w.ty) * TILE_PX * w.step / w.step_len;
}

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct Box { int x0, y0, x1, y1; };   // 1/16 px, half-open [x0,x1) x [y0,y1)

static Box hero_box_at(int x, int y) {
    return Box{ x + HERO_BOX_X0 * SUBPX, y + HERO_BOX_Y0 * SUBPX, x + HERO_BOX_X1 * SUBPX, y + HERO_BOX_Y1 * SUBPX };
}
static Box entity_box_px(int px, int py) {
    return Box{ (px + ENT_BOX_X0) * SUBPX, (py + ENT_BOX_Y0) * SUBPX, (px + ENT_BOX_X1) * SUBPX, (py + ENT_BOX_Y1) * SUBPX };
}
static Box entity_box(const Entity& e) { int px, py; walker_pixel(e.w, &px, &py); return entity_box_px(px, py); }
static bool overlap(const Box& a, const Box& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

// True if the box touches any blocking tile (outside the map counts as blocking).
static bool box_blocked(const Box& b) {
    const int T = TILE_PX * SUBPX;
    int c0 = floordiv(b.x0, T), c1 = floordiv(b.x1 - 1, T), r0 = floordiv(b.y0, T), r1 = floordiv(b.y1 - 1, T);
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c)
            if (!cell_walkable(c, r)) return true;
    return false;
}

static bool present(const Entity& e) { return e.alive && !e.hidden; }

static void update_camera() {
    int px = g_f.hero.x / SUBPX, py = g_f.hero.y / SUBPX;
    int mw = g_f.w * TILE_PX, mh = g_f.h * TILE_PX;
    g_f.cam_x = (mw <= 640) ? (mw - 640) / 2 : clampv(px + TILE_PX / 2 - 320, 0, mw - 640);
    g_f.cam_y = (mh <= 480) ? (mh - 480) / 2 : clampv(py + TILE_PX / 2 - 240, 0, mh - 480);
}

static Box hero_feet_box();
static bool rect_overlap_tiles(const Box& b, int x, int y, int w, int h);
// Triggers and exits fire on entering (an edge): a hero placed inside one (a spawn, an arrival, a retry) must
// first leave it. Scripted moves are cancelled.
static void reset_edges() {
    const Box b = hero_feet_box();
    for (Trigger& t : g_f.triggers) t.inside = rect_overlap_tiles(b, t.x, t.y, t.w, t.h);
    for (Exit& x : g_f.exits) x.inside = rect_overlap_tiles(b, x.x, x.y, x.w, x.h);
    g_f.trigger_hit = g_f.exit_hit = -1;
    g_f.hero_mv = false;
}

void field_reset() {
    Hero& h = g_f.hero;
    h.x = g_f.spawn_x * TILE_PX * SUBPX; h.y = g_f.spawn_y * TILE_PX * SUBPX;
    h.dir = g_f.spawn_dir; h.moving = false; h.walk_sub = 0;
    h.vx = h.vy = 0; h.fx = h.fy = 0;
    h.face_x = DX[h.dir] * 256; h.face_y = DY[h.dir] * 256;
    for (Entity& e : g_f.ents) {
        walker_place(e.w, e.home_x, e.home_y, e.home_dir, NPC_STEP_TICKS);
        e.alive = e.n_after == 0; e.dormant = e.n_after > 0; e.ppos = 0; e.psign = 1; e.pwait = PATROL_WAIT;
        e.facing_hero = false;
        e.asleep = e.sleeper; e.mv_active = false;
    }
    g_f.contact = -1;
    g_f.talk_target = -1;
    reset_edges();
    update_camera();
}

void field_restore(int tx, int ty, Dir dir, uint32_t defeated) {
    Hero& h = g_f.hero;
    h.x = tx * TILE_PX * SUBPX; h.y = ty * TILE_PX * SUBPX;
    h.dir = dir; h.moving = false; h.walk_sub = 0;
    h.vx = h.vy = 0; h.fx = h.fy = 0;
    h.face_x = DX[dir] * 256; h.face_y = DY[dir] * 256;
    for (int i = 0; i < g_f.ents.size(); ++i) {
        Entity& e = g_f.ents[i];
        walker_place(e.w, e.home_x, e.home_y, e.home_dir, NPC_STEP_TICKS);
        e.ppos = 0; e.psign = 1; e.pwait = PATROL_WAIT; e.facing_hero = false;
        e.asleep = e.sleeper; e.mv_active = false;
        e.alive = !((defeated >> i) & 1u) && e.n_after == 0;
        e.dormant = !((defeated >> i) & 1u) && e.n_after > 0;
    }
    for (Entity& e : g_f.ents) {                       // `after` entities whose targets are all beaten wake
        if (!e.dormant) continue;
        bool ready = true;
        for (int k = 0; k < e.n_after; ++k)
            for (int j = 0; j < g_f.ents.size(); ++j)
                if (!strcmp(g_f.ents[j].id, e.after[k]) && !((defeated >> j) & 1u)) ready = false;
        if (ready) { e.dormant = false; e.alive = true; }
    }
    g_f.contact = -1;
    g_f.talk_target = -1;
    reset_edges();
    update_camera();
}

// Advances a step in progress; returns true on the tick the walker arrives.
static bool walker_advance(Walker& w) {
    if (!w.moving) return false;
    if (++w.step < w.step_len) return false;
    w.tx = w.nx; w.ty = w.ny; w.moving = false; w.step = 0; w.phase = !w.phase;
    return true;
}

static void walker_start(Walker& w, Dir d) {
    w.dir = d; w.nx = w.tx + DX[d]; w.ny = w.ty + DY[d]; w.moving = true; w.step = 0;
}

static void touch(int entity_index) {
    if (g_f.contact < 0) g_f.contact = entity_index;
}

enum MoveResult { MOVE_OK, MOVE_BLOCKED, MOVE_CONTACT };

// Moves the hero along one axis by `d` (1/16 px), clamping at blocking tiles so the hero
// slides along walls. An enemy in the way is a contact (the battle starts); an NPC blocks like a
// wall (slice 3D-5: no contact, no encounter) - unless the hero already overlaps it (a checkpoint
// restore next to a walking NPC), so he can always step out.
static MoveResult hero_move_axis(int dx, int dy) {
    Hero& h = g_f.hero;
    int nx = h.x + dx, ny = h.y + dy;
    Box b = hero_box_at(nx, ny);
    if (box_blocked(b)) {
        const int T = TILE_PX * SUBPX;
        if (dx > 0) nx = floordiv(b.x1 - 1, T) * T - HERO_BOX_X1 * SUBPX;
        if (dx < 0) nx = (floordiv(b.x0, T) + 1) * T - HERO_BOX_X0 * SUBPX;
        if (dy > 0) ny = floordiv(b.y1 - 1, T) * T - HERO_BOX_Y1 * SUBPX;
        if (dy < 0) ny = (floordiv(b.y0, T) + 1) * T - HERO_BOX_Y0 * SUBPX;
        b = hero_box_at(nx, ny);
    }
    for (int i = 0; i < g_f.ents.size(); ++i) {
        const Entity& en = g_f.ents[i];
        if (!present(en) || !overlap(b, entity_box(en))) continue;
        if (en.kind == EntityKind::Enemy && !en.asleep) { touch(i); return MOVE_CONTACT; }
        if (!overlap(hero_box_at(h.x, h.y), entity_box(en))) return MOVE_BLOCKED;
    }
    h.walk_sub += (nx > h.x ? nx - h.x : h.x - nx) + (ny > h.y ? ny - h.y : h.y - ny);
    h.moving = h.moving || nx != h.x || ny != h.y;
    h.x = nx; h.y = ny;
    return MOVE_OK;
}

// Integer square root (floor), for the stick magnitude. Deterministic on every platform.
static int isqrt(int n) {
    if (n <= 0) return 0;
    int r = 0, bit = 1 << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
        bit >>= 2;
    }
    return r;
}

static void hero_update() {
    Hero& h = g_f.hero;
    h.moving = false;
    h.vx = h.vy = 0;
    int ax = input_stick(eng::AXIS_X), ay = input_stick(eng::AXIS_Y);
    if (ax * ax + ay * ay <= STICK_DEAD * STICK_DEAD) {          // stick at rest: d-pad = full tilt
        ax = ((input_held(eng::KEY_RIGHT) ? 1 : 0) - (input_held(eng::KEY_LEFT) ? 1 : 0)) * STICK_FULL;
        ay = ((input_held(eng::KEY_DOWN) ? 1 : 0) - (input_held(eng::KEY_UP) ? 1 : 0)) * STICK_FULL;
    }
    const int mag = isqrt(ax * ax + ay * ay);                   // 0 (no input) or > STICK_DEAD
    if (mag <= STICK_DEAD) { h.fx = h.fy = 0; return; }
    const int tilt = (mag < STICK_FULL ? mag : STICK_FULL) - STICK_DEAD;
    const int spd = HERO_SPEED_Q8 * tilt / (STICK_FULL - STICK_DEAD);
    h.vx = ax * spd / mag;
    h.vy = ay * spd / mag;
    // 4-way facing: keep the current one while the stick still points that way (no flicker
    // on diagonals), otherwise face horizontally on diagonals, else vertically.
    const int dx = (h.vx * 3 > spd) - (h.vx * 3 < -spd), dy = (h.vy * 3 > spd) - (h.vy * 3 < -spd);
    bool keep = (h.dir == DIR_LEFT && dx < 0) || (h.dir == DIR_RIGHT && dx > 0) ||
                (h.dir == DIR_UP && dy < 0) || (h.dir == DIR_DOWN && dy > 0);
    if (!keep && (dx || dy)) h.dir = dx ? (dx < 0 ? DIR_LEFT : DIR_RIGHT) : (dy < 0 ? DIR_UP : DIR_DOWN);
    if (h.vx || h.vy) { h.face_x = h.vx; h.face_y = h.vy; }    // the facing vector (talk: 60 degrees)
    h.fx += h.vx; h.fy += h.vy;                                  // remainders stay in [0, 256)
    const int sx = floordiv(h.fx, 256), sy = floordiv(h.fy, 256);
    h.fx -= sx * 256; h.fy -= sy * 256;
    if (sx && hero_move_axis(sx, 0) == MOVE_CONTACT) return;     // x then y: sliding along walls (and NPCs)
    if (sy) hero_move_axis(0, sy);
}

static int hero_feet_x() { return g_f.hero.x + (HERO_BOX_X0 + HERO_BOX_X1) / 2 * SUBPX; }
static int hero_feet_y() { return g_f.hero.y + (HERO_BOX_Y0 + HERO_BOX_Y1) / 2 * SUBPX; }

void field_hero_tile(int* tx, int* ty) {
    *tx = floordiv(hero_feet_x(), TILE_PX * SUBPX);
    *ty = floordiv(hero_feet_y(), TILE_PX * SUBPX);
}

Dir dir_toward(int dx, int dy) {
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax > ay) return dx < 0 ? DIR_LEFT : DIR_RIGHT;
    return dy < 0 ? DIR_UP : DIR_DOWN;
}

// Entity centre (its tile centre, following a step in progress), in 1/16 px.
static void entity_centre(const Entity& e, int* cx, int* cy) {
    int px, py;
    walker_pixel(e.w, &px, &py);
    *cx = (px + TILE_PX / 2) * SUBPX; *cy = (py + TILE_PX / 2) * SUBPX;
}

void field_entity_face_hero(int i) {
    if (i < 0 || i >= g_f.ents.size()) return;
    int cx, cy;
    entity_centre(g_f.ents[i], &cx, &cy);
    g_f.ents[i].w.dir = dir_toward(hero_feet_x() - cx, hero_feet_y() - cy);
}

void field_entity_face(int i, Dir d) { if (i >= 0 && i < g_f.ents.size()) g_f.ents[i].w.dir = d; }

void field_hero_face(int dx, int dy) {
    if (!dx && !dy) return;
    Hero& h = g_f.hero;
    h.face_x = dx; h.face_y = dy;
    h.dir = dir_toward(dx, dy);
}

void field_hero_face_entity(int i) {
    if (i < 0 || i >= g_f.ents.size()) return;
    int cx, cy;
    entity_centre(g_f.ents[i], &cx, &cy);
    field_hero_face(cx - hero_feet_x(), cy - hero_feet_y());
}

void field_set_hidden(int i, bool hidden) { if (i >= 0 && i < g_f.ents.size()) g_f.ents[i].hidden = hidden; }

static void entity_update(int i) {
    Entity& e = g_f.ents[i];
    if (!present(e) || e.asleep) return;
    const bool npc = e.kind == EntityKind::Npc;
    if (npc) {
        // GDD 9.1: an NPC faces the hero within 3 tiles, and holds its patrol meanwhile (which also
        // keeps it from walking out of talk range mid-approach)
        int htx, hty;
        field_hero_tile(&htx, &hty);
        const int ddx = htx - e.w.tx, ddy = hty - e.w.ty;
        const bool near = (ddx < 0 ? -ddx : ddx) <= NPC_NOTICE_TILES && (ddy < 0 ? -ddy : ddy) <= NPC_NOTICE_TILES;
        if (near && !e.w.moving) {
            e.facing_hero = true;
            field_entity_face_hero(i);
            return;
        }
        if (!near && e.facing_hero) {
            e.facing_hero = false;
            if (!e.patrol) e.w.dir = e.home_dir;
        }
    }
    if (!e.patrol) return;
    const Box hb = hero_box_at(g_f.hero.x, g_f.hero.y);
    if (e.w.moving) {
        Walker next = e.w;                                  // look ahead: would this tick's move overlap the hero?
        walker_advance(next);
        int px, py;
        walker_pixel(next, &px, &py);
        if (overlap(entity_box_px(px, py), hb)) {
            if (!npc) { touch(i); return; }                  // the enemy walks into the hero
            return;                                          // an NPC waits for the hero to step aside
        }
        if (walker_advance(e.w)) e.pwait = PATROL_WAIT;
        return;
    }
    if (e.pwait > 0) { --e.pwait; return; }
    if (e.ppos + e.psign < 0 || e.ppos + e.psign > e.plen) e.psign = -e.psign;
    int sx = e.pdx * e.psign, sy = e.pdy * e.psign;
    Dir d = sx > 0 ? DIR_RIGHT : sx < 0 ? DIR_LEFT : sy > 0 ? DIR_DOWN : DIR_UP;
    int tx = e.w.tx + sx, ty = e.w.ty + sy;
    e.w.dir = d;
    bool blocked = !cell_walkable(tx, ty);
    for (int j = 0; j < g_f.ents.size() && !blocked; ++j) {
        const Entity& o = g_f.ents[j];
        if (j != i && present(o) && ((o.w.tx == tx && o.w.ty == ty) || (o.w.moving && o.w.nx == tx && o.w.ny == ty))) blocked = true;
    }
    if (blocked) { e.psign = -e.psign; e.pwait = PATROL_WAIT; return; }   // turn around
    walker_start(e.w, d);
    e.ppos += e.psign;
}

// The NPC the talk prompt points at (docs/NEXT_SLICE_DIALOG.md 5.1): within 1.3 tiles and 60 degrees of
// the facing vector; the nearest wins, ties go to the lower entity index. Integer maths only.
static int talk_target_compute() {
    const Hero& h = g_f.hero;
    const long long fx = h.face_x, fy = h.face_y;
    const long long f2 = fx * fx + fy * fy;
    if (f2 == 0) return -1;
    const int hx = hero_feet_x(), hy = hero_feet_y();
    int best = -1;
    long long best_d2 = 0;
    for (int i = 0; i < g_f.ents.size(); ++i) {
        const Entity& e = g_f.ents[i];
        if (e.kind != EntityKind::Npc || !present(e) || e.dormant) continue;
        int cx, cy;
        entity_centre(e, &cx, &cy);
        const long long dx = cx - hx, dy = cy - hy;
        const long long d2 = dx * dx + dy * dy;
        if (d2 > TALK_RANGE2 || d2 == 0) continue;
        const long long dot = fx * dx + fy * dy;
        if (dot <= 0 || 4 * dot * dot < f2 * d2) continue;
        if (best < 0 || d2 < best_d2) { best = i; best_d2 = d2; }
    }
    return best;
}

// ---------------------------------------------------------------------------- triggers and exits (slice 3D-6)
static Box hero_feet_box() { return hero_box_at(g_f.hero.x, g_f.hero.y); }
static bool rect_overlap_tiles(const Box& b, int x, int y, int w, int h) {
    const int T = TILE_PX * SUBPX;
    return overlap(b, Box{ x * T, y * T, (x + w) * T, (y + h) * T });
}

// After the hero moved (the same tick order as NPC contact): the first trigger / open exit whose rectangle the
// feet box has just entered. Integer boxes: deterministic.
static void edges_update() {
    const Box b = hero_feet_box();
    for (int i = 0; i < g_f.triggers.size(); ++i) {
        Trigger& t = g_f.triggers[i];
        const bool in = rect_overlap_tiles(b, t.x, t.y, t.w, t.h);
        if (in && !t.inside && g_f.trigger_hit < 0 && g_f.contact < 0) g_f.trigger_hit = i;
        t.inside = in;
    }
    for (int i = 0; i < g_f.exits.size(); ++i) {
        Exit& x = g_f.exits[i];
        const bool in = rect_overlap_tiles(b, x.x, x.y, x.w, x.h);
        if (in && !x.inside && x.open && g_f.exit_hit < 0 && g_f.contact < 0 && g_f.trigger_hit < 0) g_f.exit_hit = i;
        x.inside = in;
    }
}

int field_take_trigger() { int t = g_f.trigger_hit; g_f.trigger_hit = -1; return t; }
int field_take_exit() { int x = g_f.exit_hit; g_f.exit_hit = -1; return x; }

void field_set_exit_open(int i, bool open) {
    if (i < 0 || i >= g_f.exits.size()) return;
    Exit& x = g_f.exits[i];
    x.open = open;
    for (int y = x.y; y < x.y + x.h; ++y)
        for (int xx = x.x; xx < x.x + x.w; ++xx) g_f.closed[y * g_f.w + xx] = open ? 0 : 1;
}

int field_shot_find(const char* id) {
    for (int i = 0; i < g_f.shots.size(); ++i) if (!strcmp(g_f.shots[i].id, id)) return i;
    return -1;
}
int field_entity_find(const char* id) {
    for (int i = 0; i < g_f.ents.size(); ++i) if (!strcmp(g_f.ents[i].id, id)) return i;
    return -1;
}
void field_set_asleep(int i, bool asleep) { if (i >= 0 && i < g_f.ents.size()) g_f.ents[i].asleep = asleep; }
void field_set_glow(int i, bool on) { if (i >= 0 && i < g_f.glows.size()) g_f.glows[i].on = on; }

// ---------------------------------------------------------------------------- cutscene movement (slice 3D-6)
const int MV_HERO_WALK = 17, MV_HERO_RUN = 26;      // 1/16 px per tick: one tile in ~30 / ~20 ticks
const int MV_ENT_WALK = 30, MV_ENT_RUN = 18;        // ticks per tile step

static long long isqrt64(long long n) {
    if (n <= 0) return 0;
    long long r = 0, bit = 1LL << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
        bit >>= 2;
    }
    return r;
}

void field_script_move(int actor, int tx, int ty, bool run) {
    if (actor == 0) {
        g_f.hero_mv = true;
        g_f.hero_mv_x = tx * TILE_PX * SUBPX; g_f.hero_mv_y = ty * TILE_PX * SUBPX;
        g_f.hero_mv_speed = run ? MV_HERO_RUN : MV_HERO_WALK;
        return;
    }
    const int i = actor - 1;
    if (i < 0 || i >= g_f.ents.size()) return;
    Entity& e = g_f.ents[i];
    e.mv_tx = tx; e.mv_ty = ty; e.mv_active = true;
    e.w.step_len = run ? MV_ENT_RUN : MV_ENT_WALK;
    e.facing_hero = false;
}

void field_script_move_rel(int actor, int dx, int dy, bool run) {
    if (actor == 0) {
        const Hero& h = g_f.hero;
        field_script_move(0, 0, 0, run);
        g_f.hero_mv_x = h.x + dx * TILE_PX * SUBPX; g_f.hero_mv_y = h.y + dy * TILE_PX * SUBPX;
        return;
    }
    const int i = actor - 1;
    if (i < 0 || i >= g_f.ents.size()) return;
    const Walker& w = g_f.ents[i].w;
    field_script_move(actor, (w.moving ? w.nx : w.tx) + dx, (w.moving ? w.ny : w.ty) + dy, run);
}

void field_script_place(int actor, int tx, int ty, Dir d) {
    if (actor == 0) {
        Hero& h = g_f.hero;
        h.x = tx * TILE_PX * SUBPX; h.y = ty * TILE_PX * SUBPX;
        h.dir = d; h.moving = false; h.vx = h.vy = 0; h.fx = h.fy = 0;
        h.face_x = DX[d] * 256; h.face_y = DY[d] * 256;
        g_f.hero_mv = false;
        reset_edges();
        update_camera();
        return;
    }
    const int i = actor - 1;
    if (i < 0 || i >= g_f.ents.size()) return;
    Entity& e = g_f.ents[i];
    walker_place(e.w, tx, ty, d, NPC_STEP_TICKS);
    e.mv_active = false; e.facing_hero = false;
}

bool field_script_moving() {
    if (g_f.hero_mv) return true;
    for (const Entity& e : g_f.ents) if (e.mv_active) return true;
    return false;
}

void field_script_tick() {
    Hero& h = g_f.hero;
    h.moving = false; h.vx = h.vy = 0;
    if (g_f.hero_mv) {
        const long long dx = g_f.hero_mv_x - h.x, dy = g_f.hero_mv_y - h.y;
        const long long len = isqrt64(dx * dx + dy * dy);
        const int sp = g_f.hero_mv_speed;
        int nx, ny;
        if (len <= sp) { nx = g_f.hero_mv_x; ny = g_f.hero_mv_y; g_f.hero_mv = false; }
        else { nx = h.x + (int)(dx * sp / len); ny = h.y + (int)(dy * sp / len); }
        if (len > 0) {
            // the facing (and the view's yaw) follow the motion: the same scale as the stick's velocity
            h.vx = (int)(dx * sp * 256 / (len > 0 ? len : 1)); h.vy = (int)(dy * sp * 256 / (len > 0 ? len : 1));
            h.face_x = h.vx; h.face_y = h.vy;
            h.dir = dir_toward((int)dx, (int)dy);
        }
        h.walk_sub += (nx > h.x ? nx - h.x : h.x - nx) + (ny > h.y ? ny - h.y : h.y - ny);
        h.moving = nx != h.x || ny != h.y;
        h.x = nx; h.y = ny;
        if (!g_f.hero_mv) { h.vx = h.vy = 0; }
    }
    for (Entity& e : g_f.ents) {
        if (!e.mv_active) continue;
        if (e.w.moving) { walker_advance(e.w); if (e.w.moving) continue; }
        if (e.w.tx != e.mv_tx) walker_start(e.w, e.mv_tx > e.w.tx ? DIR_RIGHT : DIR_LEFT);
        else if (e.w.ty != e.mv_ty) walker_start(e.w, e.mv_ty > e.w.ty ? DIR_DOWN : DIR_UP);
        else { e.mv_active = false; e.w.step_len = NPC_STEP_TICKS; }
    }
    update_camera();
}

void field_update() {
    ++g_f.ticks;
    if (g_f.contact < 0) hero_update();
    for (int i = 0; i < g_f.ents.size() && g_f.contact < 0; ++i) entity_update(i);
    g_f.talk_target = g_f.contact < 0 ? talk_target_compute() : -1;
    edges_update();
    update_camera();
}

int field_talk_target() { return g_f.talk_target; }

void field_debug_place_hero(int x, int y, int face_x, int face_y) {
    g_f.hero.x = x; g_f.hero.y = y; g_f.hero.face_x = face_x; g_f.hero.face_y = face_y;
}
int field_debug_talk_target() { return talk_target_compute(); }

int field_take_contact() { int c = g_f.contact; g_f.contact = -1; return c; }

void field_defeat(int i) {
    if (i < 0 || i >= g_f.ents.size()) return;
    g_f.ents[i].alive = false;
    // dormant entities wake once everything they wait for is defeated
    for (Entity& e : g_f.ents) {
        if (!e.dormant) continue;
        bool ready = true;
        for (int k = 0; k < e.n_after; ++k)
            for (const Entity& o : g_f.ents)
                if (!strcmp(o.id, e.after[k]) && (o.alive || o.dormant)) ready = false;
        if (ready) { e.dormant = false; e.alive = true; }
    }
}

void field_entity_pixel(int i, int* px, int* py) {
    if (i < 0 || i >= g_f.ents.size()) { *px = *py = 0; return; }
    walker_pixel(g_f.ents[i].w, px, py);
}



EntityLook look_by_name(const char* name, bool* ok) {
    static const char* const N[ELOOK_COUNT] = { "slime", "guard", "mage", "smith", "watch", "warden", "stone" };
    for (int i = 0; i < ELOOK_COUNT; ++i) if (!strcmp(name, N[i])) { *ok = true; return (EntityLook)i; }
    *ok = false;
    return ELOOK_SLIME;
}

}
