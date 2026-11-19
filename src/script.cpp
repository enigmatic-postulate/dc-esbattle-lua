// script.cpp - see script.h. The compiler runs once at boot (every map's scripts); the VM per tick.
#include "script.h"
#include "anim.h"
#include "field.h"
#include "lights.h"
#include "progress.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

namespace bt {

// ---------------------------------------------------------------------------- compiled pools (static)
// Per map (slice 3D-6): its entities' talk scripts, win flags and `if` conditions, its triggers' scripts and
// conditions, its exits' conditions, and which entity each actor slot is on it.
struct MapLinks {
    int16_t  ent_talk[MAX_ENTITIES], ent_win[MAX_ENTITIES];    // talk script, win flag (-1: none)
    uint16_t ent_cond[MAX_ENTITIES];                           // `if` condition (length 0: always)
    uint8_t  ent_cond_len[MAX_ENTITIES];
    int16_t  trig_script[MAX_TRIGGERS];
    uint16_t trig_cond[MAX_TRIGGERS];
    uint8_t  trig_cond_len[MAX_TRIGGERS];
    uint16_t exit_cond[MAX_EXITS];
    uint8_t  exit_cond_len[MAX_EXITS];
    uint16_t glow_cond[MAX_GLOWS];
    uint8_t  glow_cond_len[MAX_GLOWS];
    int8_t   bind[SCR_MAX_ACTORS];                             // actor slot -> entity index (-1: not on this map)
};
struct Pools {
    ScrOp     ops[SCR_MAX_OPS];
    char      str[SCR_STR_BYTES];
    uint8_t   cond[SCR_COND_BYTES];
    ScriptDef scripts[SCR_MAX_SCRIPTS];
    Speaker   speakers[SCR_MAX_SPEAKERS];
    Marker    markers[SCR_MAX_MARKERS];
    uint16_t  objectives[SCR_MAX_OBJECTIVES];   // string offsets; [0] = none
    char      flags[MAX_FLAGS][16];
    char      maps[MAX_MAPS][16];               // maps.txt (the index is the map id)
    char      actors[SCR_MAX_ACTORS][16];       // actor slots: 0 = rowan
    char      shots[MAX_MAPS][MAX_SHOTS][16];   // each map's shot names (CAM ops; the dump)
    MapLinks  links[MAX_MAPS];
    int nops, nstr, ncond, nscripts, nspeakers, nmarkers, nobjectives, nflags, nmaps, nactors;
};
static Pools g_pool;
static bool  g_loaded = false;
static char  g_err[320] = "";
static int   g_map = 0;                         // the loaded map (script_set_map)

// ---------------------------------------------------------------------------- compile-time scratch
// Only used while compiling (load time); reported separately from the pools.
const int SRC_BYTES = 16 * 1024;
const int SCR_MAX_LINES = 2048;
const int MAX_FIXUPS = 256;
struct Label { char name[16]; int pc, line; };
struct Fixup { int op, line; char name[16]; bool call; };
struct IfFrame { int jz; int ends[12]; int nends; bool has_else; int line; };
// What the compiler needs to know about each map (slice 3D-6), read from its file: entity ids and links, the
// triggers' scripts and conditions, the exits' conditions, the shot names.
struct MapScan {
    int  nents, ntrig, nexits, nshots;
    char ent_id[MAX_ENTITIES][16];
    bool ent_npc[MAX_ENTITIES];
    char ent_talk[MAX_ENTITIES][16], ent_win[MAX_ENTITIES][16], ent_cond[MAX_ENTITIES][64];
    char trig_script[MAX_TRIGGERS][16], trig_cond[MAX_TRIGGERS][64];
    char exit_cond[MAX_EXITS][64];
    int  nglows;
    char glow_cond[MAX_GLOWS][64];
};
struct Scratch {
    char    src[SRC_BYTES];
    Label   labels[SCR_MAX_LABELS];
    Fixup   fix[MAX_FIXUPS];
    IfFrame ifs[8];
    int     nlabels, nfix, nifs;
    const char* file;
    int     line;
    MapScan scan[MAX_MAPS];
    int     file_map;            // @map of the file being compiled (-1: none yet)
    int     cur_map;             // the map the current line runs on (a script's @map, then its `map` commands)
    char    files[SCR_MAX_FILES][24];
    int     file_maps[SCR_MAX_FILES];
    int     nfiles;
};
static Scratch g_s;

static const char* const VAR_NAMES[VAR_COUNT] = { "neutral", "happy", "worried" };
static const char* const EMOTE_NAMES[EMOTE_COUNT] = { "!", "?", "...", "sweat", "anger" };
static const char* const DIR_NAMES[4] = { "down", "up", "left", "right" };
static const char* const OP_NAMES[SC_COUNT] = {
    "END", "PAGE", "SYS", "CHOICE", "OPTION", "JMP", "JZ", "CALL", "STAGE", "MAXHP", "FLAG", "UNFLAG", "GIVE_POTIONS",
    "TAKE_POTIONS", "HEAL", "CHECKPOINT", "OBJECTIVE", "FACE", "ANIM", "WAIT", "EMOTE", "FADE_OUT", "FADE_IN", "LETTERBOX",
    "MOVE", "PLACE", "CAM", "PAR", "BATTLE", "MAP", "LIGHT", "LIGHT_LERP", "NAMECARD", "CARD", "TRANSITION", "CREDITS",
    "RESULTS", "TITLE" };

static bool fail(const char* msg, const char* detail = nullptr) {
    if (!g_err[0]) {
        snprintf(g_err, sizeof g_err, "%s line %d: %s%s%s", g_s.file ? g_s.file : "?", g_s.line, msg,
                 detail ? ": " : "", detail ? detail : "");
        char line[360];
        snprintf(line, sizeof line, "script: %s", g_err);
        eng::dbg_print(line);
    }
    return false;
}

// ---------------------------------------------------------------------------- small helpers
// Bounded copy into a fixed field (lengths are validated before; this keeps every build free of
// -Wformat-truncation noise from snprintf("%s")).
static void copy_str(char* dst, int cap, const char* src) {
    int n = 0;
    while (src[n] && n < cap - 1) { dst[n] = src[n]; ++n; }
    dst[n] = '\0';
}

static bool is_ident_char(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
static bool is_space(char c) { return c == ' ' || c == '\t'; }
static const char* skip_ws(const char* s) { while (is_space(*s)) ++s; return s; }

// Copies the next whitespace-separated token (a "quoted string" is one token, quotes kept) into tok.
static const char* next_token(const char* s, char* tok, int cap) {
    s = skip_ws(s);
    int n = 0;
    if (*s == '"') {
        tok[n++] = *s++;
        while (*s && *s != '"') { if (n < cap - 2) tok[n++] = *s; ++s; }
        if (*s == '"') { tok[n++] = '"'; ++s; }
    } else {
        while (*s && !is_space(*s)) { if (n < cap - 1) tok[n++] = *s; ++s; }
    }
    tok[n] = '\0';
    return s;
}

static bool parse_uint(const char* s, int lo, int hi, int* out) {
    if (!*s) return false;
    int v = 0;
    for (; *s; ++s) { if (*s < '0' || *s > '9' || v > 100000) return false; v = v * 10 + (*s - '0'); }
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

static int add_str(const char* s, int len = -1) {
    if (len < 0) len = (int)strlen(s);
    if (g_pool.nstr + len + 1 > SCR_STR_BYTES) { fail("string pool full (12 KiB)"); return -1; }
    const int off = g_pool.nstr;
    memcpy(g_pool.str + off, s, (size_t)len);
    g_pool.str[off + len] = '\0';
    g_pool.nstr += len + 1;
    return off;
}

static int emit(uint8_t code, int a = 0, int b = 0, int c = 0, int str = 0) {
    if (g_pool.nops >= SCR_MAX_OPS) { fail("op pool full (1024)"); return -1; }
    ScrOp& o = g_pool.ops[g_pool.nops];
    o.code = code; o.a = (uint8_t)a; o.b = (uint16_t)b; o.c = (uint16_t)c; o.str = (uint16_t)str;
    return g_pool.nops++;
}

static int flag_find(const char* name) {
    for (int i = 0; i < g_pool.nflags; ++i) if (!strcmp(g_pool.flags[i], name)) return i;
    return -1;
}

static int scan_entity(int map, const char* name) {
    if (map < 0 || map >= g_pool.nmaps) return -1;
    const MapScan& m = g_s.scan[map];
    for (int i = 0; i < m.nents; ++i) if (!strcmp(m.ent_id[i], name)) return i;
    return -1;
}

// An actor of the current line's map -> its slot (added on first use); -1 if unknown (the caller fails), -2 if it
// is an entity of another map (fails here).
static int actor_find(const char* name) {
    if (!strcmp(name, "rowan")) return 0;
    if (scan_entity(g_s.cur_map, name) < 0) {
        for (int m = 0; m < g_pool.nmaps; ++m)
            if (scan_entity(m, name) >= 0) {
                char d[96];
                snprintf(d, sizeof d, "%s (on %s, the line runs on %s)", name, g_pool.maps[m], g_s.cur_map >= 0 ? g_pool.maps[g_s.cur_map] : "no map");
                fail("actor not on the script's map", d);
                return -2;
            }
        return -1;
    }
    for (int k = 1; k < g_pool.nactors; ++k) if (!strcmp(g_pool.actors[k], name)) return k;
    if (g_pool.nactors >= SCR_MAX_ACTORS) { fail("too many actors (24)"); return -2; }
    copy_str(g_pool.actors[g_pool.nactors], 16, name);
    return g_pool.nactors++;
}

// ---------------------------------------------------------------------------- conditions
// Shunting-yard to RPN (the same algorithm as lint.py compile_cond). Precedence as in C:
// ! (unary) > + - > < <= > >= > == != > && > ||; binary operators left-associative.
struct CTok { int kind; int v; };   // kind: 0 operand (v = code | value << 8), 1 operator (v = CondOp), 2 '(', 3 ')'
static int prec(int op) {
    switch (op) {
    case C_NOT: return 6;
    case C_ADD: case C_SUB: return 5;
    case C_LT: case C_LE: case C_GT: case C_GE: return 4;
    case C_EQ: case C_NE: return 3;
    case C_AND: return 2;
    case C_OR: return 1;
    }
    return 0;
}

// Compiles cond into the cond pool; returns its offset (length in *len), -1 on error.
static int compile_cond(const char* s, int* len) {
    uint8_t out[COND_MAX_BYTES];
    int n = 0, depth = 0;
    int stack[32], sp = 0;
    bool expect_operand = true;
    auto put = [&](int b) { if (n >= COND_MAX_BYTES) return false; out[n++] = (uint8_t)b; return true; };
    auto pop_to_out = [&]() {
        const int op = stack[--sp];
        if (op == C_NOT) { if (depth < 1) return false; }
        else { if (depth < 2) return false; --depth; }
        return put(op);
    };
    s = skip_ws(s);
    if (!*s) { fail("empty condition"); return -1; }
    while (*s) {
        s = skip_ws(s);
        if (!*s) break;
        if ((*s >= 'a' && *s <= 'z') || *s == '_' || (*s >= '0' && *s <= '9')) {
            if (!expect_operand) { fail("bad condition: two operands in a row"); return -1; }
            char w[24]; int k = 0;
            while (is_ident_char(*s)) { if (k < 23) w[k++] = *s; ++s; }
            w[k] = '\0';
            int v;
            bool ok;
            if (w[0] >= '0' && w[0] <= '9') {
                if (!parse_uint(w, 0, 255, &v)) { fail("bad number in condition (0..255)", w); return -1; }
                ok = put(C_PUSH) && put(v);
            } else if (!strcmp(w, "stage")) ok = put(C_STAGE);
            else if (!strcmp(w, "potions")) ok = put(C_POTIONS);
            else if (!strcmp(w, "max_hp")) ok = put(C_MAXHP);
            else {
                const int f = flag_find(w);
                if (f < 0) { fail("unknown name in condition (not stage, potions, max_hp or a @flags flag)", w); return -1; }
                ok = put(C_FLAG) && put(f);
            }
            if (!ok) { fail("condition longer than 32 bytes"); return -1; }
            ++depth;
            expect_operand = false;
            continue;
        }
        int op = -1, adv = 1;
        if (s[0] == '(') {
            if (!expect_operand) { fail("bad condition: '(' after an operand"); return -1; }
            if (sp >= 32) { fail("condition too deep"); return -1; }
            stack[sp++] = -1; ++s; continue;
        }
        if (s[0] == ')') {
            if (expect_operand) { fail("bad condition: ')' without an operand"); return -1; }
            while (sp > 0 && stack[sp - 1] != -1) if (!pop_to_out()) { fail("bad condition"); return -1; }
            if (sp == 0) { fail("bad condition: unbalanced ')'"); return -1; }
            --sp; ++s; continue;
        }
        if (s[0] == '!' && s[1] != '=') {
            if (!expect_operand) { fail("bad condition: '!' after an operand"); return -1; }
            if (sp >= 32) { fail("condition too deep"); return -1; }
            stack[sp++] = C_NOT; ++s; continue;
        }
        if (s[0] == '=' && s[1] == '=') { op = C_EQ; adv = 2; }
        else if (s[0] == '!' && s[1] == '=') { op = C_NE; adv = 2; }
        else if (s[0] == '<' && s[1] == '=') { op = C_LE; adv = 2; }
        else if (s[0] == '>' && s[1] == '=') { op = C_GE; adv = 2; }
        else if (s[0] == '&' && s[1] == '&') { op = C_AND; adv = 2; }
        else if (s[0] == '|' && s[1] == '|') { op = C_OR; adv = 2; }
        else if (s[0] == '<') op = C_LT;
        else if (s[0] == '>') op = C_GT;
        else if (s[0] == '+') op = C_ADD;
        else if (s[0] == '-') op = C_SUB;
        if (op < 0) { char c[2] = { *s, 0 }; fail("bad character in condition", c); return -1; }
        if (expect_operand) { fail("bad condition: an operator where an operand belongs"); return -1; }
        while (sp > 0 && stack[sp - 1] != -1 && prec(stack[sp - 1]) >= prec(op))
            if (!pop_to_out()) { fail("bad condition"); return -1; }
        if (sp >= 32) { fail("condition too deep"); return -1; }
        stack[sp++] = op;
        s += adv;
        expect_operand = true;
    }
    if (expect_operand) { fail("bad condition: it ends with an operator"); return -1; }
    while (sp > 0) {
        if (stack[sp - 1] == -1) { fail("bad condition: unbalanced '('"); return -1; }
        if (!pop_to_out()) { fail("bad condition"); return -1; }
    }
    if (n > COND_MAX_BYTES || depth != 1) { fail("bad condition"); return -1; }
    if (g_pool.ncond + n > SCR_COND_BYTES) { fail("condition pool full (1 KiB)"); return -1; }
    const int off = g_pool.ncond;
    memcpy(g_pool.cond + off, out, (size_t)n);
    g_pool.ncond += n;
    *len = n;
    return off;
}

// ---------------------------------------------------------------------------- source lines
// Splits the source into lines in place: comments ('#' at the start or after a blank, outside quotes)
// and trailing blanks removed. Returns the line count (starts[i] points at line i + 1), or -1 if the
// source has more than `max` lines.
static int split_lines(char* src, char** starts, int max) {
    int n = 0;
    char* p = src;
    while (*p) {
        if (n >= max) return -1;
        starts[n++] = p;
        char* e = p;
        while (*e && *e != '\n') ++e;
        const bool more = *e == '\n';
        *e = '\0';
        bool q = false;
        for (char* c = p; *c; ++c) {
            if (*c == '"') q = !q;
            else if (*c == '#' && !q && (c == p || is_space(c[-1]))) { *c = '\0'; break; }
            else if (*c == '\r') { *c = '\0'; break; }
        }
        int L = (int)strlen(p);
        while (L > 0 && is_space(p[L - 1])) p[--L] = '\0';
        p = more ? e + 1 : e;
    }
    return n;
}

// Reads a logical line (a line plus its '|' continuation lines) into out; returns the next line index.
static int logical_line(char** lines, int n, int i, char* out, int cap, int* line_no) {
    *line_no = i + 1;
    snprintf(out, (size_t)cap, "%s", skip_ws(lines[i]));
    ++i;
    while (i < n) {
        const char* t = skip_ws(lines[i]);
        if (*t != '|') break;
        const int L = (int)strlen(out);
        snprintf(out + L, (size_t)(cap - L), " %s", t);
        ++i;
    }
    return i;
}

// ---------------------------------------------------------------------------- statements
static bool in_script() { return g_pool.nscripts > 0 && g_pool.scripts[g_pool.nscripts - 1].end == 0xFFFF; }

static bool close_script() {
    if (!in_script()) return true;
    if (g_s.nifs) { g_s.line = g_s.ifs[g_s.nifs - 1].line; return fail("@if without @end"); }
    g_pool.scripts[g_pool.nscripts - 1].end = (uint16_t)g_pool.nops;
    return true;
}

static bool add_fixup(int op, const char* name, bool call) {
    if (g_s.nfix >= MAX_FIXUPS) return fail("too many label references");
    Fixup& f = g_s.fix[g_s.nfix++];
    f.op = op; f.line = g_s.line; f.call = call;
    copy_str(f.name, (int)sizeof f.name, name);
    return true;
}

static bool stmt_page(const char* line, const char* colon) {
    // speaker[(variant)]: text    |    sys: text
    char spk[16] = "", var[16] = "";
    const char* p = line;
    int k = 0;
    while (p < colon && *p != '(') { if (k < 15) spk[k++] = *p; ++p; }
    spk[k] = '\0';
    if (*p == '(') {
        ++p; k = 0;
        while (p < colon && *p != ')') { if (k < 15) var[k++] = *p; ++p; }
        var[k] = '\0';
    }
    const char* text = skip_ws(colon + 1);
    if (!*text) return fail("empty page");
    static char wrapped[512];
    const int lines = text_wrap(text, PAGE_WIDTH, wrapped, sizeof wrapped);
    if (lines < 0) return fail("page text cannot be wrapped (a word wider than 420 px, a double space, a space next to '|', or a character the font lacks)");
    if (lines > PAGE_LINES) { char d[48]; snprintf(d, sizeof d, "%d lines of 420 px, at most 3", lines); return fail("page does not fit the box", d); }
    const int off = add_str(wrapped);
    if (off < 0) return false;
    const int glyphs = text_glyph_count(wrapped);
    if (!strcmp(spk, "sys")) {
        if (var[0]) return fail("sys: pages have no variant");
        return emit(SC_SYS, 0, 0, glyphs, off) >= 0;
    }
    int si = -1;
    for (int i = 0; i < g_pool.nspeakers; ++i) if (!strcmp(g_pool.speakers[i].id, spk)) si = i;
    if (si < 0) return fail("unknown speaker (not in speakers.txt)", spk);
    int vi = VAR_NEUTRAL;
    if (var[0]) {
        vi = -1;
        for (int i = 0; i < VAR_COUNT; ++i) if (!strcmp(var, VAR_NAMES[i])) vi = i;
        if (vi < 0) return fail("unknown portrait variant (neutral, happy, worried)", var);
    }
    return emit(SC_PAGE, si, vi, glyphs, off) >= 0;
}

static bool stmt_choice(const char* rest) {
    // "A" -> l | "B" -> l [| cancel "C" -> l]
    char buf[320];
    snprintf(buf, sizeof buf, "%s", rest);
    const char* opts[3]; int nopt = 0;
    bool q = false;
    char* seg = buf;
    for (char* c = buf;; ++c) {
        if (*c == '"') q = !q;
        if ((*c == '|' && !q) || !*c) {
            const bool end = !*c;
            *c = '\0';
            if (nopt >= 3) return fail("a choice has at most 3 options");
            opts[nopt++] = seg;
            if (end) break;
            seg = c + 1;
        }
    }
    if (nopt < 1) return fail("a choice has 1 to 3 options");
    int texts[3], cancel = NO_CANCEL;
    char labels[3][16];
    for (int i = 0; i < nopt; ++i) {
        const char* s = skip_ws(opts[i]);
        char tok[200];
        s = next_token(s, tok, sizeof tok);
        if (!strcmp(tok, "cancel")) {
            if (cancel != NO_CANCEL) return fail("a choice has one cancel option at most");
            if (i != nopt - 1) return fail("the cancel option must be the last one (GDD 7.3)");
            cancel = i;
            s = next_token(s, tok, sizeof tok);
        }
        const int L = (int)strlen(tok);
        if (L < 3 || tok[0] != '"' || tok[L - 1] != '"') return fail("choice option: expected \"text\" -> label");
        tok[L - 1] = '\0';
        const char* t = tok + 1;
        static char wrapped[200];
        if (text_wrap(t, 10000, wrapped, sizeof wrapped) != 1) return fail("bad choice text", t);
        if (text_width(t) > CHOICE_TEXT_MAX) return fail("choice text wider than the choice box", t);
        texts[i] = add_str(t);
        if (texts[i] < 0) return false;
        char arrow[8];
        s = next_token(s, arrow, sizeof arrow);
        if (strcmp(arrow, "->")) return fail("choice option: expected -> label");
        s = next_token(s, labels[i], sizeof labels[i]);
        if (!labels[i][0] || *skip_ws(s)) return fail("choice option: expected one label after ->");
    }
    if (emit(SC_CHOICE, nopt, cancel) < 0) return false;
    for (int i = 0; i < nopt; ++i) {
        const int op = emit(SC_OPTION, 0, 0, 0, texts[i]);
        if (op < 0 || !add_fixup(op, labels[i], false)) return false;
    }
    return true;
}

static bool parse_sint(const char* s, int lo, int hi, int* out, bool* sign) {
    *sign = *s == '-' || *s == '+';
    const bool neg = *s == '-';
    if (*sign) ++s;
    int v;
    if (!parse_uint(s, 0, 100000, &v)) return false;
    v = neg ? -v : v;
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

static int dir_index(const char* s) {
    for (int d = 0; d < 4; ++d) if (!strcmp(s, DIR_NAMES[d])) return d;
    return -1;
}

// A quoted string argument ("text") into t (cap bytes); false if it is not one.
static bool quoted_arg(const char* rest, char* t, int cap) {
    const char* q = skip_ws(rest);
    const int L = (int)strlen(q);
    if (L < 2 || q[0] != '"' || q[L - 1] != '"' || L - 2 >= cap) return false;
    memcpy(t, q + 1, (size_t)(L - 2)); t[L - 2] = '\0';
    return !strchr(t, '"');
}

static bool stmt_single(const char* line, bool in_par);

// Slice 3D-6: `a | b | c` runs actor commands together; the line ends when all have ended (docs/NEXT_SLICE_BARROW.md 5.5).
static bool stmt_parallel(const char* line) {
    char buf[512];
    copy_str(buf, (int)sizeof buf, line);
    char* parts[4];
    int n = 0;
    char* seg = buf;
    for (char* c = buf;; ++c) {
        if (*c == '|' || !*c) {
            const bool end = !*c;
            *c = '\0';
            if (n >= 4) return fail("a parallel line has at most 4 commands");
            parts[n++] = seg;
            if (end) break;
            seg = c + 1;
        }
    }
    if (n < 2) return fail("a parallel line needs two commands");
    if (emit(SC_PAR, n) < 0) return false;
    for (int i = 0; i < n; ++i) {
        const char* p = skip_ws(parts[i]);
        char w[24];
        next_token(p, w, sizeof w);
        if (strcmp(w, "move") && strcmp(w, "anim") && strcmp(w, "face") && strcmp(w, "emote") && strcmp(w, "cam") && strcmp(w, "wait"))
            return fail("only move, anim, face, emote, cam and wait may run in parallel", w);
        char one[200];
        copy_str(one, (int)sizeof one, p);
        int L = (int)strlen(one);
        while (L > 0 && is_space(one[L - 1])) one[--L] = '\0';
        if (!stmt_single(one, true)) return false;
    }
    return true;
}

static bool stmt_command(const char* line) {
    char w[24];
    const char* rest = next_token(line, w, sizeof w);
    if (!strcmp(w, "choice")) return stmt_choice(skip_ws(rest));
    bool q = false;
    for (const char* c = line; *c; ++c) { if (*c == '"') q = !q; else if (*c == '|' && !q) return stmt_parallel(line); }
    return stmt_single(line, false);
}

static bool stmt_single(const char* line, bool in_par) {
    char w[24], a1[64], a2[64];
    const char* rest = next_token(line, w, sizeof w);
    (void)in_par;
    const char* r2 = next_token(rest, a1, sizeof a1);
    const char* r3 = next_token(r2, a2, sizeof a2);
    const bool more = *skip_ws(r3) != '\0';
    int v;
    if (!strcmp(w, "end")) { if (a1[0]) return fail("end takes no argument"); return emit(SC_END) >= 0; }
    if (!strcmp(w, "goto")) {
        if (!a1[0] || a2[0]) return fail("goto takes one label");
        const int op = emit(SC_JMP);
        return op >= 0 && add_fixup(op, a1, false);
    }
    if (!strcmp(w, "call")) {
        if (!a1[0] || a2[0]) return fail("call takes one script name");
        const int op = emit(SC_CALL);
        return op >= 0 && add_fixup(op, a1, true);
    }
    if (!strcmp(w, "set")) {
        if (more || !parse_uint(a2, 0, 255, &v)) return fail("set stage|max_hp <0..255>");
        if (!strcmp(a1, "stage")) return emit(SC_STAGE, v) >= 0;
        if (!strcmp(a1, "max_hp")) { if (v < 1) return fail("max_hp must be at least 1"); return emit(SC_MAXHP, v) >= 0; }
        return fail("set stage|max_hp <n>", a1);
    }
    if (!strcmp(w, "flag") || !strcmp(w, "unflag")) {
        if (!a1[0] || a2[0]) return fail("flag|unflag <name>");
        const int f = flag_find(a1);
        if (f < 0) return fail("flag not declared in @flags", a1);
        return emit(w[0] == 'f' ? SC_FLAG : SC_UNFLAG, f) >= 0;
    }
    if (!strcmp(w, "give") || !strcmp(w, "take")) {
        if (!strcmp(a1, "potions")) {
            if (more || !parse_uint(a2, 1, MAX_POTIONS, &v)) return fail("give|take potions <1..3>");
            return emit(w[0] == 'g' ? SC_GIVE_POTIONS : SC_TAKE_POTIONS, v) >= 0;
        }
        if (w[0] == 'g' && !strcmp(a1, "skill")) {
            if (strcmp(a2, "fireball") || more) return fail("give skill fireball (the one learnable skill)");
            const int f = flag_find("skill_fire");
            if (f < 0) return fail("give skill fireball needs the flag skill_fire in @flags");
            return emit(SC_FLAG, f) >= 0;
        }
        if (w[0] == 'g' && !strcmp(a1, "token")) {
            if (a2[0]) return fail("give token takes nothing else");
            const int f = flag_find("has_token");
            if (f < 0) return fail("give token needs the flag has_token in @flags");
            return emit(SC_FLAG, f) >= 0;
        }
        return fail("give potions N | take potions N | give skill fireball | give token");
    }
    if (!strcmp(w, "heal")) { if (a1[0]) return fail("heal takes no argument"); return emit(SC_HEAL) >= 0; }
    if (!strcmp(w, "checkpoint")) {
        int id = 1;
        if (!strcmp(a1, "c1")) id = 1; else if (!strcmp(a1, "c2")) id = 2; else if (!strcmp(a1, "c3")) id = 3; else if (!strcmp(a1, "rest")) id = 9;
        else if (a1[0]) return fail("checkpoint [c1|c2|c3|rest]", a1);
        if (a2[0]) return fail("checkpoint takes one id at most");
        return emit(SC_CHECKPOINT, id) >= 0;
    }
    if (!strcmp(w, "objective")) {
        const char* s = skip_ws(rest);
        const int L = (int)strlen(s);
        if (L < 2 || s[0] != '"' || s[L - 1] != '"') return fail("objective \"text\"");
        char t[80];
        if (L - 2 >= (int)sizeof t) return fail("objective text too long");
        memcpy(t, s + 1, (size_t)(L - 2)); t[L - 2] = '\0';
        if (strchr(t, '"')) return fail("objective \"text\"");
        int id = 0;
        if (t[0]) {
            // {patrols} is the one placeholder (the live counter); the text must fit the HUD plate
            for (const char* b = strchr(t, '{'); b; b = strchr(b + 1, '{'))
                if (strncmp(b, "{patrols}", 9)) return fail("objective: the only placeholder is {patrols}");
            for (int i = 1; i < g_pool.nobjectives; ++i) if (!strcmp(g_pool.str + g_pool.objectives[i], t)) id = i;
            if (!id) {
                if (g_pool.nobjectives >= SCR_MAX_OBJECTIVES) return fail("too many objectives (32)");
                const int off = add_str(t);
                if (off < 0) return false;
                id = g_pool.nobjectives++;
                g_pool.objectives[id] = (uint16_t)off;
            }
        }
        return emit(SC_OBJECTIVE, id) >= 0;
    }
    if (!strcmp(w, "face")) {
        const int a = actor_find(a1);
        if (a < 0) return fail("face: unknown actor (rowan or a map entity id)", a1);
        if (more || !a2[0]) return fail("face <actor> <down|up|left|right|actor>");
        for (int d = 0; d < 4; ++d) if (!strcmp(a2, DIR_NAMES[d])) return emit(SC_FACE, a, d) >= 0;
        const int t = actor_find(a2);
        if (t < 0) return fail("face: unknown direction or actor", a2);
        if (t == a) return fail("face: an actor cannot face itself");
        return emit(SC_FACE, a, 0x100 | t) >= 0;
    }
    if (!strcmp(w, "anim")) {
        const int a = actor_find(a1);
        if (a < 0) return fail("anim: unknown actor (rowan or a map entity id)", a1);
        if (more || !a2[0]) return fail("anim <actor> <clip>");
        const anim::Library* L = anim::shared_library();
        const int c = L ? anim::clip_find(*L, a2) : -1;
        if (c < 0) return fail("anim: no such clip in the animation library", a2);
        return emit(SC_ANIM, a, c) >= 0;
    }
    if (!strcmp(w, "emote")) {
        const int a = actor_find(a1);
        if (a < 0) return fail("emote: unknown actor", a1);
        if (more) return fail("emote <actor> <!|?|...|sweat|anger>");
        for (int k = 0; k < EMOTE_COUNT; ++k) if (!strcmp(a2, EMOTE_NAMES[k])) return emit(SC_EMOTE, a, k) >= 0;
        return fail("emote kind must be ! ? ... sweat anger", a2);
    }
    if (!strcmp(w, "wait")) {
        if (a2[0] || !parse_uint(a1, 1, 600, &v)) return fail("wait <1..600 ticks>");
        return emit(SC_WAIT, 0, v) >= 0;
    }
    if (!strcmp(w, "fade")) {
        if (more || (strcmp(a1, "out") && strcmp(a1, "in")) || !parse_uint(a2, 1, 240, &v)) return fail("fade out|in <1..240 ticks>");
        return emit(a1[0] == 'o' ? SC_FADE_OUT : SC_FADE_IN, 0, v) >= 0;
    }
    if (!strcmp(w, "letterbox")) {
        if (a2[0] || (strcmp(a1, "on") && strcmp(a1, "off"))) return fail("letterbox on|off");
        return emit(SC_LETTERBOX, a1[1] == 'n') >= 0;
    }
    // ---- slice 3D-6: cutscene commands (docs/NEXT_SLICE_BARROW.md 5.5)
    if (!strcmp(w, "move")) {
        // move <actor> <x> <y> [run]: to a tile; a signed number (move rowan -1 0) makes the move relative
        char a3[16], a4[16];
        const char* r4 = next_token(r3, a3, sizeof a3);
        const char* r5 = next_token(r4, a4, sizeof a4);
        const int a = actor_find(a1);
        if (a < 0) return fail("move: unknown actor (rowan or a map entity id)", a1);
        int x, y;
        bool sx, sy;
        if (!parse_sint(a2, -63, 63, &x, &sx) || !parse_sint(a3, -63, 63, &y, &sy) || (a4[0] && strcmp(a4, "run")) || *skip_ws(r5))
            return fail("move <actor> <x> <y> [run] (a signed dx dy: relative)");
        const bool rel = sx || sy;
        if (!rel && (x < 0 || y < 0)) return fail("move: a tile is 0..63");
        return emit(SC_MOVE, a, (x & 0xFF) | (y & 0xFF) << 8, (rel ? MOVE_REL : 0) | (a4[0] ? MOVE_RUN : 0)) >= 0;
    }
    if (!strcmp(w, "place")) {
        char a3[16], a4[16];
        const char* r4 = next_token(r3, a3, sizeof a3);
        const char* r5 = next_token(r4, a4, sizeof a4);
        const int a = actor_find(a1);
        if (a < 0) return fail("place: unknown actor (rowan or a map entity id)", a1);
        int x, y;
        const int d = a4[0] ? dir_index(a4) : 0;
        if (!parse_uint(a2, 0, 63, &x) || !parse_uint(a3, 0, 63, &y) || d < 0 || *skip_ws(r5)) return fail("place <actor> <x> <y> [down|up|left|right]");
        return emit(SC_PLACE, a, x | y << 8, d) >= 0;
    }
    if (!strcmp(w, "cam")) {
        // cam <shot|field> [ticks]: eases to a shot of the map (0 ticks: a cut); blocks for the ease
        int t = 0;
        if (more || (a2[0] && !parse_uint(a2, 0, 600, &t))) return fail("cam <shot|field> [0..600 ticks]");
        if (!strcmp(a1, "field")) return emit(SC_CAM, CAM_FIELD, t, g_s.cur_map < 0 ? 0 : g_s.cur_map) >= 0;
        int shot = -1;
        if (g_s.cur_map >= 0)
            for (int i = 0; i < MAX_SHOTS; ++i) if (g_pool.shots[g_s.cur_map][i][0] && !strcmp(g_pool.shots[g_s.cur_map][i], a1)) shot = i;
        if (shot < 0) return fail("cam: unknown shot (not a `shot` of the script's map)", a1);
        return emit(SC_CAM, shot, t, g_s.cur_map) >= 0;
    }
    if (!strcmp(w, "battle")) {
        const int a = actor_find(a1);
        if (a <= 0) return fail("battle: not an enemy of the script's map", a1);
        const int e = scan_entity(g_s.cur_map, a1);
        if (e < 0 || g_s.scan[g_s.cur_map].ent_npc[e]) return fail("battle: not an enemy of the script's map", a1);
        if (more || (a2[0] && strcmp(a2, "boss"))) return fail("battle <enemy> [boss]");
        return emit(SC_BATTLE, a, a2[0] ? 1 : 0) >= 0;
    }
    if (!strcmp(w, "map")) {
        // map <name> <x> <y> <dir> [light <preset>]
        char a3[16], a4[16], a5[16], a6[16];
        const char* r4 = next_token(r3, a3, sizeof a3);
        const char* r5 = next_token(r4, a4, sizeof a4);
        const char* r6 = next_token(r5, a5, sizeof a5);
        const char* r7 = next_token(r6, a6, sizeof a6);
        const int m = script_map_find(a1);
        if (m < 0) return fail("map: not a map of maps.txt", a1);
        int x, y;
        const int d = dir_index(a4);
        if (!parse_uint(a2, 0, 63, &x) || !parse_uint(a3, 0, 63, &y) || d < 0 || *skip_ws(r7) || (a5[0] && (strcmp(a5, "light") || !a6[0])))
            return fail("map <name> <x> <y> <dir> [light <preset>]");
        int preset = -1;
        if (a5[0]) { preset = light_find(a6); if (preset < 0) return fail("map: unknown light preset (lights.txt)", a6); }
        g_s.cur_map = m;                                  // the following lines run on that map
        return emit(SC_MAP, m, x | y << 8, d | (preset + 1) << 4) >= 0;
    }
    if (!strcmp(w, "light")) {
        if (!strcmp(a1, "lerp")) {
            char a3[16], a4[16];
            const char* r4 = next_token(r3, a3, sizeof a3);
            const char* r5 = next_token(r4, a4, sizeof a4);
            const int from = light_find(a2), to = light_find(a3);
            int t;
            if (from < 0 || to < 0 || !parse_uint(a4, 1, 3600, &t) || *skip_ws(r5)) return fail("light lerp <preset> <preset> <1..3600 ticks> (presets of lights.txt)");
            return emit(SC_LIGHT_LERP, from, t, to) >= 0;
        }
        const int l = light_find(a1);
        if (l < 0 || a2[0]) return fail("light <preset> | light lerp <a> <b> <ticks> (presets of lights.txt)", a1);
        return emit(SC_LIGHT, l) >= 0;
    }
    if (!strcmp(w, "namecard") || !strcmp(w, "card")) {
        char t[48];
        if (!quoted_arg(rest, t, (int)sizeof t) || !t[0]) return fail("namecard|card \"text\" (1..47 characters)");
        static char one_line[200];
        if (text_wrap(t, 560, one_line, (int)sizeof one_line) != 1) return fail("card text must fit one line of 560 px", t);
        const int off = add_str(t);
        if (off < 0) return false;
        return emit(w[0] == 'n' ? SC_NAMECARD : SC_CARD, 0, w[0] == 'n' ? NAMECARD_TICKS : CARD_TICKS, 0, off) >= 0;
    }
    if (!strcmp(w, "transition")) {
        int id;
        if (a2[0] || a1[0] != 'T' || !parse_uint(a1 + 1, 1, 11, &id)) return fail("transition T1..T11");
        return emit(SC_TRANSITION, id) >= 0;
    }
    if (!strcmp(w, "credits") || !strcmp(w, "results") || !strcmp(w, "title")) {
        if (a1[0]) return fail("credits, results and title take no argument");
        return emit(w[0] == 'c' ? SC_CREDITS : w[0] == 'r' ? SC_RESULTS : SC_TITLE) >= 0;
    }
    return fail("unknown command", w);
}

static bool stmt(const char* line) {
    if (line[0] == '@') {
        char w[16];
        const char* rest = next_token(line, w, sizeof w);
        rest = skip_ws(rest);
        if (!strcmp(w, "@flags")) return true;                  // first pass
        if (!strcmp(w, "@speaker")) return fail("@speaker belongs in speakers.txt");
        if (!strcmp(w, "@map")) {                                    // slice 3D-6: the map the following scripts start on
            if (in_script()) { if (!close_script()) return false; }
            char name[24];
            rest = next_token(rest, name, sizeof name);
            const int m = script_map_find(name);
            if (m < 0 || *skip_ws(rest)) return fail("@map <a map of maps.txt>", name);
            g_s.file_map = g_s.cur_map = m;
            return true;
        }
        if (!strcmp(w, "@marker")) {
            if (in_script()) return fail("@marker inside a script");
            char id[24], kind[8];
            rest = next_token(rest, id, sizeof id);
            rest = next_token(rest, kind, sizeof kind);
            g_s.cur_map = g_s.file_map;
            const int e = scan_entity(g_s.file_map, id);
            if (e < 0 || !g_s.scan[g_s.file_map].ent_npc[e]) return fail("@marker: not an npc of the map", id);
            int k;
            if (!strcmp(kind, "!")) k = MARK_QUEST; else if (!strcmp(kind, "...")) k = MARK_TALK;
            else return fail("@marker kind must be ! or ...", kind);
            if (g_pool.nmarkers >= SCR_MAX_MARKERS) return fail("too many markers (8)");
            int len = 0;
            const int off = compile_cond(rest, &len);
            if (off < 0) return false;
            Marker& m = g_pool.markers[g_pool.nmarkers++];
            m.entity = (int8_t)e; m.kind = (uint8_t)k; m.cond_len = (uint8_t)len; m.map = (uint8_t)g_s.file_map; m.cond = (uint16_t)off;
            return true;
        }
        if (!strcmp(w, "@script")) {
            if (!close_script()) return false;
            char name[24], opt[16];
            rest = next_token(rest, name, sizeof name);
            rest = next_token(rest, opt, sizeof opt);
            if (!name[0] || *skip_ws(rest) || strlen(name) > 15 || (opt[0] && strcmp(opt, "skip"))) return fail("@script <name> [skip] (15 characters at most)");
            if (g_s.file_map < 0) return fail("@script before @map (this file is not a map's: say where its scripts start)");
            for (const char* c = name; *c; ++c) if (!is_ident_char(*c)) return fail("bad script name", name);
            if (script_find(name) >= 0) return fail("duplicate script", name);
            if (g_pool.nscripts >= SCR_MAX_SCRIPTS) return fail("too many scripts (32)");
            ScriptDef& d = g_pool.scripts[g_pool.nscripts++];
            copy_str(d.name, (int)sizeof d.name, name);
            d.first = (uint16_t)g_pool.nops; d.end = 0xFFFF;
            d.map = (uint8_t)g_s.file_map; d.skip = opt[0] ? 1 : 0;
            g_s.cur_map = g_s.file_map;
            return true;
        }
        if (!in_script()) return fail("statement outside a @script", w);
        if (!strcmp(w, "@if") || !strcmp(w, "@elif")) {
            int len = 0;
            if (w[1] == 'e') {
                if (!g_s.nifs) return fail("@elif without @if");
                IfFrame& f = g_s.ifs[g_s.nifs - 1];
                if (f.has_else) return fail("@elif after @else");
                if (f.nends >= 12) return fail("too many @elif branches");
                const int j = emit(SC_JMP);
                if (j < 0) return false;
                f.ends[f.nends++] = j;
                g_pool.ops[f.jz].b = (uint16_t)g_pool.nops;
            } else {
                if (g_s.nifs >= 8) return fail("@if nested too deep (8)");
                IfFrame& f = g_s.ifs[g_s.nifs++];
                f.nends = 0; f.has_else = false; f.line = g_s.line;
            }
            const int off = compile_cond(rest, &len);
            if (off < 0) return false;
            const int jz = emit(SC_JZ, len, 0, off);
            if (jz < 0) return false;
            g_s.ifs[g_s.nifs - 1].jz = jz;
            return true;
        }
        if (!strcmp(w, "@else")) {
            if (*rest) return fail("@else takes nothing");
            if (!g_s.nifs) return fail("@else without @if");
            IfFrame& f = g_s.ifs[g_s.nifs - 1];
            if (f.has_else) return fail("two @else");
            if (f.nends >= 12) return fail("too many @elif branches");
            const int j = emit(SC_JMP);
            if (j < 0) return false;
            f.ends[f.nends++] = j;
            g_pool.ops[f.jz].b = (uint16_t)g_pool.nops;
            f.jz = -1;
            f.has_else = true;
            return true;
        }
        if (!strcmp(w, "@end")) {
            if (*rest) return fail("@end takes nothing");
            if (!g_s.nifs) return fail("@end without @if");
            IfFrame& f = g_s.ifs[--g_s.nifs];
            if (f.jz >= 0) g_pool.ops[f.jz].b = (uint16_t)g_pool.nops;
            for (int i = 0; i < f.nends; ++i) g_pool.ops[f.ends[i]].b = (uint16_t)g_pool.nops;
            return true;
        }
        return fail("unknown directive", w);
    }
    if (!in_script()) return fail("statement outside a @script");
    if (line[0] == ':') {
        const char* name = line + 1;
        if (!*name || strlen(name) > 15) return fail(":label (15 characters at most)");
        for (const char* c = name; *c; ++c) if (!is_ident_char(*c)) return fail("bad label", name);
        for (int i = 0; i < g_s.nlabels; ++i) if (!strcmp(g_s.labels[i].name, name)) return fail("duplicate label (labels are global)", name);
        if (g_s.nlabels >= SCR_MAX_LABELS) return fail("too many labels (128)");
        Label& l = g_s.labels[g_s.nlabels++];
        copy_str(l.name, (int)sizeof l.name, name);
        l.pc = g_pool.nops; l.line = g_s.line;
        return true;
    }
    // a page: identifier[(variant)]: text
    const char* p = line;
    while (is_ident_char(*p)) ++p;
    if (p > line && *p == '(') { while (*p && *p != ')') ++p; if (*p == ')') ++p; }
    if (p > line && *p == ':' && (p[1] == ' ' || p[1] == '\0')) return stmt_page(line, p);
    return stmt_command(line);
}

// ---------------------------------------------------------------------------- reachability
// A script must never run past its last op (GDD 7.4 "every branch ends"): walks every path.
static bool check_fall_off(const ScriptDef& d) {
    static uint8_t seen[SCR_MAX_OPS];
    static uint16_t work[SCR_MAX_OPS];
    memset(seen, 0, sizeof seen);
    int nw = 0;
    work[nw++] = d.first;
    while (nw) {
        int pc = work[--nw];
        if (pc >= d.end) { char m[64]; snprintf(m, sizeof m, "%s", d.name); return fail("a path runs off the end of the script (missing 'end')", m); }
        if (seen[pc]) continue;
        seen[pc] = 1;
        const ScrOp& o = g_pool.ops[pc];
        auto push = [&](int t) { if (nw < SCR_MAX_OPS) work[nw++] = (uint16_t)t; };
        switch (o.code) {
        case SC_END: break;
        case SC_JMP: push(o.b); break;
        case SC_JZ: push(pc + 1); push(o.b); break;
        case SC_CHOICE: for (int k = 0; k < o.a; ++k) push(g_pool.ops[pc + 1 + k].b); break;
        case SC_OPTION: break;
        case SC_PAR: push(pc + 1 + o.a); break;
        case SC_TITLE: break;                                   // the session ends
        default: push(pc + 1); break;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------- load
static bool load_speakers() {
    static char buf[2048];
    g_s.file = "speakers.txt"; g_s.line = 0;
    const int n = eng::file_read("/rd/scripts/speakers.txt", buf, sizeof buf);
    if (n < 0) return fail("missing /rd/scripts/speakers.txt");
    static char* lines[64];
    const int nl = split_lines(buf, lines, 64);
    if (nl < 0) return fail("more than 64 lines");
    for (int i = 0; i < nl; ++i) {
        g_s.line = i + 1;
        const char* s = skip_ws(lines[i]);
        if (!*s) continue;
        char w[16], id[16], name[32], col[16], por[16];
        s = next_token(s, w, sizeof w);
        s = next_token(s, id, sizeof id);
        s = next_token(s, name, sizeof name);
        s = next_token(s, col, sizeof col);
        s = next_token(s, por, sizeof por);
        const int L = (int)strlen(name);
        if (strcmp(w, "@speaker") || !id[0] || L < 3 || name[0] != '"' || name[L - 1] != '"' || strlen(col) != 6 || !por[0] || *skip_ws(s))
            return fail("@speaker id \"Name\" RRGGBB portrait_base");
        if (strlen(id) > 11 || L - 2 > 11 || strlen(por) > 11) return fail("speaker id, name and portrait base are 11 characters at most");
        if (!strcmp(id, "sys")) return fail("'sys' is reserved");
        if (script_speaker_find(id) >= 0) return fail("duplicate speaker", id);
        if (g_pool.nspeakers >= SCR_MAX_SPEAKERS) return fail("too many speakers (8)");
        uint32_t rgb = 0;
        for (int k = 0; k < 6; ++k) {
            const char c = col[k];
            const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (d < 0) return fail("bad colour", col);
            rgb = rgb << 4 | (uint32_t)d;
        }
        Speaker& sp = g_pool.speakers[g_pool.nspeakers++];
        copy_str(sp.id, (int)sizeof sp.id, id);
        memcpy(sp.name, name + 1, (size_t)(L - 2)); sp.name[L - 2] = '\0';
        sp.rgb = rgb;
        copy_str(sp.portrait, (int)sizeof sp.portrait, por);
    }
    return true;
}

// ---------------------------------------------------------------------------- maps (slice 3D-6)
// Reads what the compiler needs from a map file (the same lines field_load parses, in the same order): entity ids,
// kinds, talk scripts, win flags and `if` conditions; triggers' scripts and conditions; exits' conditions; shots.
static bool scan_map(int mi) {
    MapScan& m = g_s.scan[mi];
    memset(&m, 0, sizeof m);
    char path[64];
    snprintf(path, sizeof path, "/rd/maps/%s.txt", g_pool.maps[mi]);
    static char fname[32];
    snprintf(fname, sizeof fname, "maps/%s.txt", g_pool.maps[mi]);
    g_s.file = fname; g_s.line = 0;
    const int n = eng::file_read(path, g_s.src, SRC_BYTES);
    if (n < 0) return fail("map file missing or larger than 16 KiB");
    int line_no = 0;
    for (char* line = g_s.src; line && *line;) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';
        ++line_no;
        g_s.line = line_no;
        char body[200];
        copy_str(body, (int)sizeof body, line);
        int L = (int)strlen(body);
        while (L > 0 && (body[L - 1] == '\r' || body[L - 1] == ' ')) body[--L] = '\0';
        line = next;
        char* hash = strstr(body, " #");
        if (hash) *hash = '\0';
        char cond[64] = "";
        char* c = strstr(body, " if ");
        const bool has_if = c != nullptr;
        if (c) { *c = '\0'; copy_str(cond, (int)sizeof cond, skip_ws(c + 4)); }
        char w[16], t[20][16];
        const char* r = next_token(body, w, sizeof w);
        int nt = 0;
        while (nt < 20) { r = next_token(r, t[nt], 16); if (!t[nt][0]) break; ++nt; }
        if (!strcmp(w, "entity") && nt >= 6) {
            if (m.nents >= MAX_ENTITIES) return fail("too many entities");
            const int i = m.nents++;
            copy_str(m.ent_id[i], 16, t[0]);
            m.ent_npc[i] = !strcmp(t[1], "npc");
            if (m.ent_npc[i] && nt >= 8 && !strcmp(t[6], "talk")) copy_str(m.ent_talk[i], 16, t[7]);
            for (int k = 6; k + 1 < nt; ++k) if (!strcmp(t[k], "win")) copy_str(m.ent_win[i], 16, t[k + 1]);
            if (has_if) copy_str(m.ent_cond[i], 64, cond);
        } else if (!strcmp(w, "trigger") && nt >= 6) {
            if (m.ntrig >= MAX_TRIGGERS) return fail("too many triggers");
            copy_str(m.trig_script[m.ntrig], 16, t[5]);
            if (has_if) copy_str(m.trig_cond[m.ntrig], 64, cond);
            ++m.ntrig;
        } else if (!strcmp(w, "exit") && nt >= 9) {
            if (m.nexits >= MAX_EXITS) return fail("too many exits");
            if (script_map_find(t[5]) < 0) return fail("exit: the destination is not a map of maps.txt", t[5]);
            if (has_if) copy_str(m.exit_cond[m.nexits], 64, cond);
            ++m.nexits;
        } else if (!strcmp(w, "glow") && nt >= 4) {
            if (m.nglows >= MAX_GLOWS) return fail("too many glows");
            if (has_if) copy_str(m.glow_cond[m.nglows], 64, cond);
            ++m.nglows;
        } else if (!strcmp(w, "shot") && nt >= 1) {
            if (m.nshots >= MAX_SHOTS) return fail("too many shots");
            copy_str(g_pool.shots[mi][m.nshots++], 16, t[0]);
        } else if (!strcmp(w, "light") && nt >= 1) {
            if (light_find(t[0]) < 0) return fail("light: unknown preset (lights.txt)", t[0]);
        }
    }
    return true;
}

// maps.txt: `map <name>` (in map id order), `scripts <file>` (extra script files). A map that is not listed
// (test_field) makes a one-map table of its own.
static bool load_maps(const char* current) {
    g_pool.nmaps = 0; g_s.nfiles = 0;
    static char buf[1024];
    g_s.file = "maps.txt"; g_s.line = 0;
    if (eng::file_read("/rd/maps/maps.txt", buf, sizeof buf) >= 0) {
        int line_no = 0;
        for (char* line = buf; line && *line;) {
            char* next = strchr(line, '\n');
            if (next) *next++ = '\0';
            g_s.line = ++line_no;
            char* hash = strchr(line, '#');
            if (hash) *hash = '\0';
            char w[16], name[24], extra[8];
            const char* r = next_token(line, w, sizeof w);
            r = next_token(r, name, sizeof name);
            next_token(r, extra, sizeof extra);
            line = next;
            if (!w[0] || w[0] == '\r') continue;
            int L = (int)strlen(name);
            while (L > 0 && name[L - 1] == '\r') name[--L] = '\0';
            if (!name[0] || strlen(name) > 15 || (extra[0] && extra[0] != '\r')) return fail("map <name> | scripts <file>");
            if (!strcmp(w, "map")) {
                if (g_pool.nmaps >= MAX_MAPS) return fail("too many maps (4)");
                if (script_map_find(name) >= 0) return fail("duplicate map", name);
                copy_str(g_pool.maps[g_pool.nmaps++], 16, name);
            } else if (!strcmp(w, "scripts")) {
                if (g_s.nfiles >= SCR_MAX_FILES - MAX_MAPS) return fail("too many script files");
                copy_str(g_s.files[g_s.nfiles], 24, name);
                g_s.file_maps[g_s.nfiles++] = -1;
            } else return fail("map <name> | scripts <file>", w);
        }
    }
    if (script_map_find(current) < 0) {                 // not a map of the game: its own script file only
        g_pool.nmaps = 1; g_s.nfiles = 0;
        copy_str(g_pool.maps[0], 16, current);
    }
    // the maps' own script files come first, in map order
    for (int k = g_s.nfiles - 1; k >= 0; --k) { memcpy(g_s.files[k + g_pool.nmaps], g_s.files[k], 24); g_s.file_maps[k + g_pool.nmaps] = -1; }
    for (int m = 0; m < g_pool.nmaps; ++m) { copy_str(g_s.files[m], 24, g_pool.maps[m]); g_s.file_maps[m] = m; }
    g_s.nfiles += g_pool.nmaps;
    for (int m = 0; m < g_pool.nmaps; ++m) if (!scan_map(m)) return false;
    return true;
}

static bool link_cond(const char* cond, uint16_t* off, uint8_t* len, const char* who) {
    *len = 0;
    if (!cond[0]) return true;
    int n = 0;
    const int o = compile_cond(cond, &n);
    if (o < 0) return fail("bad 'if' condition", who);
    *off = (uint16_t)o; *len = (uint8_t)n;
    return true;
}

// Every map's entities, triggers and exits against the compiled scripts; the actor slots bound to each map.
static bool link_maps() {
    for (int mi = 0; mi < g_pool.nmaps; ++mi) {
        const MapScan& m = g_s.scan[mi];
        MapLinks& L = g_pool.links[mi];
        static char fname[32];
        snprintf(fname, sizeof fname, "maps/%s.txt", g_pool.maps[mi]);
        g_s.file = fname; g_s.line = 0;
        for (int i = 0; i < MAX_ENTITIES; ++i) { L.ent_talk[i] = L.ent_win[i] = -1; L.ent_cond_len[i] = 0; }
        for (int i = 0; i < MAX_TRIGGERS; ++i) { L.trig_script[i] = -1; L.trig_cond_len[i] = 0; }
        for (int i = 0; i < MAX_EXITS; ++i) L.exit_cond_len[i] = 0;
        for (int i = 0; i < MAX_GLOWS; ++i) L.glow_cond_len[i] = 0;
        char who[40];
        for (int i = 0; i < m.nents; ++i) {
            snprintf(who, sizeof who, "entity %s", m.ent_id[i]);
            if (m.ent_talk[i][0]) {
                L.ent_talk[i] = (int16_t)script_find(m.ent_talk[i]);
                if (L.ent_talk[i] < 0) return fail("talk: unknown script", who);
            }
            if (m.ent_win[i][0]) {
                L.ent_win[i] = (int16_t)flag_find(m.ent_win[i]);
                if (L.ent_win[i] < 0) return fail("win: flag not declared in @flags", who);
            }
            if (!link_cond(m.ent_cond[i], &L.ent_cond[i], &L.ent_cond_len[i], who)) return false;
        }
        for (int i = 0; i < m.ntrig; ++i) {
            snprintf(who, sizeof who, "trigger %d", i);
            L.trig_script[i] = (int16_t)script_find(m.trig_script[i]);
            if (L.trig_script[i] < 0) return fail("trigger: unknown script", m.trig_script[i]);
            if (!link_cond(m.trig_cond[i], &L.trig_cond[i], &L.trig_cond_len[i], who)) return false;
        }
        for (int i = 0; i < m.nexits; ++i) {
            snprintf(who, sizeof who, "exit %d", i);
            if (!link_cond(m.exit_cond[i], &L.exit_cond[i], &L.exit_cond_len[i], who)) return false;
        }
        for (int i = 0; i < m.nglows; ++i) {
            snprintf(who, sizeof who, "glow %d", i);
            if (!link_cond(m.glow_cond[i], &L.glow_cond[i], &L.glow_cond_len[i], who)) return false;
        }
        for (int k = 0; k < SCR_MAX_ACTORS; ++k) L.bind[k] = (int8_t)(k > 0 && k < g_pool.nactors ? scan_entity(mi, g_pool.actors[k]) : -1);
    }
    return true;
}

static const MapLinks& cur_links() { return g_pool.links[g_map >= 0 && g_map < MAX_MAPS ? g_map : 0]; }
int script_entity_talk(int i) { return (i >= 0 && i < MAX_ENTITIES) ? cur_links().ent_talk[i] : -1; }
int script_entity_win_flag(int i) { return (i >= 0 && i < MAX_ENTITIES) ? cur_links().ent_win[i] : -1; }
bool script_entity_visible(int i, const Progress& p) {
    if (i < 0 || i >= MAX_ENTITIES || !cur_links().ent_cond_len[i]) return true;
    return cond_eval(g_pool.cond + cur_links().ent_cond[i], cur_links().ent_cond_len[i], p) != 0;
}
int script_trigger_script(int i) { return (i >= 0 && i < MAX_TRIGGERS) ? cur_links().trig_script[i] : -1; }
bool script_trigger_active(int i, const Progress& p) {
    if (i < 0 || i >= MAX_TRIGGERS) return false;
    if (!cur_links().trig_cond_len[i]) return true;
    return cond_eval(g_pool.cond + cur_links().trig_cond[i], cur_links().trig_cond_len[i], p) != 0;
}
bool script_exit_open(int i, const Progress& p) {
    if (i < 0 || i >= MAX_EXITS) return false;
    if (!cur_links().exit_cond_len[i]) return true;
    return cond_eval(g_pool.cond + cur_links().exit_cond[i], cur_links().exit_cond_len[i], p) != 0;
}
bool script_glow_on(int i, const Progress& p) {
    if (i < 0 || i >= MAX_GLOWS) return false;
    if (!cur_links().glow_cond_len[i]) return true;
    return cond_eval(g_pool.cond + cur_links().glow_cond[i], cur_links().glow_cond_len[i], p) != 0;
}
bool script_marker_here(const Marker& m) { return m.map == g_map; }
int  script_map_count() { return g_pool.nmaps; }
int  script_map_find(const char* name) {
    for (int i = 0; i < g_pool.nmaps; ++i) if (!strcmp(g_pool.maps[i], name)) return i;
    return -1;
}
const char* script_map_name(int m) { return (m >= 0 && m < g_pool.nmaps) ? g_pool.maps[m] : "?"; }
void script_set_map(int m) { g_map = (m >= 0 && m < MAX_MAPS) ? m : 0; }
int  script_current_map() { return g_map; }
int  script_actor_entity(int slot) { return (slot > 0 && slot < SCR_MAX_ACTORS) ? cur_links().bind[slot] : -1; }
int  script_actor_slot(const char* name) {
    if (!strcmp(name, "rowan")) return 0;
    for (int k = 1; k < g_pool.nactors; ++k) if (!strcmp(g_pool.actors[k], name)) return k;
    return -1;
}
const char* script_actor_name(int slot) { return (slot >= 0 && slot < g_pool.nactors) ? g_pool.actors[slot] : "?"; }

// Pass 1 over a file: its @flags (every file's flags form one list, before anything is compiled).
static bool file_flags(char** lines, int nl) {
    bool seen = false;
    for (int i = 0; i < nl; ++i) {
        const char* s = skip_ws(lines[i]);
        if (strncmp(s, "@flags", 6) || (s[6] && !is_space(s[6]))) continue;
        g_s.line = i + 1;
        if (seen) return fail("@flags declared twice");
        seen = true;
        s += 6;
        char tok[24];
        for (s = next_token(s, tok, sizeof tok); tok[0]; s = next_token(s, tok, sizeof tok)) {
            if (strlen(tok) > 15) return fail("flag names are 15 characters at most", tok);
            for (const char* c = tok; *c; ++c) if (!is_ident_char(*c) || (tok[0] >= '0' && tok[0] <= '9')) return fail("bad flag name", tok);
            if (!strcmp(tok, "stage") || !strcmp(tok, "potions") || !strcmp(tok, "max_hp")) return fail("reserved name", tok);
            if (flag_find(tok) >= 0) return fail("duplicate flag", tok);
            if (g_pool.nflags >= MAX_FLAGS) return fail("more than 32 flags");
            copy_str(g_pool.flags[g_pool.nflags++], 16, tok);
        }
    }
    return true;
}

static bool read_file(int k, char*** lines_out, int* nl_out, bool* exists) {
    char path[64];
    snprintf(path, sizeof path, "/rd/scripts/%s.txt", g_s.files[k]);
    static char fname[32];
    snprintf(fname, sizeof fname, "%s.txt", g_s.files[k]);
    g_s.file = fname; g_s.line = 0;
    *exists = eng::file_exists(path);
    if (!*exists) return true;
    const int n = eng::file_read(path, g_s.src, SRC_BYTES);
    if (n < 0) return fail("script file missing or larger than 16 KiB");
    static char* lines[SCR_MAX_LINES];
    const int nl = split_lines(g_s.src, lines, SCR_MAX_LINES);
    if (nl < 0) return fail("more than 2048 lines");
    *lines_out = lines; *nl_out = nl;
    return true;
}

bool script_load(const char* map_name) {
    memset(&g_pool, 0, sizeof g_pool);
    memset(&g_s, 0, sizeof g_s);
    g_err[0] = '\0';
    g_loaded = false;
    g_pool.nobjectives = 1;                      // id 0 = no objective
    g_pool.nactors = 1;                          // slot 0 = rowan
    copy_str(g_pool.actors[0], 16, "rowan");
    for (int m = 0; m < MAX_MAPS; ++m) {
        for (int i = 0; i < MAX_ENTITIES; ++i) { g_pool.links[m].ent_talk[i] = -1; g_pool.links[m].ent_win[i] = -1; }
        for (int i = 0; i < MAX_TRIGGERS; ++i) g_pool.links[m].trig_script[i] = -1;
        for (int k = 0; k < SCR_MAX_ACTORS; ++k) g_pool.links[m].bind[k] = -1;
    }
    g_map = 0;
    if (!load_maps(map_name)) return false;
    // which script files exist
    bool any = false;
    for (int k = 0; k < g_s.nfiles; ++k) {
        char path[64];
        snprintf(path, sizeof path, "/rd/scripts/%s.txt", g_s.files[k]);
        any = any || eng::file_exists(path);
    }
    static char fname[32];
    snprintf(fname, sizeof fname, "%s.txt", map_name);
    if (!any) {                                  // no quest on this map (test_field): nothing may need one
        progress_bind_flags(g_pool.flags, 0);
        const MapScan& m = g_s.scan[0];
        for (int i = 0; i < m.nents; ++i)
            if (m.ent_talk[i][0] || m.ent_win[i][0] || m.ent_cond[i][0]) { g_s.file = fname; return fail("the map uses talk / win / if but has no script file", m.ent_id[i]); }
        if (m.ntrig || m.nexits) { g_s.file = fname; return fail("the map has triggers or exits but no script file"); }
        script_set_map(script_map_find(map_name));
        return true;
    }
    if (!text_ready()) { g_s.file = fname; return fail("the dialog font is needed to compile scripts (page fit)"); }
    if (!load_speakers()) return false;
    // pass 1: every file's @flags
    for (int k = 0; k < g_s.nfiles; ++k) {
        char** lines = nullptr; int nl = 0; bool exists = false;
        if (!read_file(k, &lines, &nl, &exists)) return false;
        if (exists && !file_flags(lines, nl)) return false;
    }
    // pass 2: everything else, file by file
    static char logical[512];
    for (int k = 0; k < g_s.nfiles; ++k) {
        char** lines = nullptr; int nl = 0; bool exists = false;
        if (!read_file(k, &lines, &nl, &exists)) return false;
        if (!exists) continue;
        g_s.file_map = g_s.cur_map = g_s.file_maps[k];
        for (int i = 0; i < nl;) {
            int line_no;
            i = logical_line(lines, nl, i, logical, sizeof logical, &line_no);
            g_s.line = line_no;
            if (!logical[0]) continue;
            if (logical[0] == '|') return fail("a '|' continuation line must follow a choice");
            if (!stmt(logical)) return false;
        }
        g_s.line = nl;
        if (!close_script()) return false;
    }
    // label and call fixups
    for (int k = 0; k < g_s.nfix; ++k) {
        const Fixup& f = g_s.fix[k];
        g_s.line = f.line;
        int t = -1;
        if (f.call) {
            t = script_find(f.name);
            if (t < 0) return fail("call: unknown script", f.name);
            g_pool.ops[f.op].a = (uint8_t)t;
        } else {
            for (int i = 0; i < g_s.nlabels; ++i) if (!strcmp(g_s.labels[i].name, f.name)) t = g_s.labels[i].pc;
            if (t < 0) return fail("unknown label", f.name);
            g_pool.ops[f.op].b = (uint16_t)t;
        }
    }
    for (int i = 0; i < g_pool.nscripts; ++i) {
        const ScriptDef& d = g_pool.scripts[i];
        for (int pc = d.first; pc < d.end; ++pc) {
            const ScrOp& o = g_pool.ops[pc];
            const bool jump = o.code == SC_JMP || o.code == SC_JZ || o.code == SC_OPTION;
            if (jump && (o.b < d.first || o.b > d.end)) { g_s.line = 0; char m[48]; snprintf(m, sizeof m, "%s", d.name); return fail("a label jumps into another script", m); }
        }
        g_s.line = 0;
        if (!check_fall_off(d)) return false;
    }
    if (!link_maps()) return false;
    progress_bind_flags(g_pool.flags, g_pool.nflags);
    script_set_map(script_map_find(map_name));
    g_loaded = true;
    char msg[200];
    const ScriptStats st = script_stats();
    snprintf(msg, sizeof msg, "script: %d files ok: %d scripts, %d ops, %d string bytes, %d condition bytes, %d flags, %d speakers, %d actors, %d maps; pools %d B static",
             g_s.nfiles, g_pool.nscripts, g_pool.nops, g_pool.nstr, g_pool.ncond, g_pool.nflags, g_pool.nspeakers, g_pool.nactors, g_pool.nmaps, st.pool_bytes);
    eng::dbg_print(msg);
    return true;
}

bool script_loaded() { return g_loaded; }
const char* script_error() { return g_err; }
int script_find(const char* name) {
    for (int i = 0; i < g_pool.nscripts; ++i) if (!strcmp(g_pool.scripts[i].name, name)) return i;
    return -1;
}
const ScriptDef& script_def(int i) { return g_pool.scripts[i]; }
int script_count() { return g_pool.nscripts; }
const ScrOp& script_op(int pc) { return g_pool.ops[pc]; }
const char* script_str(int off) { return (off >= 0 && off < g_pool.nstr) ? g_pool.str + off : ""; }
const Speaker& script_speaker(int i) { return g_pool.speakers[i]; }
int script_speaker_count() { return g_pool.nspeakers; }
int script_speaker_find(const char* id) {
    for (int i = 0; i < g_pool.nspeakers; ++i) if (!strcmp(g_pool.speakers[i].id, id)) return i;
    return -1;
}
const Marker* script_markers(int* n) { *n = g_pool.nmarkers; return g_pool.markers; }
const uint8_t* script_cond(int off) { return g_pool.cond + off; }
const char* script_objective(int id) { return (id > 0 && id < g_pool.nobjectives) ? g_pool.str + g_pool.objectives[id] : ""; }

ScriptStats script_stats() {
    ScriptStats s;
    s.ops = g_pool.nops; s.str_bytes = g_pool.nstr; s.cond_bytes = g_pool.ncond; s.scripts = g_pool.nscripts;
    s.labels = g_s.nlabels; s.pool_bytes = (int)sizeof(Pools); s.scratch_bytes = (int)sizeof(Scratch);
    return s;
}

// ---------------------------------------------------------------------------- dump
static void quoted(char* out, int cap, const char* s) {
    int n = 0;
    out[n++] = '"';
    for (; *s && n < cap - 4; ++s) {
        if (*s == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
        else if (*s == '"' || *s == '\\') { out[n++] = '\\'; out[n++] = *s; }
        else out[n++] = *s;
    }
    out[n++] = '"';
    out[n] = '\0';
}

static void hex(char* out, const uint8_t* b, int n) {
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < n; ++i) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
    out[2 * n] = '\0';
}

static const char* actor_name(int a) { return script_actor_name(a); }

void script_dump(void (*out)(const char*, void*), void* ctx) {
    char line[700], q[560], h[80];
    int n = snprintf(line, sizeof line, "flags %d", g_pool.nflags);
    for (int i = 0; i < g_pool.nflags; ++i) n += snprintf(line + n, sizeof line - (size_t)n, " %s", g_pool.flags[i]);
    out(line, ctx);
    for (int i = 0; i < g_pool.nspeakers; ++i) {
        const Speaker& s = g_pool.speakers[i];
        snprintf(line, sizeof line, "speaker %d %s \"%s\" %06X %s", i, s.id, s.name, (unsigned)s.rgb, s.portrait);
        out(line, ctx);
    }
    for (int i = 1; i < g_pool.nobjectives; ++i) {
        quoted(q, sizeof q, g_pool.str + g_pool.objectives[i]);
        snprintf(line, sizeof line, "objective %d %s", i, q);
        out(line, ctx);
    }
    for (int i = 0; i < g_pool.nmarkers; ++i) {
        const Marker& m = g_pool.markers[i];
        hex(h, g_pool.cond + m.cond, m.cond_len);
        snprintf(line, sizeof line, "marker %s %s %s %s", g_pool.maps[m.map], g_s.scan[m.map].ent_id[m.entity], m.kind == MARK_QUEST ? "!" : "...", h);
        out(line, ctx);
    }
    n = snprintf(line, sizeof line, "actors %d", g_pool.nactors);
    for (int i = 0; i < g_pool.nactors; ++i) n += snprintf(line + n, sizeof line - (size_t)n, " %s", g_pool.actors[i]);
    out(line, ctx);
    const anim::Library* L = anim::shared_library();
    for (int si = 0; si < g_pool.nscripts; ++si) {
        const ScriptDef& d = g_pool.scripts[si];
        snprintf(line, sizeof line, "script %d %s %d %d %s%s", si, d.name, d.first, d.end, g_pool.maps[d.map], d.skip ? " skip" : "");
        out(line, ctx);
        for (int pc = d.first; pc < d.end; ++pc) {
            const ScrOp& o = g_pool.ops[pc];
            n = snprintf(line, sizeof line, "%d %s", pc, OP_NAMES[o.code]);
            switch (o.code) {
            case SC_PAGE: quoted(q, sizeof q, g_pool.str + o.str);
                snprintf(line + n, sizeof line - (size_t)n, " %s %s %d %s", g_pool.speakers[o.a].id, VAR_NAMES[o.b], o.c, q); break;
            case SC_SYS: quoted(q, sizeof q, g_pool.str + o.str); snprintf(line + n, sizeof line - (size_t)n, " %d %s", o.c, q); break;
            case SC_CHOICE:
                if (o.b == NO_CANCEL) snprintf(line + n, sizeof line - (size_t)n, " %d cancel=-", o.a);
                else snprintf(line + n, sizeof line - (size_t)n, " %d cancel=%d", o.a, o.b);
                break;
            case SC_OPTION: quoted(q, sizeof q, g_pool.str + o.str); snprintf(line + n, sizeof line - (size_t)n, " -> %d %s", o.b, q); break;
            case SC_JMP: snprintf(line + n, sizeof line - (size_t)n, " %d", o.b); break;
            case SC_JZ: hex(h, g_pool.cond + o.c, o.a); snprintf(line + n, sizeof line - (size_t)n, " %d %s", o.b, h); break;
            case SC_CALL: snprintf(line + n, sizeof line - (size_t)n, " %s", g_pool.scripts[o.a].name); break;
            case SC_STAGE: case SC_MAXHP: case SC_GIVE_POTIONS: case SC_TAKE_POTIONS: case SC_OBJECTIVE: case SC_CHECKPOINT:
                snprintf(line + n, sizeof line - (size_t)n, " %d", o.a); break;
            case SC_FLAG: case SC_UNFLAG: snprintf(line + n, sizeof line - (size_t)n, " %s", g_pool.flags[o.a]); break;
            case SC_FACE:
                if (o.b & 0x100) snprintf(line + n, sizeof line - (size_t)n, " %s %s", actor_name(o.a), actor_name(o.b & 0xFF));
                else snprintf(line + n, sizeof line - (size_t)n, " %s %s", actor_name(o.a), DIR_NAMES[o.b & 3]);
                break;
            case SC_ANIM: snprintf(line + n, sizeof line - (size_t)n, " %s %s", actor_name(o.a), L && o.b < L->num_clips ? L->clips[o.b].name : "?"); break;
            case SC_EMOTE: snprintf(line + n, sizeof line - (size_t)n, " %s %s", actor_name(o.a), EMOTE_NAMES[o.b % EMOTE_COUNT]); break;
            case SC_WAIT: case SC_FADE_OUT: case SC_FADE_IN: snprintf(line + n, sizeof line - (size_t)n, " %d", o.b); break;
            case SC_LETTERBOX: snprintf(line + n, sizeof line - (size_t)n, " %s", o.a ? "on" : "off"); break;
            case SC_MOVE:
                snprintf(line + n, sizeof line - (size_t)n, " %s %d %d%s%s", actor_name(o.a), (int)(int8_t)(o.b & 0xFF), (int)(int8_t)(o.b >> 8),
                         (o.c & MOVE_REL) ? " rel" : "", (o.c & MOVE_RUN) ? " run" : "");
                break;
            case SC_PLACE: snprintf(line + n, sizeof line - (size_t)n, " %s %d %d %s", actor_name(o.a), o.b & 0xFF, o.b >> 8, DIR_NAMES[o.c & 3]); break;
            case SC_CAM: snprintf(line + n, sizeof line - (size_t)n, " %s %d", o.a == CAM_FIELD ? "field" : g_pool.shots[o.c % MAX_MAPS][o.a % MAX_SHOTS], o.b); break;
            case SC_PAR: snprintf(line + n, sizeof line - (size_t)n, " %d", o.a); break;
            case SC_BATTLE: snprintf(line + n, sizeof line - (size_t)n, " %s%s", actor_name(o.a), o.b ? " boss" : ""); break;
            case SC_MAP:
                snprintf(line + n, sizeof line - (size_t)n, " %s %d %d %s %s", g_pool.maps[o.a % MAX_MAPS], o.b & 0xFF, o.b >> 8, DIR_NAMES[o.c & 3],
                         (o.c >> 4) ? light_get((o.c >> 4) - 1).name : "-");
                break;
            case SC_LIGHT: snprintf(line + n, sizeof line - (size_t)n, " %s", light_get(o.a).name); break;
            case SC_LIGHT_LERP: snprintf(line + n, sizeof line - (size_t)n, " %s %s %d", light_get(o.a).name, light_get(o.c).name, o.b); break;
            case SC_NAMECARD: case SC_CARD: quoted(q, sizeof q, g_pool.str + o.str); snprintf(line + n, sizeof line - (size_t)n, " %s %d", q, o.b); break;
            case SC_TRANSITION: snprintf(line + n, sizeof line - (size_t)n, " T%d", o.a); break;
            default: break;
            }
            out(line, ctx);
        }
    }
}

// ---------------------------------------------------------------------------- VM
struct Vm { int script, pc, block, timer, sp; int stack[SCR_STACK]; };
static Vm g_vm = { -1, 0, -1, 0, 0, { 0, 0, 0, 0 } };
static ScriptHost g_host = {};

void script_set_host(const ScriptHost& h) { g_host = h; }
bool script_running() { return g_vm.script >= 0; }
int  script_current() { return g_vm.script; }
int  script_blocking_pc() { return g_vm.block; }
int  script_timer() { return g_vm.timer; }
void script_abort() { g_vm.script = -1; g_vm.block = -1; g_vm.timer = 0; g_vm.sp = 0; }

static bool host_op(uint8_t code) {
    return code == SC_MOVE || code == SC_CAM || code == SC_PAR || code == SC_BATTLE || code == SC_MAP || code == SC_NAMECARD ||
           code == SC_CARD || code == SC_TRANSITION || code == SC_CREDITS || code == SC_RESULTS;
}
bool script_waiting_host() { return g_vm.script >= 0 && g_vm.block >= 0 && host_op(g_pool.ops[g_vm.block].code); }

static int script_at(int pc) {
    for (int i = 0; i < g_pool.nscripts; ++i) if (pc >= g_pool.scripts[i].first && pc < g_pool.scripts[i].end) return i;
    return -1;
}
bool script_skippable() {
    if (g_vm.script < 0) return false;
    const int s = script_at(g_vm.block >= 0 ? g_vm.block : g_vm.pc);
    return s >= 0 && g_pool.scripts[s].skip;
}

static void vm_error(const char* what) {
    char m[120];
    snprintf(m, sizeof m, "script: %s in %s at op %d - script stopped", what,
             g_vm.script >= 0 ? g_pool.scripts[g_vm.script].name : "?", g_vm.pc);
    eng::dbg_print(m);
    script_abort();
}

static void changed() { if (g_host.progress_changed) g_host.progress_changed(); }

// An actor slot as the host sees it: 0 = Rowan, 1 + i = entity i of the current map, -1 = not on this map.
static int host_actor(int slot) {
    if (slot == 0) return 0;
    const int e = script_actor_entity(slot);
    return e < 0 ? -1 : 1 + e;
}

// The actor commands that may also run inside a parallel line. Returns the ticks the command keeps the line busy
// by itself (a camera ease, a wait); moves report through the host's busy().
static int actor_cmd(const ScrOp& o) {
    switch (o.code) {
    case SC_FACE: {
        const int a = host_actor(o.a);
        if (a < 0 || !g_host.face) return 0;
        if (o.b & 0x100) { const int t = host_actor(o.b & 0xFF); if (t >= 0) g_host.face(a, 0x100 | t); }
        else g_host.face(a, o.b);
        return 0;
    }
    case SC_ANIM: { const int a = host_actor(o.a); if (a >= 0 && g_host.anim) g_host.anim(a, o.b); return 0; }
    case SC_EMOTE: { const int a = host_actor(o.a); if (a >= 0 && g_host.emote) g_host.emote(a, o.b); return 0; }
    case SC_MOVE: {
        const int a = host_actor(o.a);
        if (a >= 0 && g_host.move) g_host.move(a, (int)(int8_t)(o.b & 0xFF), (int)(int8_t)(o.b >> 8), o.c);
        return 0;
    }
    case SC_CAM:
        if (g_host.cam) g_host.cam(o.a == CAM_FIELD ? -1 : (o.c == g_map ? o.a : -1), o.b);
        return o.b;
    case SC_WAIT: return o.b;
    default: return 0;
    }
}

// Runs from g_vm.pc until a blocking op or the end.
static void vm_run() {
    for (int budget = 0; g_vm.script >= 0; ++budget) {
        if (budget >= SCR_RUN_LIMIT) { vm_error("no blocking command after 512 ops (a loop with no exit)"); return; }
        if (g_vm.pc < 0 || g_vm.pc >= g_pool.nops) { vm_error("pc out of range"); return; }
        const ScrOp& o = g_pool.ops[g_vm.pc];
        switch (o.code) {
        case SC_END:
            if (g_vm.sp > 0) { g_vm.pc = g_vm.stack[--g_vm.sp]; continue; }
            script_abort();
            return;
        case SC_PAGE: case SC_SYS: case SC_CHOICE:
            g_vm.block = g_vm.pc;
            return;
        case SC_WAIT:
            g_vm.block = g_vm.pc; g_vm.timer = o.b;
            return;
        case SC_FADE_OUT: case SC_FADE_IN:
            g_vm.block = g_vm.pc; g_vm.timer = o.b;
            if (g_host.fade) g_host.fade(o.code == SC_FADE_OUT, o.b);
            return;
        case SC_JMP: g_vm.pc = o.b; continue;
        case SC_JZ: g_vm.pc = cond_eval(g_pool.cond + o.c, o.a, progress()) ? g_vm.pc + 1 : o.b; continue;
        case SC_CALL:
            if (g_vm.sp >= SCR_STACK) { vm_error("call stack overflow (4 deep)"); return; }
            g_vm.stack[g_vm.sp++] = g_vm.pc + 1;
            g_vm.pc = g_pool.scripts[o.a].first;
            continue;
        case SC_OPTION: vm_error("ran into a choice option"); return;
        case SC_STAGE: progress_set_stage(o.a); changed(); break;
        case SC_MAXHP: progress().max_hp = o.a; changed(); break;
        case SC_FLAG: progress_set_flag(o.a, true); changed(); break;
        case SC_UNFLAG: progress_set_flag(o.a, false); changed(); break;
        case SC_GIVE_POTIONS: progress_give_potions(o.a); changed(); break;
        case SC_TAKE_POTIONS: progress_give_potions(-(int)o.a); changed(); break;
        case SC_HEAL: if (g_host.heal) g_host.heal(); break;
        case SC_CHECKPOINT: if (g_host.checkpoint) g_host.checkpoint(o.a); break;
        case SC_OBJECTIVE: progress().objective = o.a; changed(); break;
        case SC_FACE: case SC_ANIM: case SC_EMOTE: actor_cmd(o); break;
        case SC_LETTERBOX: if (g_host.letterbox) g_host.letterbox(o.a != 0); break;
        // ---- slice 3D-6
        case SC_PLACE: {
            const int a = host_actor(o.a);
            if (a >= 0 && g_host.place) g_host.place(a, o.b & 0xFF, o.b >> 8, o.c & 3);
            break;
        }
        case SC_MOVE: case SC_CAM: {
            const int t = actor_cmd(o);
            g_vm.block = g_vm.pc; g_vm.timer = t;
            return;
        }
        case SC_PAR: {
            int t = 0;
            for (int k = 1; k <= o.a; ++k) { const int tk = actor_cmd(g_pool.ops[g_vm.pc + k]); t = tk > t ? tk : t; }
            g_vm.block = g_vm.pc; g_vm.timer = t;
            return;
        }
        case SC_BATTLE: {
            const int a = host_actor(o.a);
            if (a <= 0 || !g_host.battle) { vm_error("battle: the enemy is not on this map"); return; }
            g_vm.block = g_vm.pc; g_vm.timer = 0;
            g_host.battle(a, o.b != 0);
            return;
        }
        case SC_MAP:
            g_vm.block = g_vm.pc; g_vm.timer = 0;
            if (g_host.map) g_host.map(o.a, o.b & 0xFF, o.b >> 8, o.c & 3, (int)(o.c >> 4) - 1);
            return;
        case SC_LIGHT: if (g_host.light) g_host.light(o.a); break;
        case SC_LIGHT_LERP: if (g_host.light_lerp) g_host.light_lerp(o.a, o.c, o.b); break;
        case SC_NAMECARD: case SC_CARD:
            g_vm.block = g_vm.pc; g_vm.timer = o.b;
            if (o.code == SC_NAMECARD && g_host.namecard) g_host.namecard(g_pool.str + o.str, o.b);
            if (o.code == SC_CARD && g_host.card) g_host.card(g_pool.str + o.str, o.b);
            return;
        case SC_TRANSITION:
            g_vm.block = g_vm.pc; g_vm.timer = 0;
            if (g_host.transition) g_host.transition(o.a);
            return;
        case SC_CREDITS: case SC_RESULTS:
            g_vm.block = g_vm.pc; g_vm.timer = 0;
            if (g_host.screen) g_host.screen(o.code == SC_CREDITS ? SCREEN_CREDITS : SCREEN_RESULTS);
            return;
        case SC_TITLE:
            script_abort();
            if (g_host.screen) g_host.screen(SCREEN_TITLE);
            return;
        default: vm_error("bad op"); return;
        }
        ++g_vm.pc;
    }
}

bool script_start(int script) {
    script_abort();
    if (script < 0 || script >= g_pool.nscripts) return false;
    g_vm.script = script;
    g_vm.pc = g_pool.scripts[script].first;
    vm_run();
    return g_vm.script >= 0;
}

void script_tick() {
    if (g_vm.script < 0 || g_vm.block < 0) return;
    const ScrOp& o = g_pool.ops[g_vm.block];
    if (host_op(o.code)) {
        if (g_vm.timer > 0) --g_vm.timer;
        if (g_vm.timer > 0 || (g_host.busy && g_host.busy())) return;
        g_vm.pc = g_vm.block + 1 + (o.code == SC_PAR ? o.a : 0); g_vm.block = -1; g_vm.timer = 0;
        vm_run();
        return;
    }
    if (o.code != SC_WAIT && o.code != SC_FADE_OUT && o.code != SC_FADE_IN) return;
    if (--g_vm.timer > 0) return;
    g_vm.pc = g_vm.block + 1; g_vm.block = -1; g_vm.timer = 0;
    vm_run();
}

void script_page_done() {
    if (g_vm.script < 0 || g_vm.block < 0) return;
    const uint8_t code = g_pool.ops[g_vm.block].code;
    if (code != SC_PAGE && code != SC_SYS) return;
    g_vm.pc = g_vm.block + 1; g_vm.block = -1;
    vm_run();
}

void script_choose(int option) {
    if (g_vm.script < 0 || g_vm.block < 0) return;
    const ScrOp& c = g_pool.ops[g_vm.block];
    if (c.code != SC_CHOICE || option < 0 || option >= c.a) return;
    g_vm.pc = g_pool.ops[g_vm.block + 1 + option].b; g_vm.block = -1;
    vm_run();
}

}
