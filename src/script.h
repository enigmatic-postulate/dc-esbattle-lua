// script.h - the script language (slice 3D-5, tasks C1/C2; GDD 7.4, 15.1; slice 3D-6: cutscenes across maps,
// docs/NEXT_SLICE_BARROW.md 5.4, 5.5): a compiler that turns every script file into fixed arrays once at boot (no
// heap, no parsing per frame), and a VM that runs one script at a time until a blocking command (a page, a choice,
// a wait, a fade; since slice 3D-6 also a move, a camera ease, a battle, a map change, a card, a transition, the
// credits and the results). tools/dialog/lint.py implements the same compiler in Python (its --dump must equal
// script_dump(): tests/dialog parity check) plus the GDD 7.4 validation.
//
// Files (slice 3D-6): romdisk/maps/maps.txt lists the maps (`map <name>`, the index is Progress.map) and extra
// script files (`scripts <file>`). Compiled in order: scripts/<map>.txt for every map that has one (its scripts run
// on that map), then each extra file (`@map <name>` says where its scripts start). A script's actors are checked
// against its map; a `map` command changes the map for the lines after it (the ending crosses maps). Actors are
// compiled to slots (0 = rowan, then every other name), bound to each map's entities at compile time.
//
// Commands: pages (speaker[(variant)]: text, sys:), choice, @if/@elif/@else/@end, :label, goto, end, call, set
// stage|max_hp, flag, unflag, give|take potions, give skill fireball, give token, heal, checkpoint [c1|c2|c3|rest],
// objective, face, anim, wait, emote, fade out|in, letterbox on|off; slice 3D-6: move, place, cam, `a | b` (parallel),
// battle, map, light, light lerp, namecard, card, transition, credits, results, title. Data directives: @flags,
// @marker, @map, @script <name> [skip] and (speakers.txt) @speaker.
#pragma once
#include <stdint.h>
#include "progress.h"

namespace bt {

enum ScCode : uint8_t {
    SC_END, SC_PAGE, SC_SYS, SC_CHOICE, SC_OPTION, SC_JMP, SC_JZ, SC_CALL, SC_STAGE, SC_MAXHP, SC_FLAG, SC_UNFLAG,
    SC_GIVE_POTIONS, SC_TAKE_POTIONS, SC_HEAL, SC_CHECKPOINT, SC_OBJECTIVE, SC_FACE, SC_ANIM, SC_WAIT, SC_EMOTE,
    SC_FADE_OUT, SC_FADE_IN, SC_LETTERBOX,
    // slice 3D-6
    SC_MOVE, SC_PLACE, SC_CAM, SC_PAR, SC_BATTLE, SC_MAP, SC_LIGHT, SC_LIGHT_LERP, SC_NAMECARD, SC_CARD, SC_TRANSITION,
    SC_CREDITS, SC_RESULTS, SC_TITLE, SC_COUNT
};
// 8 bytes. PAGE: a speaker, b variant, c glyphs, str text (pre-wrapped, '\n' between lines). SYS: c, str.
// CHOICE: a options, b cancel option (0xFF: none), followed by `a` OPTION ops (b target pc, str text).
// JMP: b target. JZ: b target when the condition is false, a condition bytes, c condition offset.
// CALL: a script. STAGE/MAXHP: a value. FLAG/UNFLAG: a flag. GIVE/TAKE_POTIONS: a count. CHECKPOINT: a id.
// OBJECTIVE: a objective id (0 = none). FACE: a actor, b direction (0..3) or 0x100 | actor. ANIM: a actor,
// b library clip. WAIT/FADE_*: b ticks. EMOTE: a actor, b kind. LETTERBOX: a on. Actors are slots (script_actor_*).
// Slice 3D-6: MOVE: a actor, b x | y << 8 (relative: int8 each), c MOVE_REL | MOVE_RUN. PLACE: a actor, b x | y << 8,
// c dir. CAM: a shot (0xFF: the field camera), b ticks, c the shot's map. PAR: a = n; the next n ops run together.
// BATTLE: a actor (an enemy of the map), b 1 = boss. MAP: a map, b x | y << 8, c dir | (preset + 1) << 4 (0: the map's
// own light). LIGHT: a preset. LIGHT_LERP: a from, c to, b ticks. NAMECARD/CARD: str text, b ticks.
// TRANSITION: a catalogue id (1..11 = T1..T11). CREDITS, RESULTS, TITLE: none.
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
const int SCR_MAX_ACTORS = 24;                   // actor slots (rowan + every entity name of every map)
const int SCR_MAX_FILES = 6;                     // script files (maps.txt)
const uint8_t MOVE_REL = 1, MOVE_RUN = 2;
const uint8_t CAM_FIELD = 0xFF;
const int NAMECARD_TICKS = 120, CARD_TICKS = 150;
enum ScreenKind { SCREEN_CREDITS, SCREEN_RESULTS, SCREEN_TITLE };

struct Speaker { char id[12]; char name[12]; uint32_t rgb; char portrait[12]; };
struct ScriptDef { char name[16]; uint16_t first, end; uint8_t map, skip; };   // map: where it starts; skip: Start skips it
struct Marker { int8_t entity; uint8_t kind, cond_len; uint8_t map; uint16_t cond; };

// Compiles every script file (maps.txt; + speakers.txt) against every map's entities, triggers, exits and shots
// (read from the map files). A map that is not in maps.txt (test_field) compiles only scripts/<map_name>.txt, if any;
// without one script_loaded() is false (a map without a quest). false on any error (logged with the file and line).
// Also binds the @flags to progress.h. Then script_set_map(the map's id).
bool script_load(const char* map_name);
// ---- maps (slice 3D-6)
int  script_map_count();
int  script_map_find(const char* name);          // -1 if not in the table
const char* script_map_name(int map);
void script_set_map(int map);                    // the loaded map: entity links, actor binding, markers
int  script_current_map();
int  script_actor_entity(int slot);              // slot -> entity index on the current map (-1: not there); slot 0: -1
int  script_actor_slot(const char* name);        // -1 if no script names it
const char* script_actor_name(int slot);
// the loaded map's triggers and exits (field.h order): script (-1: none), and whether the `if` holds
int  script_trigger_script(int trigger);
bool script_trigger_active(int trigger, const Progress& p);
bool script_exit_open(int exit, const Progress& p);
bool script_glow_on(int glow, const Progress& p);
bool script_marker_here(const Marker& m);        // the marker belongs to the current map
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
// Actors passed to the host are the map's: 0 = Rowan, 1 + i = entity i (a slot not on the current map is skipped).
struct ScriptHost {
    void (*heal)();
    void (*checkpoint)(int id);
    void (*face)(int actor, int dir_or_actor);
    void (*anim)(int actor, int clip);
    void (*emote)(int actor, int kind);
    void (*fade)(bool out, int ticks);
    void (*letterbox)(bool on);
    void (*progress_changed)();                  // after any Progress change (markers, conditions, HUD)
    // slice 3D-6 (cutscenes). busy(): something a blocking command started is still running (a move, a camera
    // ease, a battle, a map change, a card, a transition, the credits, the results) - the VM waits for it.
    void (*move)(int actor, int x, int y, int flags);
    void (*place)(int actor, int x, int y, int dir);
    void (*cam)(int shot, int ticks);            // shot: an index into the current map's shots; -1 = the field camera
    void (*battle)(int actor, bool boss);
    void (*map)(int map, int x, int y, int dir, int preset);   // preset -1: the map's own light
    void (*light)(int preset);
    void (*light_lerp)(int from, int to, int ticks);
    void (*namecard)(const char* text, int ticks);
    void (*card)(const char* text, int ticks);
    void (*transition)(int id);
    void (*screen)(int kind);                    // ScreenKind
    bool (*busy)();
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
bool script_skippable();                         // the running script was declared `@script <name> skip`
bool script_waiting_host();                      // blocked on a host activity (a cutscene command), not on the UI

}
