// progress.h - quest state (slice 3D-5, task A1; GDD 10, 10.1, 15.4): the Progress struct, its flags,
// condition evaluation, the objective text and the RAM checkpoints. Pure logic: no rendering, no
// input, integers only (deterministic: float == double traces).
//
// One source of truth: the hero's max HP and potions live here (PartyState keeps the current HP and
// the stats); battles get them at battle_begin and give the potions back (game.cpp).
#pragma once
#include <stdint.h>

namespace bt {

struct Progress {                 // GDD 10.1; copied whole by checkpoints
    uint8_t  stage;               // 0..7 (this slice: 0, 1, 2, 3, 6); only moves forward
    uint8_t  potions;             // 0..3
    uint8_t  max_hp;              // 20, 30
    uint8_t  objective;           // objective id (script objective table; 0 = none)
    uint32_t flags;               // bit i = flag i of @flags (max 32)
    uint16_t defeated;            // bit i = map entity i beaten (field_defeat)
    uint8_t  cp_x, cp_y, cp_dir;  // hero tile and facing when the checkpoint was taken
    uint8_t  cp_id;               // 0 = C0 (new game), 1 = C1, 2 = C2, 9 = Brom's rest (traces)
};
static_assert(sizeof(Progress) <= 64, "GDD 10.1: Progress must stay under 64 bytes");

const int MAX_FLAGS = 32;
const int MAX_POTIONS = 3;
const int START_MAX_HP = 20;

// The flags the code itself reads (looked up by name in the script's @flags at boot; -1 = absent:
// a map without a quest script, e.g. test_field, has every skill learned and nothing to set).
struct QuestFlags { int patrol_a, patrol_b, skill_fire, has_token, warden_down; };

Progress&       progress();                          // the live state
const Progress& progress_checkpoint();               // the last checkpoint's copy
const QuestFlags& quest_flags();
void progress_bind_flags(const char (*names)[16], int n);   // from the compiled @flags (script.cpp)
int  progress_flag_index(const char* name);          // -1 if not declared
const char* progress_flag_name(int i);
int  progress_flag_count();

void progress_new_game(int spawn_x, int spawn_y, int spawn_dir, int objective);
void checkpoint_save(int cp_id, int tile_x, int tile_y, int dir);
void checkpoint_restore();                           // progress() = the checkpoint's copy

bool progress_flag(int i);                           // false for i < 0
void progress_set_flag(int i, bool on);
void progress_set_stage(int s);                      // forward only (a backwards set is ignored)
void progress_give_potions(int n);                   // capped at MAX_POTIONS, floored at 0 (take = negative)
bool progress_skill_fire();                          // Fire learned (true when the map has no skill_fire flag)
int  progress_patrols();                             // patrol_a + patrol_b (the objective's {patrols})

// Conditions (GDD 7.4): a small RPN byte code compiled by script.cpp (and tools/dialog/lint.py).
enum CondOp : uint8_t { C_PUSH = 1, C_STAGE, C_POTIONS, C_MAXHP, C_FLAG, C_NOT, C_ADD, C_SUB, C_EQ, C_NE, C_LT, C_LE,
                        C_GT, C_GE, C_AND, C_OR };
const int COND_MAX_BYTES = 32;
// Evaluates code[0..len) (C_PUSH and C_FLAG take one operand byte). Malformed code evaluates to 0.
int cond_eval(const uint8_t* code, int len, const Progress& p);

}
