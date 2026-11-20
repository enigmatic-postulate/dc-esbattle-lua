// audio.h - the audio logic (slice 3D-7, docs/NEXT_SLICE_SOUND_SAVE.md 4.6, 5.1, 5.2, 6.1): the cue table
// (/rd/audio/cues.txt), the music state machine (one stream: fades, ducking, stings) and the per-frame effect queue
// (dedupe, rate limits, 8 voices by priority). Pure logic, deterministic, integer only: it reads ticks and event
// names and produces backend commands (eng::snd_*). It never writes game state and never allocates.
//
//   game thread, per displayed frame:
//     for each game tick (0..4):  game_tick() -> audio_tick(); eng::audio_event(name) ... (queued, no backend call)
//     audio_frame()               at most AUDIO_MAX_CMDS backend commands
#pragma once
#include <stdint.h>
namespace bt {

const int AUDIO_VOICES   = 8;      // effect voices (plan 4.6.3)
const int AUDIO_MAX_CMDS = 8;      // backend commands per displayed frame (plan 5.2)
const int AUDIO_FADE     = 20;     // ticks: a music cue's fade out (of the track before it) / in, unless cues.txt gives its own

struct AudioOptions { int music, effects; bool mono; };   // volumes 0..10 (plan 4.3)

// Boot: reads the cue table and every sound's header, then eng::snd_boot. false (one log line; every call below
// becomes a no-op) if there is no /rd/audio/cues.txt, or it has an error, or the backend refuses.
bool audio_boot();
bool audio_ready();

// A named moment (what eng::audio_event forwards): an alias, "music_<cue>", "sting_<cue>" or an effect cue.
// pan 0..255 (128 = centre) applies to effects flagged pan_actor.
void audio_event(const char* name, int pan = 128);
// Music by cue name ("village"): fades the current track out over `out` ticks, then this one in over `in`. The
// track that is already playing is not restarted. A "once" cue (a sting) starts at once at full level.
void audio_music(const char* cue, int out, int in);
void audio_music_stop(int out);
// Ducking (dialog 60, pause 50, 100 = none): the music level eases to `percent` over `ticks`.
void audio_duck(int percent, int ticks);
void audio_set_options(const AudioOptions& o);
const AudioOptions& audio_options();

void audio_tick();        // once per game tick (fades, ducking, sting ends, rate limits)
void audio_frame();       // once per displayed frame: the queued effects and the music changes go to the backend

// ---- tests, reports
struct AudioStats { int cues_sfx, cues_music, aliases, unknown_events, dropped_gap, dropped_dupe, dropped_voices, dropped_cmds;
                    uint32_t sfx_bytes, music_bytes; };
const AudioStats& audio_stats();
const char* audio_music_name();    // the track on the stream ("" if none)
int  audio_music_level();          // 0..255 as last sent to the backend
// Effect / music cue names by backend id (the host's trace names them); "" out of range.
const char* audio_sfx_name(int id);
const char* audio_track_name(int id);
// Parses one cue table (the text of cues.txt) without touching files or the backend: the loader's error for the
// lint fixtures ("" if it is fine). The message format is shared with tools/audio/lint_cues.py.
const char* audio_check_table(char* text);

}
