// script.cpp - see script.h. The compiler runs once per map load; the VM per tick.
#include "script.h"
#include "anim.h"
#include "field.h"
#include "progress.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

namespace bt {

// ---------------------------------------------------------------------------- compiled pools (static)
struct Pools {
    ScrOp     ops[SCR_MAX_OPS];
    char      str[SCR_STR_BYTES];
    uint8_t   cond[SCR_COND_BYTES];
    ScriptDef scripts[SCR_MAX_SCRIPTS];
    Speaker   speakers[SCR_MAX_SPEAKERS];
    Marker    markers[SCR_MAX_MARKERS];
    uint16_t  objectives[SCR_MAX_OBJECTIVES];   // string offsets; [0] = none
    char      flags[MAX_FLAGS][16];
    int16_t   ent_talk[MAX_ENTITIES], ent_win[MAX_ENTITIES];   // map links: talk script, win flag (-1: none)
    uint16_t  ent_cond[MAX_ENTITIES];                          // `if` condition (length 0: always)
    uint8_t   ent_cond_len[MAX_ENTITIES];
    int nops, nstr, ncond, nscripts, nspeakers, nmarkers, nobjectives, nflags;
};
static Pools g_pool;
static bool  g_loaded = false;
static char  g_err[320] = "";

// ---------------------------------------------------------------------------- compile-time scratch
// Only used while compiling (load time); reported separately from the pools.
const int SRC_BYTES = 16 * 1024;
const int SCR_MAX_LINES = 2048;
const int MAX_FIXUPS = 256;
struct Label { char name[16]; int pc, line; };
struct Fixup { int op, line; char name[16]; bool call; };
struct IfFrame { int jz; int ends[12]; int nends; bool has_else; int line; };
struct Scratch {
    char    src[SRC_BYTES];
    Label   labels[SCR_MAX_LABELS];
    Fixup   fix[MAX_FIXUPS];
    IfFrame ifs[8];
    int     nlabels, nfix, nifs;
    const char* file;
    int     line;
};
static Scratch g_s;

static const char* const VAR_NAMES[VAR_COUNT] = { "neutral", "happy", "worried" };
static const char* const EMOTE_NAMES[EMOTE_COUNT] = { "!", "?", "...", "sweat", "anger" };
static const char* const DIR_NAMES[4] = { "down", "up", "left", "right" };
static const char* const OUT_OF_SCOPE[] = { "cam", "move", "battle", "map", "light", "namecard", "card", "credits",
                                            "results", "title", "transition" };
static const char* const OP_NAMES[SC_COUNT] = {
    "END", "PAGE", "SYS", "CHOICE", "OPTION", "JMP", "JZ", "CALL", "STAGE", "MAXHP", "FLAG", "UNFLAG", "GIVE_POTIONS",
    "TAKE_POTIONS", "HEAL", "CHECKPOINT", "OBJECTIVE", "FACE", "ANIM", "WAIT", "EMOTE", "FADE_OUT", "FADE_IN", "LETTERBOX" };

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

static int actor_find(const char* name) {
    if (!strcmp(name, "rowan")) return 0;
    const FieldState& f = field_state();
    for (int i = 0; i < f.ents.size(); ++i) if (!strcmp(f.ents[i].id, name)) return 1 + i;
    return -1;
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

static bool stmt_command(const char* line) {
    char w[24], a1[64], a2[64];
    const char* rest = next_token(line, w, sizeof w);
    for (const char* o : OUT_OF_SCOPE) if (!strcmp(w, o)) return fail("command not in this slice", w);
    if (!strcmp(w, "choice")) return stmt_choice(skip_ws(rest));
    if (strchr(line, '|')) return fail("parallel commands (cmd | cmd) are not in this slice");
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
        if (!strcmp(a1, "c1")) id = 1; else if (!strcmp(a1, "c2")) id = 2; else if (!strcmp(a1, "rest")) id = 9;
        else if (a1[0]) return fail("checkpoint [c1|c2|rest]", a1);
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
    return fail("unknown command", w);
}

static bool stmt(const char* line) {
    if (line[0] == '@') {
        char w[16];
        const char* rest = next_token(line, w, sizeof w);
        rest = skip_ws(rest);
        if (!strcmp(w, "@flags")) return true;                  // first pass
        if (!strcmp(w, "@speaker")) return fail("@speaker belongs in speakers.txt");
        if (!strcmp(w, "@marker")) {
            if (in_script()) return fail("@marker inside a script");
            char id[24], kind[8];
            rest = next_token(rest, id, sizeof id);
            rest = next_token(rest, kind, sizeof kind);
            const int a = actor_find(id);
            if (a <= 0 || field_state().ents[a - 1].kind != EntityKind::Npc) return fail("@marker: not an npc of the map", id);
            int k;
            if (!strcmp(kind, "!")) k = MARK_QUEST; else if (!strcmp(kind, "...")) k = MARK_TALK;
            else return fail("@marker kind must be ! or ...", kind);
            if (g_pool.nmarkers >= SCR_MAX_MARKERS) return fail("too many markers (8)");
            int len = 0;
            const int off = compile_cond(rest, &len);
            if (off < 0) return false;
            Marker& m = g_pool.markers[g_pool.nmarkers++];
            m.entity = (int8_t)(a - 1); m.kind = (uint8_t)k; m.cond_len = (uint8_t)len; m.pad = 0; m.cond = (uint16_t)off;
            return true;
        }
        if (!strcmp(w, "@script")) {
            if (!close_script()) return false;
            char name[24];
            rest = next_token(rest, name, sizeof name);
            if (!name[0] || *skip_ws(rest) || strlen(name) > 15) return fail("@script <name> (15 characters at most)");
            for (const char* c = name; *c; ++c) if (!is_ident_char(*c)) return fail("bad script name", name);
            if (script_find(name) >= 0) return fail("duplicate script", name);
            if (g_pool.nscripts >= SCR_MAX_SCRIPTS) return fail("too many scripts (32)");
            ScriptDef& d = g_pool.scripts[g_pool.nscripts++];
            copy_str(d.name, (int)sizeof d.name, name);
            d.first = (uint16_t)g_pool.nops; d.end = 0xFFFF;
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

// The map's entities against the compiled scripts: talk scripts, win flags, `if` conditions.
static bool link_map(const char* map_file) {
    const FieldState& f = field_state();
    g_s.file = map_file; g_s.line = 0;
    for (int i = 0; i < f.ents.size(); ++i) {
        const Entity& e = f.ents[i];
        g_pool.ent_talk[i] = -1; g_pool.ent_win[i] = -1; g_pool.ent_cond_len[i] = 0;
        char who[40];
        snprintf(who, sizeof who, "entity %s", e.id);
        if (e.talk[0]) {
            g_pool.ent_talk[i] = (int16_t)script_find(e.talk);
            if (g_pool.ent_talk[i] < 0) return fail("talk: unknown script", who);
        }
        if (e.win[0]) {
            g_pool.ent_win[i] = (int16_t)flag_find(e.win);
            if (g_pool.ent_win[i] < 0) return fail("win: flag not declared in @flags", who);
        }
        if (e.cond[0]) {
            int len = 0;
            const int off = compile_cond(e.cond, &len);
            if (off < 0) return fail("bad 'if' condition", who);
            g_pool.ent_cond[i] = (uint16_t)off; g_pool.ent_cond_len[i] = (uint8_t)len;
        }
    }
    return true;
}

int script_entity_talk(int i) { return (i >= 0 && i < MAX_ENTITIES) ? g_pool.ent_talk[i] : -1; }
int script_entity_win_flag(int i) { return (i >= 0 && i < MAX_ENTITIES) ? g_pool.ent_win[i] : -1; }
bool script_entity_visible(int i, const Progress& p) {
    if (i < 0 || i >= MAX_ENTITIES || !g_pool.ent_cond_len[i]) return true;
    return cond_eval(g_pool.cond + g_pool.ent_cond[i], g_pool.ent_cond_len[i], p) != 0;
}

bool script_load(const char* map_name) {
    memset(&g_pool, 0, sizeof g_pool);
    memset(&g_s, 0, sizeof g_s);
    g_err[0] = '\0';
    g_loaded = false;
    g_pool.nobjectives = 1;                      // id 0 = no objective
    for (int i = 0; i < MAX_ENTITIES; ++i) { g_pool.ent_talk[i] = -1; g_pool.ent_win[i] = -1; }
    char path[64];
    snprintf(path, sizeof path, "/rd/scripts/%s.txt", map_name);
    static char fname[32];
    snprintf(fname, sizeof fname, "%s.txt", map_name);
    if (!eng::file_exists(path)) {               // no quest on this map (test_field): nothing may need one
        progress_bind_flags(g_pool.flags, 0);
        for (const Entity& e : field_state().ents)
            if (e.talk[0] || e.win[0] || e.cond[0]) { g_s.file = fname; return fail("the map uses talk / win / if but has no script file", e.id); }
        return true;
    }
    if (!text_ready()) { g_s.file = fname; return fail("the dialog font is needed to compile scripts (page fit)"); }
    if (!load_speakers()) return false;
    g_s.file = fname; g_s.line = 0;
    const int n = eng::file_read(path, g_s.src, SRC_BYTES);
    if (n < 0) return fail("script file missing or larger than 16 KiB");
    static char* lines[SCR_MAX_LINES];
    const int nl = split_lines(g_s.src, lines, SCR_MAX_LINES);
    if (nl < 0) return fail("more than 2048 lines");
    // pass 1: @flags
    for (int i = 0; i < nl; ++i) {
        const char* s = skip_ws(lines[i]);
        if (strncmp(s, "@flags", 6) || (s[6] && !is_space(s[6]))) continue;
        g_s.line = i + 1;
        if (g_pool.nflags) return fail("@flags declared twice");
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
    // pass 2: everything else
    static char logical[512];
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
    static char mapf[40];
    snprintf(mapf, sizeof mapf, "maps/%s.txt", map_name);
    if (!link_map(mapf)) return false;
    progress_bind_flags(g_pool.flags, g_pool.nflags);
    g_loaded = true;
    char msg[160];
    const ScriptStats st = script_stats();
    snprintf(msg, sizeof msg, "script: %s ok: %d scripts, %d ops, %d string bytes, %d condition bytes, %d flags, %d speakers; pools %d B static",
             fname, g_pool.nscripts, g_pool.nops, g_pool.nstr, g_pool.ncond, g_pool.nflags, g_pool.nspeakers, st.pool_bytes);
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

static const char* actor_name(int a) {
    if (a == 0) return "rowan";
    const FieldState& f = field_state();
    return (a - 1 < f.ents.size()) ? f.ents[a - 1].id : "?";
}

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
        snprintf(line, sizeof line, "marker %s %s %s", actor_name(1 + m.entity), m.kind == MARK_QUEST ? "!" : "...", h);
        out(line, ctx);
    }
    const anim::Library* L = anim::shared_library();
    for (int si = 0; si < g_pool.nscripts; ++si) {
        const ScriptDef& d = g_pool.scripts[si];
        snprintf(line, sizeof line, "script %d %s %d %d", si, d.name, d.first, d.end);
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

static void vm_error(const char* what) {
    char m[120];
    snprintf(m, sizeof m, "script: %s in %s at op %d - script stopped", what,
             g_vm.script >= 0 ? g_pool.scripts[g_vm.script].name : "?", g_vm.pc);
    eng::dbg_print(m);
    script_abort();
}

static void changed() { if (g_host.progress_changed) g_host.progress_changed(); }

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
        case SC_FACE: if (g_host.face) g_host.face(o.a, o.b); break;
        case SC_ANIM: if (g_host.anim) g_host.anim(o.a, o.b); break;
        case SC_EMOTE: if (g_host.emote) g_host.emote(o.a, o.b); break;
        case SC_LETTERBOX: if (g_host.letterbox) g_host.letterbox(o.a != 0); break;
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
    const uint8_t code = g_pool.ops[g_vm.block].code;
    if (code != SC_WAIT && code != SC_FADE_OUT && code != SC_FADE_IN) return;
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
