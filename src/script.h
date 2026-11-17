// script.h - the script language (slice 3D-5, tasks C1/C2; GDD 7.4, 15.1): a compiler that turns
// romdisk/scripts/<map>.txt + speakers.txt into fixed arrays at load time (no heap, no parsing per
// frame), and a VM that runs one script at a time until a blocking command (a page, a choice, a wait,
// a fade). tools/dialog/lint.py implements the same compiler in Python (its --dump must equal
// script_dump(): tests/dialog parity check) plus the GDD 7.4 validation.
//
// Command set of this slice (docs/NEXT_SLICE_DIALOG.md §7.2): pages (speaker[(variant)]: text, sys:),
// choice, @if/@elif/@else/@end, :label, goto, end, call, set stage|max_hp, flag, unflag,
// give|take potions, give skill fireball, give token, heal, checkpoint [c1|c2|rest], objective,
// face, anim, wait, emote, fade out|in, letterbox on|off; the data directives @flags, @marker and
// (speakers.txt) @speaker. cam, move, battle, map, light, namecard, card, credits, results, title,
// transition and parallel "cmd | cmd" lines are rejected ("not in this slice").
#pragma once
#include <stdint.h>
#include "progress.h"

namespace bt {

enum ScCode : uint8_t {
    SC_END, SC_PAGE, SC_SYS, SC_CHOICE, SC_OPTION, SC_JMP, SC_JZ, SC_CALL, SC_STAGE, SC_MAXHP, SC_FLAG, SC_UNFLAG,
    SC_GIVE_POTIONS, SC_TAKE_POTIONS, SC_HEAL, SC_CHECKPOINT, SC_OBJECTIVE, SC_FACE, SC_ANIM, SC_WAIT, SC_EMOTE,
    SC_FADE_OUT, SC_FADE_IN, SC_LETTERBOX, SC_COUNT
};
// 8 bytes. PAGE: a speaker, b variant, c glyphs, str text (pre-wrapped, '\n' between lines). SYS: c, str.
// CHOICE: a options, b cancel option (0xFF: none), followed by `a` OPTION ops (b target pc, str text).
// JMP: b target. JZ: b target when the condition is false, a condition bytes, c condition offset.
// CALL: a script. STAGE/MAXHP: a value. FLAG/UNFLAG: a flag. GIVE/TAKE_POTIONS: a count. CHECKPOINT: a id.
// OBJECTIVE: a objective id (0 = none). FACE: a actor, b direction (0..3) or 0x100 | actor. ANIM: a actor,
// b library clip. WAIT/FADE_*: b ticks. EMOTE: a actor, b kind. LETTERBOX: a on. Actor 0 = rowan, 1 + i = entity i.
struct ScrOp { uint8_t code, a; uint16_t b, c, str; };
static_assert(sizeof(ScrOp) == 8, "ScrOp is 8 bytes");

enum Variant : uint8_t { VAR_NEUTRAL, VAR_HAPPY, VAR_WORRIED, VAR_COUNT };
enum Emote : uint8_t { EMOTE_EXCL, EMOTE_QUESTION, EMOTE_DOTS, EMOTE_SWEAT, EMOTE_ANGER, EMOTE_COUNT };
enum MarkerKind : uint8_t { MARK_QUEST, MARK_TALK };
const uint8_t NO_CANCEL = 0xFF;
const int PAGE_WIDTH = 420, PAGE_LINES = 3;      // GDD 7.1: 3 lines of at most 420 px
const int CHOICE_TEXT_MAX = 300;                 // px: an option must fit the widest choice box

const int SCR_MAX_SCRIPTS = 32, SCR_MAX_OPS = 1024, SCR_STR_BYTES = 12 * 1024, SCR_COND_BYTES = 1024;
const int SCR_MAX_SPEAKERS = 8, SCR_MAX_MARKERS = 8, SCR_MAX_OBJECTIVES = 32, SCR_MAX_LABELS = 128;
const int SCR_STACK = 4;                         // call depth
const int SCR_RUN_LIMIT = 512;                   // ops in one run without blocking: a loop with no exit

struct Speaker { char id[12]; char name[12]; uint32_t rgb; char portrait[12]; };
struct ScriptDef { char name[16]; uint16_t first, end; };
struct Marker { int8_t entity; uint8_t kind, cond_len; uint8_t pad; uint16_t cond; };

// Compiles /rd/scripts/<map_name>.txt (+ speakers.txt) against the loaded map (field_state(): actor
// names). No script file: true, and script_loaded() is false (a map without a quest, e.g. test_field).
// false on any error (logged with the file and line). Also binds the @flags to progress.h.
bool script_load(const char* map_name);
bool script_loaded();
const char* script_error();                      // the first compile error ("" if none)
int  script_find(const char* name);              // -1 if absent
const ScriptDef& script_def(int i);
int  script_count();
const ScrOp& script_op(int pc);
const char* script_str(int off);
const Speaker& script_speaker(int i);
int  script_speaker_count();
int  script_speaker_find(const char* id);        // -1 if absent
const Marker* script_markers(int* n);
const uint8_t* script_cond(int off);
const char* script_objective(int id);            // raw objective text ("" for 0 / unknown)
// Map links (resolved by script_load against field_state()): entity i's talk script and win flag
// (-1: none) and its `if` condition (true when it has none).
int  script_entity_talk(int entity);
int  script_entity_win_flag(int entity);
bool script_entity_visible(int entity, const Progress& p);
// Compiled pool use, for the budget report (bytes of the static pools and in use).
struct ScriptStats { int ops, str_bytes, cond_bytes, scripts, labels, pool_bytes, scratch_bytes; };
ScriptStats script_stats();
// The compiled form as text, one line at a time (tools/dialog/lint.py --dump prints the same).
void script_dump(void (*emit)(const char* line, void* ctx), void* ctx);

// ---------------------------------------------------------------------------- VM
// What the VM needs from the game (set by game.cpp; any may be null).
struct ScriptHost {
    void (*heal)();
    void (*checkpoint)(int id);
    void (*face)(int actor, int dir_or_actor);
    void (*anim)(int actor, int clip);
    void (*emote)(int actor, int kind);
    void (*fade)(bool out, int ticks);
    void (*letterbox)(bool on);
    void (*progress_changed)();                  // after any Progress change (markers, conditions, HUD)
};
void script_set_host(const ScriptHost& h);

// Starts a script (VM reset) and runs it until it blocks or ends. Returns true if it blocked (the game
// enters Dialog mode), false if it ran to its end within the tick (or did not start).
bool script_start(int script);
bool script_running();                           // started and not ended
int  script_current();                           // running script (-1: none)
int  script_blocking_pc();                       // the op the VM waits on (-1: none)
void script_tick();                              // one tick: wait / fade counters; resumes when they run out
void script_page_done();                         // the page (PAGE / SYS) was read: continue
void script_choose(int option);                  // the choice (CHOICE) was answered: jump
void script_abort();                             // stop without running anything else
int  script_timer();                             // ticks left of a wait / fade (0: none)

}
