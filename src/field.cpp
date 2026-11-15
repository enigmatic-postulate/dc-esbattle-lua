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
    return in_bounds(x, y) && tile_walkable(tile_at(x, y)) && !g_f.solid[y * g_f.w + x];
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
            Entity e;
            memset(&e, 0, sizeof e);
            char kind[16], sheet[16], d[16], enc[24], extra[16];
            int x, y, pdx = 0, pdy = 0, plen = 0;
            int got = sscanf(line + 7, "%15s %15s %15s %d %d %15s %23s %15s %d %d %d",
                             e.id, kind, sheet, &x, &y, d, enc, extra, &pdx, &pdy, &plen);
            if (got != 7 && got != 11) { report(line_no, "bad entity", line); return false; }
            if (strcmp(kind, "enemy") == 0) e.kind = EntityKind::Enemy;
            else if (strcmp(kind, "npc") == 0) e.kind = EntityKind::Npc;
            else { report(line_no, "entity kind must be enemy|npc", kind); return false; }
            if (!parse_dir(d, &e.home_dir)) { report(line_no, "bad direction", d); return false; }
            if (!encounter_by_id(enc)) { report(line_no, "unknown encounter", enc); return false; }
            if (got == 11) {
                if (strcmp(extra, "patrol") != 0 || plen < 1 || (pdx == 0) == (pdy == 0)) { report(line_no, "bad patrol", line); return false; }
                e.patrol = true; e.pdx = pdx; e.pdy = pdy; e.plen = plen;
            }
            e.sheet = sheet_by_name(sheet);
            snprintf(e.encounter, sizeof e.encounter, "%s", enc);
            e.home_x = x; e.home_y = y;
            if (!g_f.ents.push(e)) { report(line_no, "too many entities", ""); return false; }
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
    }
    field_reset();
    char msg[96];
    snprintf(msg, sizeof msg, "field_load: ok, %dx%d, %d props, %d entities", g_f.w, g_f.h, g_f.props.size(), g_f.ents.size());
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

static int entity_hit(const Box& b) {
    for (int i = 0; i < g_f.ents.size(); ++i)
        if (g_f.ents[i].alive && overlap(b, entity_box(g_f.ents[i]))) return i;
    return -1;
}

static void update_camera() {
    int px = g_f.hero.x / SUBPX, py = g_f.hero.y / SUBPX;
    int mw = g_f.w * TILE_PX, mh = g_f.h * TILE_PX;
    g_f.cam_x = (mw <= 640) ? (mw - 640) / 2 : clampv(px + TILE_PX / 2 - 320, 0, mw - 640);
    g_f.cam_y = (mh <= 480) ? (mh - 480) / 2 : clampv(py + TILE_PX / 2 - 240, 0, mh - 480);
}

void field_reset() {
    Hero& h = g_f.hero;
    h.x = g_f.spawn_x * TILE_PX * SUBPX; h.y = g_f.spawn_y * TILE_PX * SUBPX;
    h.dir = g_f.spawn_dir; h.moving = false; h.walk_sub = 0;
    h.vx = h.vy = 0; h.fx = h.fy = 0;
    for (Entity& e : g_f.ents) {
        walker_place(e.w, e.home_x, e.home_y, e.home_dir, NPC_STEP_TICKS);
        e.alive = true; e.ppos = 0; e.psign = 1; e.pwait = PATROL_WAIT;
    }
    g_f.contact = -1;
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

// Moves the hero along one axis by `d` (1/16 px), clamping at blocking tiles so the hero
// slides along walls. Returns false if the move would touch an entity (contact instead).
static bool hero_move_axis(int dx, int dy) {
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
    int e = entity_hit(b);
    if (e >= 0) { touch(e); return false; }
    h.walk_sub += (nx > h.x ? nx - h.x : h.x - nx) + (ny > h.y ? ny - h.y : h.y - ny);
    h.moving = h.moving || nx != h.x || ny != h.y;
    h.x = nx; h.y = ny;
    return true;
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
    h.fx += h.vx; h.fy += h.vy;                                  // remainders stay in [0, 256)
    const int sx = floordiv(h.fx, 256), sy = floordiv(h.fy, 256);
    h.fx -= sx * 256; h.fy -= sy * 256;
    if (sx && !hero_move_axis(sx, 0)) return;                    // x then y: sliding along walls
    if (sy) hero_move_axis(0, sy);
}

static void entity_update(int i) {
    Entity& e = g_f.ents[i];
    if (!e.alive || !e.patrol) return;
    const Box hb = hero_box_at(g_f.hero.x, g_f.hero.y);
    if (e.w.moving) {
        Walker next = e.w;                                  // look ahead: would this tick's move overlap the hero?
        walker_advance(next);
        int px, py;
        walker_pixel(next, &px, &py);
        if (overlap(entity_box_px(px, py), hb)) { touch(i); return; }   // the enemy walks into the hero
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
        if (j != i && o.alive && ((o.w.tx == tx && o.w.ty == ty) || (o.w.moving && o.w.nx == tx && o.w.ny == ty))) blocked = true;
    }
    if (blocked) { e.psign = -e.psign; e.pwait = PATROL_WAIT; return; }   // turn around
    walker_start(e.w, d);
    e.ppos += e.psign;
}

void field_update() {
    ++g_f.ticks;
    if (g_f.contact < 0) hero_update();
    for (int i = 0; i < g_f.ents.size() && g_f.contact < 0; ++i) entity_update(i);
    update_camera();
}

int field_take_contact() { int c = g_f.contact; g_f.contact = -1; return c; }

void field_defeat(int i) { if (i >= 0 && i < g_f.ents.size()) g_f.ents[i].alive = false; }

void field_entity_pixel(int i, int* px, int* py) {
    if (i < 0 || i >= g_f.ents.size()) { *px = *py = 0; return; }
    walker_pixel(g_f.ents[i].w, px, py);
}

} // namespace bt
