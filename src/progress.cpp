// progress.cpp - see progress.h
#include "progress.h"
#include <string.h>

namespace bt {

static Progress   g_p, g_cp;
static QuestFlags g_qf = { -1, -1, -1, -1, -1, -1, -1 };
static char       g_names[MAX_FLAGS][16];
static int        g_nflags = 0;

Progress& progress() { return g_p; }
const Progress& progress_checkpoint() { return g_cp; }
const QuestFlags& quest_flags() { return g_qf; }

void progress_bind_flags(const char (*names)[16], int n) {
    g_nflags = n < MAX_FLAGS ? n : MAX_FLAGS;
    for (int i = 0; i < g_nflags; ++i) { memcpy(g_names[i], names[i], 16); g_names[i][15] = '\0'; }
    g_qf.patrol_a = progress_flag_index("patrol_a");
    g_qf.patrol_b = progress_flag_index("patrol_b");
    g_qf.skill_fire = progress_flag_index("skill_fire");
    g_qf.has_token = progress_flag_index("has_token");
    g_qf.warden_down = progress_flag_index("warden_down");
    g_qf.gate_open = progress_flag_index("gate_open");
    g_qf.warden_seen = progress_flag_index("warden_seen");
}

int progress_flag_index(const char* name) {
    for (int i = 0; i < g_nflags; ++i)
        if (!strcmp(g_names[i], name)) return i;
    return -1;
}
const char* progress_flag_name(int i) { return (i >= 0 && i < g_nflags) ? g_names[i] : "?"; }
int progress_flag_count() { return g_nflags; }

void progress_new_game(int spawn_x, int spawn_y, int spawn_dir, int objective, int map) {
    memset(&g_p, 0, sizeof g_p);
    g_p.map = (uint8_t)map;
    g_p.stage = 0;
    g_p.potions = 0;
    g_p.max_hp = START_MAX_HP;
    g_p.objective = (uint8_t)objective;
    g_p.cp_x = (uint8_t)spawn_x; g_p.cp_y = (uint8_t)spawn_y; g_p.cp_dir = (uint8_t)spawn_dir; g_p.cp_id = 0;
    g_cp = g_p;                                      // C0
}

void checkpoint_save(int cp_id, int tile_x, int tile_y, int dir) {
    g_p.cp_x = (uint8_t)tile_x; g_p.cp_y = (uint8_t)tile_y; g_p.cp_dir = (uint8_t)dir; g_p.cp_id = (uint8_t)cp_id;
    g_cp = g_p;
}

void checkpoint_restore() { g_p = g_cp; }

bool progress_flag(int i) { return i >= 0 && i < MAX_FLAGS && ((g_p.flags >> i) & 1u); }
void progress_set_flag(int i, bool on) {
    if (i < 0 || i >= MAX_FLAGS) return;
    if (on) g_p.flags |= 1u << i; else g_p.flags &= ~(1u << i);
}
void progress_set_stage(int s) { if (s > g_p.stage && s <= 255) g_p.stage = (uint8_t)s; }
void progress_give_potions(int n) {
    int v = (int)g_p.potions + n;
    g_p.potions = (uint8_t)(v < 0 ? 0 : (v > MAX_POTIONS ? MAX_POTIONS : v));
}
bool progress_skill_fire() { return g_qf.skill_fire < 0 || progress_flag(g_qf.skill_fire); }
int  progress_patrols() { return (progress_flag(g_qf.patrol_a) ? 1 : 0) + (progress_flag(g_qf.patrol_b) ? 1 : 0); }

int cond_eval(const uint8_t* code, int len, const Progress& p) {
    int st[COND_MAX_BYTES];
    int sp = 0;
    for (int i = 0; i < len; ++i) {
        const uint8_t op = code[i];
        if (op == C_PUSH || op == C_FLAG) {
            if (i + 1 >= len || sp >= COND_MAX_BYTES) return 0;
            const uint8_t v = code[++i];
            st[sp++] = op == C_PUSH ? (int)v : (int)((p.flags >> (v & 31)) & 1u);
            continue;
        }
        if (op == C_STAGE || op == C_POTIONS || op == C_MAXHP) {
            if (sp >= COND_MAX_BYTES) return 0;
            st[sp++] = op == C_STAGE ? p.stage : (op == C_POTIONS ? p.potions : p.max_hp);
            continue;
        }
        if (op == C_NOT) { if (sp < 1) return 0; st[sp - 1] = !st[sp - 1]; continue; }
        if (sp < 2) return 0;
        const int b = st[--sp], a = st[sp - 1];
        int r = 0;
        switch (op) {
        case C_ADD: r = a + b; break;
        case C_SUB: r = a - b; break;
        case C_EQ:  r = a == b; break;
        case C_NE:  r = a != b; break;
        case C_LT:  r = a < b; break;
        case C_LE:  r = a <= b; break;
        case C_GT:  r = a > b; break;
        case C_GE:  r = a >= b; break;
        case C_AND: r = a && b; break;
        case C_OR:  r = a || b; break;
        default: return 0;
        }
        st[sp - 1] = r;
    }
    return sp == 1 ? st[0] : 0;
}

}
