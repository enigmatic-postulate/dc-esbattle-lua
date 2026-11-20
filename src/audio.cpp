// audio.cpp - see audio.h.
#include "audio.h"
#include "adpcm.h"
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace bt {

const int MAX_SFX = 48, MAX_MUSIC = 12, MAX_ALIAS = 24, MAX_SILENT = 16, QUEUE = 16;
const int FADE_ONE = 1 << 12;
enum CueFlags : uint8_t { CF_PAN_ACTOR = 1 };
enum Kind : uint8_t { K_NONE, K_SFX, K_MUSIC, K_SILENT };

struct SfxCue   { char name[24]; char path[56]; uint8_t vol, prio, flags; uint16_t gap; uint32_t ticks, last_tick; bool played; };
struct MusicCue { char name[24]; char path[56]; uint8_t vol; bool once; uint16_t fade_out, fade_in; uint32_t ticks; };
struct Alias    { char name[24]; Kind kind; uint8_t idx; };
struct Voice    { int cue; uint32_t start, end; };
struct Pending  { uint8_t cue; uint8_t pan; };

struct Table { SfxCue sfx[MAX_SFX]; MusicCue music[MAX_MUSIC]; Alias alias[MAX_ALIAS]; char silent[MAX_SILENT][24];
               int nsfx, nmusic, nalias, nsilent; };
static Table g_t;
static bool g_ready = false;
static AudioOptions g_opt = { 8, 10, false };                 // plan 4.3 defaults (mono: the platform's setting, Stage C)
static AudioStats g_stats;
static uint32_t g_now = 0;
static Voice g_voice[AUDIO_VOICES];
static Pending g_queue[QUEUE];
static int g_nqueue = 0;

struct Music {
    int cur; bool cur_once;                  // the track on the stream (-1: none)
    int fade, fade_step;                     // 0..FADE_ONE, per tick
    bool has_next; int next, next_in;        // after the fade out (-1: silence)
    uint32_t end_tick;                       // a sting's end
    int duck, duck_target, duck_step;        // percent x 16
    bool cmd_start;                          // the stream must (re)start or stop this frame
    int sent_vol;
};
static Music g_m;

static char g_err[160];

// ---------------------------------------------------------------------------- the cue table
static int find_sfx(const Table& t, const char* n)   { for (int i = 0; i < t.nsfx; ++i) if (strcmp(t.sfx[i].name, n) == 0) return i; return -1; }
static int find_music(const Table& t, const char* n) { for (int i = 0; i < t.nmusic; ++i) if (strcmp(t.music[i].name, n) == 0) return i; return -1; }
static bool name_taken(const Table& t, const char* n) {
    if (find_sfx(t, n) >= 0 || find_music(t, n) >= 0) return true;
    for (int i = 0; i < t.nalias; ++i) if (strcmp(t.alias[i].name, n) == 0) return true;
    for (int i = 0; i < t.nsilent; ++i) if (strcmp(t.silent[i], n) == 0) return true;
    return false;
}
static bool name_ok(const char* n) {
    const size_t len = strlen(n);
    if (len < 1 || len > 23) return false;
    for (const char* p = n; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    return true;
}
static bool to_int(const char* s, int lo, int hi, int* out) {
    char* e = nullptr;
    const long v = strtol(s, &e, 10);
    if (e == s || *e || v < lo || v > hi) return false;
    *out = (int)v;
    return true;
}

// Parses `text` (modified in place) into t. "" or "line N: message".
static const char* parse_table(char* text, Table* t) {
    memset(t, 0, sizeof *t);
    int ln = 0;
#define FAIL(...) do { char m_[120]; snprintf(m_, sizeof m_, __VA_ARGS__); snprintf(g_err, sizeof g_err, "line %d: %s", ln, m_); return g_err; } while (0)
    for (char* line = text; line && *line;) {
        char* nl = strchr(line, '\n');
        if (nl) *nl++ = '\0';
        ++ln;
        if (char* hash = strchr(line, '#')) *hash = '\0';
        char* tok[8]; int n = 0;
        for (char* p = strtok(line, " \t\r"); p && n < 8; p = strtok(nullptr, " \t\r")) tok[n++] = p;
        line = nl;
        if (n == 0) continue;
        if (n < 2) FAIL("'%s' needs a name", tok[0]);
        if (!name_ok(tok[1])) FAIL("bad name '%.30s' (a-z, 0-9, _; at most 23)", tok[1]);
        if (name_taken(*t, tok[1])) FAIL("'%s' is defined twice", tok[1]);
        if (strcmp(tok[0], "music") == 0) {
            int vol = 0, fo = AUDIO_FADE, fi = AUDIO_FADE;
            if (n != 5 && n != 7) FAIL("music: expected `music <name> loop|once <file> <vol> [<fade_out> <fade_in>]`");
            if (strcmp(tok[2], "loop") != 0 && strcmp(tok[2], "once") != 0) FAIL("music '%s': '%.20s' is not loop or once", tok[1], tok[2]);
            if (strlen(tok[3]) > 55) FAIL("music '%s': file name too long", tok[1]);
            if (!to_int(tok[4], 0, 255, &vol)) FAIL("music '%s': vol '%.12s' is not 0-255", tok[1], tok[4]);
            if (n == 7 && (!to_int(tok[5], 0, 600, &fo) || !to_int(tok[6], 0, 600, &fi))) FAIL("music '%s': fades '%.12s' '%.12s' are not 0-600 ticks", tok[1], tok[5], tok[6]);
            if (t->nmusic >= MAX_MUSIC) FAIL("more than %d music cues", MAX_MUSIC);
            MusicCue& c = t->music[t->nmusic++];
            strcpy(c.name, tok[1]); strcpy(c.path, tok[3]); c.vol = (uint8_t)vol; c.once = tok[2][0] == 'o'; c.fade_out = (uint16_t)fo; c.fade_in = (uint16_t)fi;
        } else if (strcmp(tok[0], "sfx") == 0) {
            int vol = 0, prio = 0, gap = 0;
            if (n != 6 && n != 7) FAIL("sfx: expected `sfx <name> <file> <vol> <prio> <min_gap> [pan_actor]`");
            if (strlen(tok[2]) > 55) FAIL("sfx '%s': file name too long", tok[1]);
            if (!to_int(tok[3], 0, 255, &vol)) FAIL("sfx '%s': vol '%.12s' is not 0-255", tok[1], tok[3]);
            if (!to_int(tok[4], 1, 9, &prio)) FAIL("sfx '%s': prio '%.12s' is not 1-9", tok[1], tok[4]);
            if (!to_int(tok[5], 0, 600, &gap)) FAIL("sfx '%s': min_gap '%.12s' is not 0-600", tok[1], tok[5]);
            if (n == 7 && strcmp(tok[6], "pan_actor") != 0) FAIL("sfx '%s': unknown flag '%.20s'", tok[1], tok[6]);
            if (t->nsfx >= MAX_SFX) FAIL("more than %d sfx cues", MAX_SFX);
            SfxCue& c = t->sfx[t->nsfx++];
            strcpy(c.name, tok[1]); strcpy(c.path, tok[2]); c.vol = (uint8_t)vol; c.prio = (uint8_t)prio; c.gap = (uint16_t)gap;
            c.flags = n == 7 ? CF_PAN_ACTOR : 0;
        } else if (strcmp(tok[0], "alias") == 0) {
            if (n != 3) FAIL("alias: expected `alias <event> <cue>`");
            const int s = find_sfx(*t, tok[2]), m = find_music(*t, tok[2]);
            if (s < 0 && m < 0) FAIL("alias '%s': no cue '%.30s' above it", tok[1], tok[2]);
            if (t->nalias >= MAX_ALIAS) FAIL("more than %d aliases", MAX_ALIAS);
            Alias& a = t->alias[t->nalias++];
            strcpy(a.name, tok[1]); a.kind = s >= 0 ? K_SFX : K_MUSIC; a.idx = (uint8_t)(s >= 0 ? s : m);
        } else if (strcmp(tok[0], "silent") == 0) {
            if (n != 2) FAIL("silent: expected `silent <event>`");
            if (t->nsilent >= MAX_SILENT) FAIL("more than %d silent events", MAX_SILENT);
            strcpy(t->silent[t->nsilent++], tok[1]);
        } else FAIL("unknown line type '%.20s' (music, sfx, alias, silent)", tok[0]);
    }
#undef FAIL
    return "";
}

const char* audio_check_table(char* text) {
    static Table scratch;
    return parse_table(text, &scratch);
}

// An event name -> what it plays.
static Kind resolve(const char* name, int* idx) {
    for (int i = 0; i < g_t.nalias; ++i) if (strcmp(g_t.alias[i].name, name) == 0) { *idx = g_t.alias[i].idx; return g_t.alias[i].kind; }
    for (int i = 0; i < g_t.nsilent; ++i) if (strcmp(g_t.silent[i], name) == 0) return K_SILENT;
    if (strncmp(name, "music_", 6) == 0 || strncmp(name, "sting_", 6) == 0) {
        const int m = find_music(g_t, name + 6);
        if (m >= 0 && g_t.music[m].once == (name[0] == 's')) { *idx = m; return K_MUSIC; }
        return K_NONE;
    }
    const int s = find_sfx(g_t, name);
    if (s >= 0) { *idx = s; return K_SFX; }
    return K_NONE;
}

// Reads a sound's header: its length in ticks (rounded up) and its bytes. false + a log line if it is not playable.
static bool read_header(const char* rel, bool sfx, uint32_t* ticks, uint32_t* bytes) {
    char path[96];
    snprintf(path, sizeof path, "/rd/audio/%s", rel);
    uint8_t h[adpcm::HEADER_BYTES];
    adpcm::Header hd;
    char msg[160];
    if (eng::file_read_at(path, 0, h, (int)sizeof h) != (int)sizeof h || !adpcm::parse_header(h, &hd)) {
        snprintf(msg, sizeof msg, "audio: %s is missing or not an .adp file", path);
        eng::dbg_print(msg);
        return false;
    }
    if (sfx && hd.samples > 65534u) {                    // an AICA channel's sample counter is 16 bits (plan 13.1)
        snprintf(msg, sizeof msg, "audio: %s has %lu samples; an effect holds at most 65534", path, (unsigned long)hd.samples);
        eng::dbg_print(msg);
        return false;
    }
    *ticks = (uint32_t)(((uint64_t)hd.samples * 60u + hd.rate - 1u) / hd.rate);
    *bytes = adpcm::data_bytes(hd);
    return true;
}

bool audio_ready() { return g_ready; }
const AudioStats& audio_stats() { return g_stats; }
const AudioOptions& audio_options() { return g_opt; }
const char* audio_sfx_name(int id) { return id >= 0 && id < g_t.nsfx ? g_t.sfx[id].name : ""; }
const char* audio_track_name(int id) { return id >= 0 && id < g_t.nmusic ? g_t.music[id].name : ""; }
const char* audio_music_name() { return g_ready && g_m.cur >= 0 ? g_t.music[g_m.cur].name : ""; }
int audio_music_level() { return g_m.sent_vol < 0 ? 0 : g_m.sent_vol; }

bool audio_boot() {
    g_ready = false;
    memset(&g_stats, 0, sizeof g_stats);
    memset(g_voice, 0, sizeof g_voice);
    for (Voice& v : g_voice) v.cue = -1;
    g_nqueue = 0; g_now = 0;
    g_m = Music{ -1, false, FADE_ONE, 0, false, -1, 0, 0, 100 * 16, 100 * 16, 0, false, -1 };
    static char text[8 * 1024];
    if (eng::file_read("/rd/audio/cues.txt", text, (int)sizeof text) < 0) {
        eng::boot_log("audio: no /rd/audio/cues.txt (or over 8 KiB): no sound");
        return false;
    }
    const char* err = parse_table(text, &g_t);
    if (err[0]) {
        char msg[200];
        snprintf(msg, sizeof msg, "audio: cues.txt %s", err);
        eng::dbg_print(msg);
        return false;
    }
    static const char* sfx_paths[MAX_SFX];
    static const char* music_paths[MAX_MUSIC];
    static char full[MAX_SFX + MAX_MUSIC][72];
    bool ok = true;
    for (int i = 0; i < g_t.nsfx; ++i) {
        uint32_t bytes = 0;
        ok = read_header(g_t.sfx[i].path, true, &g_t.sfx[i].ticks, &bytes) && ok;
        g_stats.sfx_bytes += bytes;
        snprintf(full[i], sizeof full[i], "/rd/audio/%s", g_t.sfx[i].path);
        sfx_paths[i] = full[i];
    }
    for (int i = 0; i < g_t.nmusic; ++i) {
        uint32_t bytes = 0;
        ok = read_header(g_t.music[i].path, false, &g_t.music[i].ticks, &bytes) && ok;
        g_stats.music_bytes += bytes;
        snprintf(full[MAX_SFX + i], sizeof full[0], "/rd/audio/%s", g_t.music[i].path);
        music_paths[i] = full[MAX_SFX + i];
    }
    if (!ok) return false;
    g_stats.cues_sfx = g_t.nsfx; g_stats.cues_music = g_t.nmusic; g_stats.aliases = g_t.nalias;
    const eng::SndBank bank = { g_t.nsfx, sfx_paths, g_t.nmusic, music_paths };
    if (!eng::snd_boot(bank)) { eng::boot_log("audio: the sound backend is off: no sound"); return false; }
    char msg[160];
    snprintf(msg, sizeof msg, "audio: %d effects (%lu KiB), %d tracks (%lu KiB), %d aliases", g_t.nsfx,
             (unsigned long)(g_stats.sfx_bytes / 1024), g_t.nmusic, (unsigned long)(g_stats.music_bytes / 1024), g_t.nalias);
    eng::boot_log(msg);
    g_ready = true;
    return true;
}

void audio_set_options(const AudioOptions& o) {
    g_opt.music = o.music < 0 ? 0 : (o.music > 10 ? 10 : o.music);
    g_opt.effects = o.effects < 0 ? 0 : (o.effects > 10 ? 10 : o.effects);
    g_opt.mono = o.mono;
}

// ---------------------------------------------------------------------------- music
static int ceil_div(int a, int b) { return (a + b - 1) / b; }

static void switch_now() {
    g_m.cur = g_m.next; g_m.cur_once = false; g_m.has_next = false;
    g_m.fade = (g_m.cur >= 0 && g_m.next_in > 0) ? 0 : FADE_ONE;
    g_m.fade_step = (g_m.cur >= 0 && g_m.next_in > 0) ? ceil_div(FADE_ONE, g_m.next_in) : 0;
    g_m.cmd_start = true;
}

static void music_request(int idx, int out, int in) {
    if (idx >= 0 && g_t.music[idx].once) {                       // a sting: at once, full level, no loop
        g_m.cur = idx; g_m.cur_once = true; g_m.has_next = false;
        g_m.fade = FADE_ONE; g_m.fade_step = 0;
        g_m.end_tick = g_now + g_t.music[idx].ticks;
        g_m.cmd_start = true;
        return;
    }
    if (idx == g_m.cur && !g_m.cur_once) {                       // already on the stream: keep it (back up if it was leaving)
        g_m.has_next = false;
        if (idx >= 0 && g_m.fade < FADE_ONE) g_m.fade_step = ceil_div(FADE_ONE, in > 0 ? in : 1);
        return;
    }
    g_m.next = idx; g_m.next_in = in; g_m.has_next = true;
    if (g_m.cur < 0 || out <= 0 || g_m.fade <= 0) switch_now();
    else g_m.fade_step = -ceil_div(FADE_ONE, out);
}

void audio_music(const char* cue, int out, int in) {
    if (!g_ready) return;
    const int m = find_music(g_t, cue);
    if (m < 0) { ++g_stats.unknown_events; return; }
    music_request(m, out, in);
}
void audio_music_stop(int out) { if (g_ready) music_request(-1, out, 0); }

void audio_duck(int percent, int ticks) {
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    g_m.duck_target = percent * 16;
    const int d = g_m.duck_target - g_m.duck;
    g_m.duck_step = ticks > 0 ? ceil_div(d < 0 ? -d : d, ticks) : 0;
    if (ticks <= 0 || d == 0) g_m.duck = g_m.duck_target;
}

static int group_gain(int level) { return level * level; }       // 0..10 -> 0..100, squared so the steps sound even

static int music_volume() {
    if (g_m.cur < 0) return 0;
    const long v = (long)g_t.music[g_m.cur].vol * g_m.fade / FADE_ONE * g_m.duck / (100 * 16) * group_gain(g_opt.music) / 100;
    return (int)(v > 255 ? 255 : v);
}

// ---------------------------------------------------------------------------- events, ticks, frames
void audio_event(const char* name, int pan) {
    if (!g_ready) return;
    int idx = 0;
    switch (resolve(name, &idx)) {
    case K_MUSIC:  music_request(idx, g_t.music[idx].fade_out, g_t.music[idx].fade_in); return;
    case K_SILENT: return;
    case K_NONE:   ++g_stats.unknown_events; return;
    case K_SFX:    break;
    }
    SfxCue& c = g_t.sfx[idx];
    if (c.played && g_now - c.last_tick < c.gap) { ++g_stats.dropped_gap; return; }
    for (int i = 0; i < g_nqueue; ++i) if (g_queue[i].cue == idx) { ++g_stats.dropped_dupe; return; }   // once per frame
    if (g_nqueue >= QUEUE) { ++g_stats.dropped_cmds; return; }
    c.played = true; c.last_tick = g_now;
    pan = pan < 0 ? 0 : (pan > 255 ? 255 : pan);
    g_queue[g_nqueue++] = Pending{ (uint8_t)idx, (uint8_t)((c.flags & CF_PAN_ACTOR) ? pan : 128) };
}

void audio_tick() {
    ++g_now;
    if (!g_ready) return;
    if (g_m.fade_step) {
        g_m.fade += g_m.fade_step;
        if (g_m.fade >= FADE_ONE) { g_m.fade = FADE_ONE; g_m.fade_step = 0; }
        if (g_m.fade <= 0) { g_m.fade = 0; g_m.fade_step = 0; if (g_m.has_next) switch_now(); }
    }
    if (g_m.cur_once && g_m.cur >= 0 && (int32_t)(g_now - g_m.end_tick) >= 0) {   // the sting is over: silence
        g_m.cur = -1; g_m.cur_once = false; g_m.cmd_start = true;
    }
    if (g_m.duck != g_m.duck_target) {
        const int d = g_m.duck_target - g_m.duck;
        const int s = g_m.duck_step > 0 ? g_m.duck_step : (d < 0 ? -d : d);
        g_m.duck += d > 0 ? (d < s ? d : s) : (-d < s ? d : -s);
    }
}

// A voice for cue c: a free one, else the oldest playing the same cue, else the oldest of a lower priority; -1: none.
static int pick_voice(int c) {
    int same = -1, lower = -1;
    for (int v = 0; v < AUDIO_VOICES; ++v) {
        const Voice& vc = g_voice[v];
        if (vc.cue < 0 || (int32_t)(g_now - vc.end) >= 0) return v;
        if (vc.cue == c) { if (same < 0 || (int32_t)(vc.start - g_voice[same].start) < 0) same = v; }
        else if (g_t.sfx[vc.cue].prio < g_t.sfx[c].prio) { if (lower < 0 || (int32_t)(vc.start - g_voice[lower].start) < 0) lower = v; }
    }
    return same >= 0 ? same : lower;
}

void audio_frame() {
    if (!g_ready) return;
    int cmds = 0;
    const int vol = music_volume();
    if (g_m.cmd_start) {
        g_m.cmd_start = false;
        eng::snd_music(g_m.cur, 0, vol, g_m.cur >= 0 && !g_m.cur_once);
        g_m.sent_vol = vol;
        ++cmds;
    } else if (g_m.cur >= 0 && vol != g_m.sent_vol) {
        eng::snd_music_volume(vol);
        g_m.sent_vol = vol;
        ++cmds;
    }
    // effects, the highest priority first (stable: equal priorities keep their order)
    for (int i = 1; i < g_nqueue; ++i) {
        const Pending p = g_queue[i];
        int j = i;
        while (j > 0 && g_t.sfx[g_queue[j - 1].cue].prio < g_t.sfx[p.cue].prio) { g_queue[j] = g_queue[j - 1]; --j; }
        g_queue[j] = p;
    }
    const int gain = group_gain(g_opt.effects);
    for (int i = 0; i < g_nqueue; ++i) {
        const Pending& p = g_queue[i];
        if (cmds >= AUDIO_MAX_CMDS) { ++g_stats.dropped_cmds; continue; }
        const int v = pick_voice(p.cue);
        if (v < 0) { ++g_stats.dropped_voices; continue; }
        const SfxCue& c = g_t.sfx[p.cue];
        g_voice[v] = Voice{ p.cue, g_now, g_now + c.ticks };
        eng::snd_play(v, p.cue, c.vol * gain / 100, g_opt.mono ? 128 : p.pan);
        ++cmds;
    }
    g_nqueue = 0;
}

}
