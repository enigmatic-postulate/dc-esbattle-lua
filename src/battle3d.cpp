// battle3d.cpp - see battle3d.h. Tick side: actor views, the sequencer, camera rig, particles.
// Drawing lives in battle3d_draw.cpp; both share battle3d_internal.h.
#include "battle3d_internal.h"
#include "input.h"
#include "models.h"
#include "skills.h"
#include "status.h"
#include "targeting.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace bt {

using namespace r3d;

B3 g_b3;

// ---------------------------------------------------------------------------- tuning
float T_CHAR_SCALE   = 0.8f;    // every character, as on the field
float T_RUN_SPEED    = 6.5f;    // units per second when running in / back
float T_MELEE_GAP    = 1.25f;   // distance from the target where a melee swing starts
float T_CAM_RATE     = 0.10f;   // camera smoothing per tick (UI shots)
float T_CAM_RATE_SEQ = 0.14f;   // camera smoothing per tick during sequences
float T_SHAKE_MELEE  = 0.07f;
float T_SHAKE_BIG    = 0.16f;
float T_LEAN         = 0.25f;   // target-selection camera lean toward the target
float T_SHADOW_ALPHA = 120.0f;
float T_FAST         = 1.0f;    // 1 = holding A plays sequences at 2x (decision D4)
int   T_RECOVER      = 18;      // ticks after a melee impact before the attacker heads back
static const Tune3 TUNES[] = {
    { "char_scale", &T_CHAR_SCALE }, { "run_speed", &T_RUN_SPEED }, { "melee_gap", &T_MELEE_GAP },
    { "cam_rate", &T_CAM_RATE }, { "cam_rate_seq", &T_CAM_RATE_SEQ }, { "shake_melee", &T_SHAKE_MELEE },
    { "shake_big", &T_SHAKE_BIG }, { "lean", &T_LEAN }, { "shadow_alpha", &T_SHADOW_ALPHA }, { "fast", &T_FAST },
};

bool battle3d_tune(const char* name, float value) {
    for (const Tune3& t : TUNES)
        if (!strcmp(t.name, name)) { *t.value = value; return true; }
    return false;
}

// ---------------------------------------------------------------------------- looks
// model, weapons (right / left hand slot; nullptr = none, or merged into the model), clips.
// Clip names are library names (tools/battle3d/build_anims.py). atk[1] alternates with atk[0].
const KindDef KINDS[K_COUNT] = {
    //        name                model           scale  wpn_r           wpn_l            idle          hit      death         atk0      atk1       ranged  height
    { "HERO",             "knight",       1.0f, "w_sword",      "w_shield",      "idle_melee", "hit_b", "death_a",    "chop",   "slice_d", false, 2.25f },
    { "SKELETON",         "skel_warrior", 1.0f, nullptr,        "w_skel_shield", "skel_idle",  "hit_a", "skel_death", "slice_h","chop",    false, 2.35f },
    { "SKULL ROGUE",      "skel_rogue",   1.0f, "w_skel_blade", nullptr,         "skel_idle",  "hit_b", "skel_death", "stab",   "slice_d", false, 2.20f },
    { "BONE MAGE",        "skel_mage",    1.0f, nullptr,        nullptr,         "skel_idle",  "hit_a", "skel_death", "throw",  "throw",   true,  2.45f },
    { "THE BONE WARDEN",  "skel_mage",    1.5f, nullptr,        nullptr,         "idle_2h",    "hit_a", "skel_death", "chop_2h","chop_2h", false, 2.45f },
    { "RAISED SKELETON",  "skel_warrior", 1.0f, nullptr,        nullptr,         "skel_idle",  "hit_a", "skel_death", "slice_h","chop",    false, 2.35f },
};

static int kind_of(const Actor& a, const EncounterDef* enc, int enemy_i) {
    if (a.team == Team::Player) return K_HERO;
    EnemyLook look = LOOK_AUTO;
    if (enc && enemy_i >= 0 && enemy_i < enc->count) look = enc->enemies[enemy_i].look;
    switch (look) {
    case LOOK_WARRIOR: return K_WARRIOR;
    case LOOK_ROGUE:   return K_ROGUE;
    case LOOK_MAGE:    return K_MAGE;
    case LOOK_WARDEN:  return K_WARDEN;
    case LOOK_MINION:  return K_MINION;
    default:           return a.sheet == SHEET_GUARD ? K_MAGE : K_WARRIOR;
    }
}

// ---------------------------------------------------------------------------- helpers
float ease(float u) { u = u < 0 ? 0 : (u > 1 ? 1 : u); return u * u * (3.0f - 2.0f * u); }

static float yaw_to(Vec3 from, Vec3 to) {
    // yaw 0 faces +Z, yaw > 0 turns toward +X (mat_trs_y). Not a hot path: exact atan2.
    return atan2f(to.x - from.x, to.z - from.z);
}

uint32_t b3_rand() { g_b3.rng = g_b3.rng * 1664525u + 1013904223u; return g_b3.rng >> 8; }
float b3_randf(float lo, float hi) { return lo + (hi - lo) * (float)(b3_rand() & 0xFFFF) / 65535.0f; }

View* view_of(const Actor* a) {
    for (int i = 0; i < g_b3.num_views; ++i)
        if (g_b3.views[i].a == a) return &g_b3.views[i];
    return nullptr;
}

Vec3 view_chest(const View& v) { return v.pos + v3(0, 0.55f * KINDS[v.kind].height * v.scale, 0); }
Vec3 view_head(const View& v) { return v.pos + v3(0, KINDS[v.kind].height * v.scale + 0.15f, 0); }

static int clip(const char* name) { return anim::clip_find(g_b3.lib, name); }

static void view_play(View& v, int c, float fade, int then) {
    if (c < 0) return;
    anim::play(v.an, g_b3.lib, c, fade, then);
}

// ---------------------------------------------------------------------------- particles, texts
void fx_particle(const Particle& p) {
    if (!g_b3.parts.push(p)) { g_b3.parts[b3_rand() % (uint32_t)g_b3.parts.size()] = p; }
}

void fx_burst(int kind, Vec3 at, Vec3 toward) {
    Vec3 dir = normalize(v3(toward.x - at.x, 0.0f, toward.z - at.z));
    switch (kind) {
    case BURST_SPARKS:                       // steel on bone: white-gold sparks + a flash
        for (int i = 0; i < 14; ++i) {
            Particle p;
            p.p = at; p.v = v3(b3_randf(-2.5f, 2.5f) + dir.x * 1.5f, b3_randf(0.5f, 4.0f), b3_randf(-2.5f, 2.5f) + dir.z * 1.5f);
            p.age = 0; p.life = 10 + (int)(b3_rand() % 8); p.size0 = 0.22f; p.size1 = 0.04f; p.rot = b3_randf(0, 6.28f); p.spin = 0;
            p.cell = FX_STAR; p.additive = true; p.c0 = 0xFFFFF0C0; p.c1 = 0x00FF8020; p.grav = -9.0f;
            fx_particle(p);
        }
        {
            Particle f; f.p = at; f.v = v3(0, 0, 0); f.age = 0; f.life = 6; f.size0 = 1.1f; f.size1 = 0.5f; f.rot = 0; f.spin = 0;
            f.cell = FX_GLOW; f.additive = true; f.c0 = 0xFFFFFFE0; f.c1 = 0x00FFC080; f.grav = 0; fx_particle(f);
        }
        break;
    case BURST_FIRE:                         // fireball impact: embers + glow + smoke
        for (int i = 0; i < 16; ++i) {
            Particle p;
            p.p = at; p.v = v3(b3_randf(-2.2f, 2.2f), b3_randf(0.5f, 3.5f), b3_randf(-2.2f, 2.2f));
            p.age = 0; p.life = 16 + (int)(b3_rand() % 12); p.size0 = 0.45f; p.size1 = 0.1f; p.rot = b3_randf(0, 6.28f);
            p.spin = b3_randf(-0.2f, 0.2f); p.cell = (i & 1) ? FX_FLAME : FX_DOT; p.additive = true;
            p.c0 = 0xFFFFD060; p.c1 = 0x00E03010; p.grav = -2.0f;
            fx_particle(p);
        }
        for (int i = 0; i < 5; ++i) {
            Particle p;
            p.p = at + v3(b3_randf(-0.3f, 0.3f), 0.2f, b3_randf(-0.3f, 0.3f)); p.v = v3(b3_randf(-0.3f, 0.3f), 1.2f, b3_randf(-0.3f, 0.3f));
            p.age = 0; p.life = 34; p.size0 = 0.6f; p.size1 = 1.4f; p.rot = b3_randf(0, 6.28f); p.spin = 0.03f;
            p.cell = FX_SMOKE; p.additive = false; p.c0 = 0xA0302820; p.c1 = 0x00302820; p.grav = 0;
            fx_particle(p);
        }
        {
            Particle f; f.p = at; f.v = v3(0, 0, 0); f.age = 0; f.life = 9; f.size0 = 2.0f; f.size1 = 1.0f; f.rot = 0; f.spin = 0;
            f.cell = FX_GLOW; f.additive = true; f.c0 = 0xFFFFC060; f.c1 = 0x00FF4000; f.grav = 0; fx_particle(f);
        }
        break;
    case BURST_ARCANE:                       // mage bolt impact: violet sparks
        for (int i = 0; i < 12; ++i) {
            Particle p;
            p.p = at; p.v = v3(b3_randf(-2.0f, 2.0f), b3_randf(-0.5f, 2.5f), b3_randf(-2.0f, 2.0f));
            p.age = 0; p.life = 14 + (int)(b3_rand() % 6); p.size0 = 0.3f; p.size1 = 0.05f; p.rot = 0; p.spin = 0.3f;
            p.cell = FX_STAR; p.additive = true; p.c0 = 0xFFE0B0FF; p.c1 = 0x006020C0; p.grav = 0;
            fx_particle(p);
        }
        {
            Particle f; f.p = at; f.v = v3(0, 0, 0); f.age = 0; f.life = 8; f.size0 = 1.6f; f.size1 = 0.6f; f.rot = 0; f.spin = 0;
            f.cell = FX_GLOW; f.additive = true; f.c0 = 0xFFC080FF; f.c1 = 0x004010A0; f.grav = 0; fx_particle(f);
        }
        break;
    case BURST_DUST:                         // a skeleton hits the ground / a body falls
        for (int i = 0; i < 10; ++i) {
            Particle p;
            const float a = b3_randf(0, 6.28f);
            const shz_sincos_t sc = shz_sincosf(a);
            p.p = at + v3(sc.cos * 0.3f, 0.1f, sc.sin * 0.3f); p.v = v3(sc.cos * 1.6f, b3_randf(0.2f, 0.8f), sc.sin * 1.6f);
            p.age = 0; p.life = 26; p.size0 = 0.5f; p.size1 = 1.1f; p.rot = a; p.spin = 0.02f;
            p.cell = FX_SMOKE; p.additive = false; p.c0 = 0x90A08C70; p.c1 = 0x00A08C70; p.grav = 0.5f;
            fx_particle(p);
        }
        for (int i = 0; i < 6; ++i) {            // bone splinters
            Particle p;
            p.p = at + v3(0, 0.3f, 0); p.v = v3(b3_randf(-1.8f, 1.8f), b3_randf(1.5f, 3.5f), b3_randf(-1.8f, 1.8f));
            p.age = 0; p.life = 24; p.size0 = 0.25f; p.size1 = 0.2f; p.rot = b3_randf(0, 6.28f); p.spin = 0.35f;
            p.cell = FX_SHARD; p.additive = false; p.c0 = 0xFFE8E0C8; p.c1 = 0x00E8E0C8; p.grav = -9.0f;
            fx_particle(p);
        }
        break;
    case BURST_DARK:                         // Soul Rend / the Warden's slam: purple-black smoke and motes
        for (int i = 0; i < 14; ++i) {
            Particle p;
            p.p = at + v3(b3_randf(-0.4f, 0.4f), b3_randf(0.0f, 0.8f), b3_randf(-0.4f, 0.4f));
            p.v = v3(b3_randf(-1.5f, 1.5f), b3_randf(0.5f, 2.5f), b3_randf(-1.5f, 1.5f));
            p.age = 0; p.life = 22 + (int)(b3_rand() % 10); p.size0 = 0.7f; p.size1 = 1.3f; p.rot = b3_randf(0, 6.28f); p.spin = 0.05f;
            p.cell = FX_SMOKE; p.additive = false; p.c0 = 0xC0201030; p.c1 = 0x00100818; p.grav = 0;
            fx_particle(p);
        }
        for (int i = 0; i < 10; ++i) {
            Particle p;
            p.p = at; p.v = v3(b3_randf(-2.5f, 2.5f), b3_randf(0.5f, 3.0f), b3_randf(-2.5f, 2.5f));
            p.age = 0; p.life = 16; p.size0 = 0.35f; p.size1 = 0.05f; p.rot = 0; p.spin = 0.2f;
            p.cell = FX_STAR; p.additive = true; p.c0 = 0xFFD080FF; p.c1 = 0x00400080; p.grav = 0;
            fx_particle(p);
        }
        break;
    case BURST_HEAL:                         // the potion: green sparkles rising
        for (int i = 0; i < 14; ++i) {
            Particle p;
            const float a = b3_randf(0, 6.28f);
            const shz_sincos_t sc = shz_sincosf(a);
            p.p = at + v3(sc.cos * 0.5f, b3_randf(-0.8f, 0.4f), sc.sin * 0.5f);
            p.v = v3(0, b3_randf(0.8f, 1.8f), 0);
            p.age = 0; p.life = 26; p.size0 = 0.3f; p.size1 = 0.1f; p.rot = 0; p.spin = 0.1f;
            p.cell = FX_STAR; p.additive = true; p.c0 = 0xFFA0FFA0; p.c1 = 0x0020A040; p.grav = 0;
            fx_particle(p);
        }
        break;
    case BURST_WISP:                         // the Warden's soul leaves: one slow purple glow rising
        {
            Particle p;
            p.p = at; p.v = v3(b3_randf(-0.2f, 0.2f), 0.9f, b3_randf(-0.2f, 0.2f));
            p.age = 0; p.life = 70; p.size0 = 0.6f; p.size1 = 0.9f; p.rot = 0; p.spin = 0;
            p.cell = FX_GLOW; p.additive = true; p.c0 = 0xFFC090FF; p.c1 = 0x00402080; p.grav = -0.2f;
            fx_particle(p);
        }
        break;
    case BURST_GUARD:                        // shield ring pulse
        for (int i = 0; i < 10; ++i) {
            Particle p;
            const float a = 6.28318f * (float)i / 10.0f;
            const shz_sincos_t sc = shz_sincosf(a);
            p.p = at; p.v = v3(sc.cos * 1.8f, 0.6f, sc.sin * 1.8f);
            p.age = 0; p.life = 16; p.size0 = 0.3f; p.size1 = 0.05f; p.rot = 0; p.spin = 0;
            p.cell = FX_STAR; p.additive = true; p.c0 = 0xFFB0E0FF; p.c1 = 0x002060FF; p.grav = 0;
            fx_particle(p);
        }
        break;
    }
}

void fx_text(const View& v, const char* text, uint32_t color, int scale) {
    FloatText3 f;
    f.p = view_head(v) + v3(b3_randf(-0.2f, 0.2f), 0.0f, 0.0f);
    f.age = 0; f.life = 54;
    snprintf(f.text, sizeof f.text, "%s", text);
    f.color = color; f.scale = scale;
    // stack labels that start together on the same actor
    int same = 0;
    for (const FloatText3& o : g_b3.texts) if (o.age < 6 && shz_fabsf(o.p.x - f.p.x) < 0.5f && shz_fabsf(o.p.z - f.p.z) < 0.5f) ++same;
    f.p.y += 0.35f * (float)same;
    if (!g_b3.texts.push(f)) g_b3.texts[0] = f;
}

// ---------------------------------------------------------------------------- camera
static CamShot shot_arena(const char* name) {
    CamShot s;
    const ArenaShot* a = arena_shot(*g_b3.arena, name);
    if (!a) a = arena_shot(*g_b3.arena, "overview");
    s.eye = a->eye; s.target = a->target; s.fov = a->fov;
    return s;
}

static Vec3 enemy_centroid() {
    Vec3 c = v3(0, 0, 0);
    int n = 0;
    for (int i = 0; i < g_b3.num_views; ++i) {
        const View& v = g_b3.views[i];
        if (v.kind == K_HERO || !v.present) continue;
        c = c + v.home; ++n;
    }
    return n ? c * (1.0f / (float)n) : v3(2.5f, 0, 0);
}

CamShot shot_for(int shot, const View* u, const View* t) {
    CamShot s = shot_arena("overview");
    switch (shot) {
    case SHOT_OVERVIEW: break;
    case SHOT_LEAN:
        if (t) { Vec3 d = t->home - s.target; d.y = 0; s.target = s.target + d * T_LEAN; s.eye = s.eye + d * (T_LEAN * 0.6f); }
        break;
    case SHOT_PULLBACK: {       // Fire confirm: ease toward the enemy group (no dolly: the overview frames them all)
        const Vec3 d = (enemy_centroid() - s.target) * 0.25f;
        s.target = s.target + v3(d.x, 0, d.z);
        s.eye = s.eye + v3(d.x * 0.5f, 0.3f, 0);
        break;
    }
    case SHOT_ACTION:
        if (u && t) {   // the pair, from the south and a little behind the attacker; distance follows their gap
            const Vec3 mid = (u->pos + t->pos) * 0.5f;
            Vec3 d = v3(t->pos.x - u->pos.x, 0, t->pos.z - u->pos.z);
            const float gap = length(d);
            d = gap > 1e-3f ? d * (1.0f / gap) : v3(1, 0, 0);
            s.eye = mid - d * (1.2f + 0.25f * gap) + v3(0, 1.9f + 0.3f * gap, 4.6f + 0.75f * gap);
            s.target = mid + v3(0, 1.0f, 0);
            s.fov = 40.0f;
        }
        break;
    case SHOT_FOCUS:
        if (u) {        // the overview's angle, pulled in toward the actor
            const Vec3 look = s.target + (u->pos + v3(0, 1.0f, 0) - s.target) * 0.65f;
            s.eye = look + (s.eye - s.target) * 0.72f;
            s.target = look;
        }
        break;
    case SHOT_CAST:
        if (u) {
            const Vec3 c = enemy_centroid();
            s.eye = u->home + v3(-1.4f, 2.3f, 5.6f);
            s.target = u->home + (c - u->home) * 0.45f + v3(0, 1.0f, 0);
            s.fov = 44.0f;
        }
        break;
    case SHOT_INTRO: s = shot_arena("intro"); break;
    case SHOT_VICTORY: {
        s = shot_arena("victory");
        // slow orbit around the hero
        const View* h = u;
        if (h) {
            // slow orbit, clamped (0.6 rad): the arena's view-culled props are built for these cameras
            const float a = 0.004f * (float)(g_b3.outro_t < 150 ? g_b3.outro_t : 150);
            const shz_sincos_t sc = shz_sincosf(a);
            Vec3 r = s.eye - h->pos;
            s.eye = h->pos + v3(r.x * sc.cos - r.z * sc.sin, r.y, r.x * sc.sin + r.z * sc.cos);
            s.target = h->pos + v3(0.3f, 1.0f, 0);
        }
        break;
    }
    case SHOT_DEFEAT:
        if (u) { s.eye = s.eye + (u->pos + v3(0, 1.0f, 0) - s.eye) * (0.25f + 0.0015f * (float)(g_b3.outro_t < 120 ? g_b3.outro_t : 120));
                 s.target = s.target + (u->pos + v3(0, 0.8f, 0) - s.target) * 0.5f; }
        break;
    }
    return s;
}

static void cam_set_goal(int shot, int u, int t, float rate) {
    g_b3.cam.shot = shot; g_b3.cam.u = u; g_b3.cam.t = t; g_b3.cam.rate = rate;
}

static void cam_tick() {
    CamRig& c = g_b3.cam;
    const View* u = c.u >= 0 ? &g_b3.views[c.u] : nullptr;
    const View* t = c.t >= 0 ? &g_b3.views[c.t] : nullptr;
    const CamShot goal = shot_for(c.shot, u, t);
    if (c.cut) { c.cur = goal; c.cut = false; }
    else {
        c.cur.eye = lerp(c.cur.eye, goal.eye, c.rate);
        c.cur.target = lerp(c.cur.target, goal.target, c.rate);
        c.cur.fov += (goal.fov - c.cur.fov) * c.rate;
    }
    if (c.shake > 0.001f) c.shake *= 0.84f; else c.shake = 0.0f;
}

void cam_camera(Camera* out) {
    const CamRig& c = g_b3.cam;
    Vec3 eye = c.cur.eye, tgt = c.cur.target;
    if (c.shake > 0.0f) {
        // deterministic per frame (a hash of the tick), not the particle RNG
        const uint32_t h = (uint32_t)g_b3.frame * 2654435761u;
        const float sx = ((float)((h >> 8) & 255) / 127.5f - 1.0f) * c.shake, sy = ((float)((h >> 16) & 255) / 127.5f - 1.0f) * c.shake;
        eye = eye + v3(sx, sy, 0); tgt = tgt + v3(sx * 0.5f, sy * 0.5f, 0);
    }
    camera_look(out, eye, tgt, c.cur.fov);
}

// ---------------------------------------------------------------------------- sequences
const char* battle3d_seq_name(int kind) {
    switch (kind) {
    case SEQ_NONE: return "none";
    case SEQ_INTRO: return "intro";
    case SEQ_MELEE: return "melee";
    case SEQ_BOLT: return "bolt";
    case SEQ_FIREBALL: return "fireball";
    case SEQ_GUARD: return "guard";
    case SEQ_BURN: return "burn";
    case SEQ_POTION: return "potion";
    case SEQ_CHARGE: return "charge";
    case SEQ_SOUL_REND: return "soul_rend";
    case SEQ_GRAVE_FIRE: return "grave_fire";
    case SEQ_WARD: return "ward";
    case SEQ_SUMMON: return "summon";
    case SEQ_PHASE: return "phase";
    case SEQ_CRUMBLE: return "crumble";
    }
    return "?";
}

static void cue(Seq& s, int t, uint8_t op, int actor, int a = 0, int b = 0, float f = 0.0f, Vec3 p = { 0, 0, 0 }) {
    Cue c; c.t = (int16_t)t; c.op = op; c.actor = (int8_t)actor; c.a = (int16_t)a; c.b = (int16_t)b; c.f = f; c.p = p;
    if (!s.cues.push(c)) eng::dbg_print("battle3d: sequence cue list full");
}

static int vidx(const View* v) { return v ? (int)(v - g_b3.views) : -1; }

// Run and return durations gate the rules clock (the sequence's length), so they use exact IEEE
// arithmetic (sqrtf, division) - identical on the SH4 and the host - not SH4ZAM's FSRRA estimates.
static int run_ticks(Vec3 a, Vec3 b) {
    const float dx = b.x - a.x, dz = b.z - a.z;
    const float d = sqrtf(dx * dx + dz * dz);
    const int t = (int)(d / T_RUN_SPEED * 60.0f + 0.5f);
    return t < 10 ? 10 : (t > 48 ? 48 : t);
}

static Vec3 melee_spot(const View& u, const View& tg) {
    const float dx = u.home.x - tg.pos.x, dz = u.home.z - tg.pos.z;
    const float d = sqrtf(dx * dx + dz * dz);
    const float k = d > 1e-4f ? T_MELEE_GAP * tg.scale / d : 0.0f;
    return v3(tg.pos.x + dx * k, tg.pos.y, tg.pos.z + dz * k);
}

static void build_melee(Seq& s, View& u, View& tg) {
    const int ui = vidx(&u), ti = vidx(&tg);
    const Vec3 spot = melee_spot(u, tg);
    int t = 0;
    cue(s, t, OP_CAM, ui, SHOT_ACTION, ti, T_CAM_RATE_SEQ);
    cue(s, t, OP_FACE, ui, ti);
    cue(s, t, OP_PLAY, ui, clip("run"), -1, 0.12f);
    const int run = run_ticks(u.pos, spot);
    cue(s, t, OP_MOVE, ui, run, 0, 0.0f, spot);
    t += run;
    const int atk = u.atk_alt ? u.c_atk[1] : u.c_atk[0];
    u.atk_alt ^= 1;
    cue(s, t, OP_PLAY, ui, atk, -1, 0.08f);
    int imp = anim::clip_event(g_b3.lib, atk, anim::EV_IMPACT);
    if (imp < 0) imp = anim::clip_ticks(g_b3.lib, atk) * 6 / 10;
    cue(s, t + imp, OP_IMPACT, ti, 0);
    // recovery: back to the slot shortly after the blow lands (the clip's follow-through is cut)
    const int rest = anim::clip_ticks(g_b3.lib, atk) - imp;
    t += imp + (rest < T_RECOVER ? rest : T_RECOVER);
    const int back = run_ticks(spot, u.home);
    cue(s, t, OP_FACE_POINT, ui, 0, 0, 0.0f, u.home);
    cue(s, t, OP_PLAY, ui, clip("run"), -1, 0.12f);
    cue(s, t, OP_MOVE, ui, back, 0, 0.0f, u.home);
    t += back;
    cue(s, t, OP_FACE_HOME, ui);
    cue(s, t, OP_IDLE, ui, 0, 0, 0.2f);
    cue(s, t, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, t + 6, OP_END, -1);
}

static void build_bolt(Seq& s, View& u, View& tg) {
    const int ui = vidx(&u), ti = vidx(&tg);
    cue(s, 0, OP_CAM, ui, SHOT_ACTION, ti, T_CAM_RATE_SEQ);
    cue(s, 0, OP_FACE, ui, ti);
    const int c = clip("throw");
    cue(s, 4, OP_PLAY, ui, c, -2, 0.1f);
    int rel = anim::clip_event(g_b3.lib, c, anim::EV_RELEASE);
    if (rel < 0) rel = 40;
    cue(s, 4, OP_CHARGE, ui, rel, 1);                       // violet glow gathers in the hand
    cue(s, 4 + rel, OP_LAUNCH, ui, 0, PROJ_BOLT, 24.0f);
    cue(s, 4 + rel + 24 + 26, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, 4 + rel + 24 + 26, OP_FACE_HOME, ui);
    cue(s, 4 + rel + 24 + 30, OP_END, -1);
}

static void build_fireball(Seq& s, View& u, const BattleEvent& ev) {
    const int ui = vidx(&u);
    cue(s, 0, OP_CAM, ui, SHOT_CAST, -1, T_CAM_RATE_SEQ);
    const int c = clip("throw");
    cue(s, 0, OP_PLAY, ui, c, -2, 0.1f);
    int rel = anim::clip_event(g_b3.lib, c, anim::EV_RELEASE);
    if (rel < 0) rel = 40;
    cue(s, 0, OP_CHARGE, ui, rel, 0);                       // fire gathers in the right hand
    const int FLY = 26;
    int last = rel;
    for (int k = 0; k < ev.hits.size(); ++k) {
        cue(s, rel + 5 * k, OP_LAUNCH, ui, k, PROJ_FIREBALL, (float)FLY);
        last = rel + 5 * k + FLY;
    }
    cue(s, last + 26, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, last + 30, OP_END, -1);
}

static void build_guard(Seq& s, View& u) {
    const int ui = vidx(&u);
    cue(s, 0, OP_CAM, ui, SHOT_FOCUS, -1, T_CAM_RATE);
    cue(s, 0, OP_PLAY, ui, clip("block"), clip("blocking"), 0.1f);
    cue(s, 12, OP_GUARD_ON, ui);
    cue(s, 30, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, 36, OP_END, -1);
}

static void build_burn(Seq& s, View& u) {
    const int ui = vidx(&u);
    cue(s, 0, OP_CAM, ui, SHOT_FOCUS, -1, T_CAM_RATE);
    cue(s, 0, OP_BURST, ui, BURST_FIRE);
    cue(s, 4, OP_BURN_HIT, ui);
    cue(s, 34, OP_END, -1);
}

static void build_potion(Seq& s, View& u) {
    const int ui = vidx(&u);
    cue(s, 0, OP_CAM, ui, SHOT_FOCUS, -1, T_CAM_RATE);
    cue(s, 0, OP_PLAY, ui, clip("use_item"), -2, 0.1f);
    cue(s, 30, OP_HEAL, ui);
    cue(s, 58, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, 62, OP_END, -1);
}

// CHARGE: the telegraph (GDD 12.3): the Warden winds up and holds it, dark motes gather in his staff
static void build_charge(Seq& s, View& u) {
    const int ui = vidx(&u);
    cue(s, 0, OP_CAM, ui, SHOT_FOCUS, -1, T_CAM_RATE_SEQ);
    cue(s, 0, OP_PLAY, ui, clip("throw"), -1, 0.15f);
    cue(s, 0, OP_HOLD_AT, ui, 24);
    cue(s, 6, OP_CHARGE_ON, ui);
    cue(s, 80, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, 86, OP_END, -1);
}

// Soul Rend: the held blow lands - a staff slam, a dark wave runs along the ground to the hero
static void build_soul_rend(Seq& s, View& u, View& tg) {
    const int ui = vidx(&u), ti = vidx(&tg);
    const int c = clip("chop_2h");
    cue(s, 0, OP_CAM, ui, SHOT_ACTION, ti, T_CAM_RATE_SEQ);
    cue(s, 0, OP_FACE, ui, ti);
    cue(s, 0, OP_PLAY, ui, c, -2, 0.1f);
    int imp = anim::clip_event(g_b3.lib, c, anim::EV_IMPACT);
    if (imp < 0) imp = 50;
    cue(s, imp, OP_BURST, ui, BURST_DARK);
    cue(s, imp, OP_WAVE, ui, 20);
    cue(s, imp + 20 + 40, OP_FACE_HOME, ui);
    cue(s, imp + 20 + 40, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, imp + 20 + 44, OP_END, -1);
}

// Grave Fire (phase 2): a cast, and purple fire erupts under every foe
static void build_grave_fire(Seq& s, View& u, const BattleEvent& ev) {
    const int ui = vidx(&u);
    View* first = ev.hits.empty() ? nullptr : view_of(ev.hits[0].target);
    cue(s, 0, OP_CAM, ui, SHOT_ACTION, vidx(first), T_CAM_RATE_SEQ);
    if (first) cue(s, 0, OP_FACE, ui, vidx(first));
    const int c = clip("throw");
    cue(s, 0, OP_PLAY, ui, c, -2, 0.1f);
    int rel = anim::clip_event(g_b3.lib, c, anim::EV_RELEASE);
    if (rel < 0) rel = 40;
    cue(s, 0, OP_CHARGE, ui, rel, 1);
    for (int k = 0; k < ev.hits.size(); ++k) cue(s, rel + 6 + 4 * k, OP_ERUPT, ui, k);
    cue(s, rel + 60, OP_FACE_HOME, ui);
    cue(s, rel + 60, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, rel + 64, OP_END, -1);
}

// Raise Dead: the staff strikes the ground, the rune ring pulses, the minions claw out of the dirt
static void build_summon(Seq& s, View& u) {
    const int ui = vidx(&u);
    const int c = clip("chop_2h");
    cue(s, 0, OP_CAM, ui, SHOT_FOCUS, -1, T_CAM_RATE_SEQ);
    cue(s, 0, OP_PLAY, ui, c, -2, 0.1f);
    int imp = anim::clip_event(g_b3.lib, c, anim::EV_IMPACT);
    if (imp < 0) imp = 50;
    cue(s, imp, OP_RING_PULSE, ui);
    cue(s, imp, OP_BURST, ui, BURST_DARK);
    cue(s, imp + 6, OP_RAISE, ui);
    const int rise = anim::clip_ticks(g_b3.lib, clip("skel_rise"));
    cue(s, imp + 6 + rise - 10, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, imp + 6 + rise, OP_END, -1);
}

// The phase change (GDD 12.5, 4 s): staff slam, the ring turns red, standing minions crumble, two rise
static void build_phase(Seq& s, View& u) {
    const int ui = vidx(&u);
    const int c = clip("chop_2h");
    cue(s, 0, OP_CAM, ui, SHOT_FOCUS, -1, T_CAM_RATE_SEQ);
    cue(s, 0, OP_CAPTION, ui, CAP_PHASE);
    cue(s, 10, OP_PLAY, ui, c, -2, 0.1f);
    int imp = anim::clip_event(g_b3.lib, c, anim::EV_IMPACT);
    if (imp < 0) imp = 50;
    cue(s, 10 + imp, OP_PHASE2, ui);
    cue(s, 10 + imp, OP_RING_PULSE, ui);
    cue(s, 10 + imp, OP_BURST, ui, BURST_DARK);
    cue(s, 10 + imp, OP_CRUMBLE, ui);
    cue(s, 10 + imp + 70, OP_RAISE, ui);
    const int rise = anim::clip_ticks(g_b3.lib, clip("skel_rise"));
    cue(s, 10 + imp + 70 + rise - 10, OP_CAM, -1, SHOT_OVERVIEW, -1, T_CAM_RATE);
    cue(s, 10 + imp + 70 + rise, OP_END, -1);
}

// The Warden is laid to rest: his minions collapse with him
static void build_crumble(Seq& s) {
    cue(s, 0, OP_CRUMBLE, -1);
    cue(s, 20, OP_END, -1);
}

static bool g_short_intro = false;     // slice 3D-6 (BA4): the field's T8 already showed the name card
void battle3d_set_short_intro(bool on) { g_short_intro = on; }

static void build_intro(Seq& s) {
    cam_set_goal(SHOT_INTRO, -1, -1, 1.0f);
    g_b3.cam.cur = shot_for(SHOT_INTRO, nullptr, nullptr);     // cut now (the next cue eases away from it)
    cue(s, 1, OP_CAM, -1, SHOT_OVERVIEW, -1, 0.045f);
    int t = 10, last = 60;
    const int aw = clip("skel_awaken");
    for (int i = 0; i < g_b3.num_views; ++i) {
        View& v = g_b3.views[i];
        if (v.kind == K_HERO || !v.present) continue;
        cue(s, t, OP_PLAY, i, aw, -2, 0.05f);
        last = t + anim::clip_ticks(g_b3.lib, aw);
        t += 10;
    }
    if (g_b3.boss_vi >= 0 && !g_short_intro) {     // the boss intro runs until his name card has faded
        cue(s, 24, OP_NAMECARD, -1);
        if (last + 4 < 24 + 110) last = 24 + 110 - 4;
    }
    cue(s, last + 4, OP_END, -1);
}

// Applies hit k of the sequence's event to its target: shown HP, number, flash, reaction.
static void impact(Seq& s, int k, bool melee) {
    if (k < 0 || k >= s.ev.hits.size()) return;
    const Hit& h = s.ev.hits[k];
    View* tg = view_of(h.target);
    View* u = view_of(s.ev.user);
    if (!tg) return;
    const bool guarded = tg->guard_shown && s.ev.skill == SK_ATTACK;
    tg->shown_hp = tg->shown_hp - h.dmg < 0 ? 0 : tg->shown_hp - h.dmg;
    char num[12];
    snprintf(num, sizeof num, "%d", h.dmg);
    fx_text(*tg, num, guarded ? 0xFF9CD8FF : 0xFFFFE060, 3);
    tg->flash = 5;
    const Vec3 at = view_chest(*tg) + (u ? normalize(v3(u->pos.x - tg->pos.x, 0, u->pos.z - tg->pos.z)) * 0.35f : v3(0, 0, 0));
    if (melee) {
        fx_burst(guarded ? BURST_GUARD : BURST_SPARKS, at, u ? u->pos : tg->pos);
        g_b3.hitstop = guarded ? 3 : 4;
        g_b3.cam.shake = T_SHAKE_MELEE;
    }
    if (tg->shown_hp <= 0 && tg->a->hp <= 0) {
        view_play(*tg, tg->c_death, 0.08f, -1);
        tg->dying = true; tg->dead_t = 0;
        tg->guard_shown = false; tg->burn_shown = false; tg->charge_shown = false;
    } else if (guarded) {
        view_play(*tg, clip("block_hit"), 0.06f, clip("blocking"));
        fx_text(*tg, "GUARD", 0xFF80D0FF, 2);
    } else {
        view_play(*tg, tg->c_hit, 0.06f, -2);
    }
}

static void fire_cue(Seq& s, const Cue& c) {
    View* v = c.actor >= 0 ? &g_b3.views[c.actor] : nullptr;
    switch (c.op) {
    case OP_PLAY:
        if (v) view_play(*v, c.a, c.f, c.b == -2 ? v->c_idle : c.b);
        break;
    case OP_IDLE:
        if (v) view_play(*v, v->guard_shown ? clip("blocking") : v->c_idle, c.f, -1);
        break;
    case OP_MOVE:
        if (v) { v->mv_from = v->pos; v->mv_to = c.p; v->mv_t = 0; v->mv_dur = c.a > 0 ? c.a : 1; }
        break;
    case OP_FACE:
        if (v && c.a >= 0) v->yaw_goal = yaw_to(v->pos, g_b3.views[c.a].pos);
        break;
    case OP_FACE_POINT:
        if (v) v->yaw_goal = yaw_to(v->pos, c.p);
        break;
    case OP_FACE_HOME:
        if (v) v->yaw_goal = v->home_yaw;
        break;
    case OP_CAM:
        cam_set_goal(c.a, c.actor, c.b, c.f > 0 ? c.f : T_CAM_RATE);
        break;
    case OP_IMPACT:
        impact(s, c.a, true);
        break;
    case OP_LAUNCH: {
        if (!v || c.a >= s.ev.hits.size()) break;
        View* tg = view_of(s.ev.hits[c.a].target);
        if (!tg) break;
        Projectile p;
        p.from = v->hand; p.to = view_chest(*tg);
        p.t = 0; p.dur = (int)c.f; p.kind = c.b; p.hit = c.a; p.arc = c.b == PROJ_FIREBALL ? 1.2f : 0.3f;
        if (!g_b3.projs.push(p)) impact(s, c.a, false);
        break;
    }
    case OP_CHARGE:
        if (v) { v->charge = c.a; v->charge_kind = c.b; }
        break;
    case OP_GUARD_ON:
        if (v) { v->guard_shown = true; fx_burst(BURST_GUARD, view_chest(*v), v->pos); fx_text(*v, "GUARD", 0xFF80D0FF, 2); }
        break;
    case OP_BURST:
        if (v) fx_burst(c.a, view_chest(*v), v->pos);
        break;
    case OP_BURN_HIT:
        if (v) {
            v->shown_hp = v->shown_hp - s.ev.dmg < 0 ? 0 : v->shown_hp - s.ev.dmg;
            char num[12];
            snprintf(num, sizeof num, "%d", s.ev.dmg);
            fx_text(*v, num, 0xFFFFA040, 3);
            v->flash = 4;
            if (v->shown_hp <= 0 && v->a->hp <= 0) {
                view_play(*v, v->c_death, 0.08f, -1);
                v->dying = true; v->dead_t = 0; v->burn_shown = false;
            } else {
                view_play(*v, clip("hit_a"), 0.06f, -2);
            }
        }
        break;
    case OP_HOLD_AT:
        if (v) anim::hold_at(v->an, (float)c.a / 60.0f);
        break;
    case OP_CHARGE_ON:
        if (v) {
            v->charge_shown = true;
            fx_burst(BURST_DARK, view_head(*v), v->pos);
            g_b3.caption = CAP_CHARGE; g_b3.caption_t = 140;
        }
        break;
    case OP_WAVE: {
        if (!v || s.ev.hits.empty()) break;
        View* tg = view_of(s.ev.hits[0].target);
        if (!tg) break;
        v->charge_shown = false;
        Projectile p;
        p.from = v3(v->pos.x, 0.3f, v->pos.z); p.to = v3(tg->pos.x, 0.3f, tg->pos.z);
        p.t = 0; p.dur = c.a > 0 ? c.a : 20; p.kind = PROJ_WAVE; p.hit = 0; p.arc = 0.0f;
        if (!g_b3.projs.push(p)) impact(s, 0, true);
        break;
    }
    case OP_ERUPT: {
        if (c.a >= s.ev.hits.size()) break;
        View* tg = view_of(s.ev.hits[c.a].target);
        if (!tg) break;
        // purple fire under the target: a burst of violet flames from the ground
        for (int i = 0; i < 18; ++i) {
            Particle p;
            const float a = b3_randf(0, 6.28f);
            const shz_sincos_t sc = shz_sincosf(a);
            p.p = tg->pos + v3(sc.cos * b3_randf(0.1f, 0.6f), 0.1f, sc.sin * b3_randf(0.1f, 0.6f));
            p.v = v3(sc.cos * 0.4f, b3_randf(2.0f, 4.0f), sc.sin * 0.4f);
            p.age = 0; p.life = 18 + (int)(b3_rand() % 12); p.size0 = 0.6f; p.size1 = 0.15f; p.rot = b3_randf(-0.3f, 0.3f); p.spin = 0;
            p.cell = FX_FLAME; p.additive = true; p.c0 = 0xFFE080FF; p.c1 = 0x006010C0; p.grav = 0;
            fx_particle(p);
        }
        impact(s, c.a, false);
        g_b3.cam.shake = T_SHAKE_MELEE;
        if (tg->a->hp > 0) { tg->burn_shown = true; fx_text(*tg, "BURN", 0xFFFF8040, 2); }
        break;
    }
    case OP_RING_PULSE:
        g_b3.ring_pulse_t = 40;
        g_b3.cam.shake = T_SHAKE_BIG * 0.6f;
        break;
    case OP_RAISE:
        for (const Hit& h : s.ev.hits) {
            View* m = view_of(h.target);
            if (!m) continue;
            m->present = true; m->dying = false; m->gone = false; m->alpha = 1.0f; m->dead_t = 0;
            m->pos = m->home; m->yaw = m->yaw_goal = m->home_yaw;
            m->shown_hp = m->a->hp_max; m->bar = (float)m->shown_hp;
            m->burn_shown = m->guard_shown = false;
            m->asleep = false;
            const int rc = clip("skel_rise");
            anim::animator_init(m->an, rc, 0.0f);
            m->an.then = (int16_t)m->c_idle; m->an.then_fade = 0.2f;
            m->rise_t = anim::clip_ticks(g_b3.lib, rc);       // (lighting: baked at battle start, same yaw)
            fx_burst(BURST_DUST, m->pos, m->pos);
        }
        break;
    case OP_CRUMBLE:
        for (const Hit& h : s.ev.hits) {
            View* m = view_of(h.target);
            if (!m || !m->present || m->dying) continue;
            if (s.kind == SEQ_PHASE && h.dmg != 1) continue;        // phase: only those that stood
            view_play(*m, m->c_death, 0.08f, -1);
            m->dying = true; m->dead_t = 0; m->shown_hp = 0; m->burn_shown = false;
        }
        break;
    case OP_PHASE2:
        g_b3.boss_phase2 = true;
        break;
    case OP_CAPTION:
        g_b3.caption = c.a; g_b3.caption_t = 200;
        break;
    case OP_HEAL:
        if (v) {
            int heal = s.ev.hits.empty() ? 0 : -s.ev.hits[0].dmg;
            heal = heal < 0 ? 0 : (heal > 999 ? 999 : heal);
            v->shown_hp = v->shown_hp + heal > v->a->hp_max ? v->a->hp_max : v->shown_hp + heal;
            char num[12];
            snprintf(num, sizeof num, "+%d", heal);
            fx_text(*v, num, 0xFF70F090, 3);
            fx_burst(BURST_HEAL, view_chest(*v), v->pos);
        }
        break;
    case OP_NAMECARD:
        g_b3.namecard_t = 110;
        break;
    case OP_END:
        s.ended = true;
        break;
    }
}

static bool start_seq(Seq& s) {
    s.t = 0; s.next = 0; s.ended = false; s.cues.clear();
    const BattleEvent& ev = s.ev;
    View* u = view_of(ev.user);
    if (s.kind == SEQ_INTRO) { build_intro(s); return true; }
    if (!u) return false;
    if (ev.kind == EvKind::BurnTick) { s.kind = SEQ_BURN; build_burn(s, *u); return true; }
    if (ev.kind == EvKind::Crumble) { s.kind = SEQ_CRUMBLE; build_crumble(s); return true; }
    View* tg0 = ev.hits.empty() ? nullptr : view_of(ev.hits[0].target);
    switch (ev.skill) {
    case SK_POTION:     if (ev.hits.empty()) return false; s.kind = SEQ_POTION; build_potion(s, *u); return true;
    case SK_CHARGE:     s.kind = SEQ_CHARGE; build_charge(s, *u); return true;
    case SK_SOUL_REND:  if (!tg0) return false; s.kind = SEQ_SOUL_REND; build_soul_rend(s, *u, *tg0); return true;
    case SK_GRAVE_FIRE: s.kind = SEQ_GRAVE_FIRE; build_grave_fire(s, *u, ev); return true;
    case SK_BONE_WARD:  s.kind = SEQ_WARD; build_guard(s, *u); return true;
    case SK_RAISE_DEAD: s.kind = SEQ_SUMMON; build_summon(s, *u); return true;
    case SK_PHASE:      s.kind = SEQ_PHASE; build_phase(s, *u); return true;
    case SK_BONE_SLASH: if (!tg0) return false; s.kind = SEQ_MELEE; build_melee(s, *u, *tg0); return true;
    case SK_ATTACK: {
        if (ev.hits.empty()) return false;
        View* tg = view_of(ev.hits[0].target);
        if (!tg) return false;
        if (KINDS[u->kind].ranged) { s.kind = SEQ_BOLT; build_bolt(s, *u, *tg); }
        else { s.kind = SEQ_MELEE; build_melee(s, *u, *tg); }
        return true;
    }
    case SK_FIREBALL: s.kind = SEQ_FIREBALL; build_fireball(s, *u, ev); return true;
    case SK_GUARD:    s.kind = SEQ_GUARD; build_guard(s, *u); return true;
    default: return false;
    }
}

// The sequence is over when its END cue fired, nobody is still running to a spot, no projectile
// is in flight and every actor it killed has hit the ground.
static bool seq_done(const Seq& s) {
    if (!s.ended) return false;
    if (!g_b3.projs.empty()) return false;
    for (int i = 0; i < g_b3.num_views; ++i) {
        const View& v = g_b3.views[i];
        if (v.mv_dur > 0 && v.mv_t < v.mv_dur) return false;
        if (v.dying && v.dead_t < v.collapse_t + 8) return false;
        if (v.rise_t > 0) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------- per-tick update
static void views_step(float dt, bool frozen) {
    for (int i = 0; i < g_b3.num_views; ++i) {
        View& v = g_b3.views[i];
        if (!v.present) continue;
        if (!frozen) anim::tick(v.an, g_b3.lib, dt);
        if (v.mv_dur > 0 && v.mv_t < v.mv_dur && !frozen) {
            ++v.mv_t;
            // lands exactly on the goal (positions feed the next sequence's exact run lengths)
            v.pos = v.mv_t >= v.mv_dur ? v.mv_to : lerp(v.mv_from, v.mv_to, ease((float)v.mv_t / (float)v.mv_dur));
        }
        // turn toward yaw_goal
        const float d = angle_diff(v.yaw, v.yaw_goal);
        float step = d * 0.25f;
        if (step > 0.25f) step = 0.25f;
        if (step < -0.25f) step = -0.25f;
        v.yaw += step;
        if (v.flash > 0) --v.flash;
        if (v.dying) {
            ++v.dead_t;
            if (v.dead_t == v.collapse_t) fx_burst(BURST_DUST, v.pos, v.pos);
            if (v.kind != K_HERO && v.dead_t > v.collapse_t + 40) {
                v.alpha -= 1.0f / 30.0f;
                if (v.alpha <= 0.0f) { v.alpha = 0.0f; v.gone = true; }
            }
        }
        // HP bar drains toward the shown HP (after a short delay)
        if (v.bar > (float)v.shown_hp) {
            if (v.bar_delay > 0) --v.bar_delay;
            else { v.bar -= 0.25f; if (v.bar < (float)v.shown_hp) v.bar = (float)v.shown_hp; }
        } else {
            v.bar_delay = 14;
            if (v.bar < (float)v.shown_hp) { v.bar += 0.5f; if (v.bar > (float)v.shown_hp) v.bar = (float)v.shown_hp; }
        }
        // status effects shown only while the rules still have them
        if (v.burn_shown && !status_has(*v.a, StatusId::Burn)) v.burn_shown = false;
        if (v.guard_shown && !status_has(*v.a, StatusId::Guard)) {
            v.guard_shown = false;
            if (!v.dying && v.an.clip == clip("blocking")) view_play(v, v.c_idle, 0.2f, -1);
        }
        if (v.charge > 0) --v.charge;
        if (v.rise_t > 0 && !frozen) --v.rise_t;
        if (v.charge_shown && !status_has(*v.a, StatusId::Charge) && !v.dying) {
            // the charge was spent (Soul Rend) or lost (the phase change interrupted it)
            v.charge_shown = false;
            if (v.an.hold_at >= 0.0f) view_play(v, v.c_idle, 0.2f, -1);
        }
    }
}

static void particles_step() {
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < g_b3.parts.size();) {
        Particle& p = g_b3.parts[i];
        ++p.age;
        if (p.age >= p.life) { g_b3.parts.swap_remove(i); continue; }
        p.v.y += p.grav * dt;
        p.p = p.p + p.v * dt;
        if (p.p.y < 0.02f && p.grav < 0) { p.p.y = 0.02f; p.v = v3(p.v.x * 0.5f, -p.v.y * 0.3f, p.v.z * 0.5f); }
        p.rot += p.spin;
        ++i;
    }
    for (int i = 0; i < g_b3.texts.size();) {
        FloatText3& f = g_b3.texts[i];
        if (++f.age >= f.life) { g_b3.texts.swap_remove(i); continue; }
        ++i;
    }
}

// Cosmetic emitters: burning actors, charging hands, guard shimmer.
static void emitters_step() {
    for (int i = 0; i < g_b3.num_views; ++i) {
        View& v = g_b3.views[i];
        if (!v.present || v.gone) continue;
        if (v.burn_shown && (g_b3.frame % 3) == 0) {
            Particle p;
            p.p = v.pos + v3(b3_randf(-0.3f, 0.3f), b3_randf(0.3f, 1.3f) * v.scale, b3_randf(-0.3f, 0.3f));
            p.v = v3(b3_randf(-0.2f, 0.2f), b3_randf(1.0f, 1.8f), b3_randf(-0.2f, 0.2f));
            p.age = 0; p.life = 20; p.size0 = 0.45f; p.size1 = 0.12f; p.rot = b3_randf(-0.3f, 0.3f); p.spin = 0;
            p.cell = FX_FLAME; p.additive = true; p.c0 = 0xFFFFB040; p.c1 = 0x00C02000; p.grav = 0;
            fx_particle(p);
        }
        if (v.charge_shown && (g_b3.frame & 1) == 0) {        // dark motes spiral into the staff
            Particle p;
            const float a = 0.35f * (float)g_b3.frame + b3_randf(0, 0.6f);
            const shz_sincos_t sc = shz_sincosf(a);
            p.p = v.hand + v3(sc.cos * 1.2f, b3_randf(-0.6f, 1.0f), sc.sin * 1.2f);
            p.v = (v.hand - p.p) * 3.4f;
            p.age = 0; p.life = 14; p.size0 = 0.35f; p.size1 = 0.15f; p.rot = 0; p.spin = 0.2f;
            p.cell = FX_STAR; p.additive = true; p.c0 = 0xFFC070FF; p.c1 = 0x40300060; p.grav = 0;
            fx_particle(p);
        }
        if (v.kind == K_WARDEN && v.dying && v.dead_t > 10 && v.dead_t < 130 && (g_b3.frame % 6) == 0)
            fx_burst(BURST_WISP, view_chest(v), v.pos);
        if (v.charge > 0 && (g_b3.frame & 1) == 0) {
            Particle p;
            const float a = b3_randf(0, 6.28f);
            const shz_sincos_t sc = shz_sincosf(a);
            p.p = v.hand + v3(sc.cos * 0.6f, b3_randf(-0.3f, 0.5f), sc.sin * 0.6f);
            p.v = (v.hand - p.p) * 3.0f;
            p.age = 0; p.life = 12; p.size0 = 0.25f; p.size1 = 0.35f; p.rot = 0; p.spin = 0;
            p.cell = v.charge_kind ? FX_STAR : FX_FLAME; p.additive = true;
            p.c0 = v.charge_kind ? 0xFFD0A0FF : 0xFFFFC050; p.c1 = v.charge_kind ? 0x40602090 : 0x40FF5010; p.grav = 0;
            fx_particle(p);
        }
    }
}

static void projectiles_step() {
    Seq* s = g_b3.queue.empty() ? nullptr : &g_b3.queue[0];
    for (int i = 0; i < g_b3.projs.size();) {
        Projectile& p = g_b3.projs[i];
        ++p.t;
        const float u = (float)p.t / (float)p.dur;
        p.pos = lerp(p.from, p.to, u) + v3(0, p.arc * 4.0f * u * (1.0f - u), 0);
        // trail
        if (p.kind == PROJ_WAVE) {                        // dark smoke rolling along the ground
            for (int k = 0; k < 2; ++k) {
                Particle w;
                w.p = p.pos + v3(b3_randf(-0.4f, 0.4f), b3_randf(-0.2f, 0.3f), b3_randf(-0.4f, 0.4f));
                w.v = v3(b3_randf(-0.4f, 0.4f), b3_randf(0.4f, 1.4f), b3_randf(-0.4f, 0.4f));
                w.age = 0; w.life = 24; w.size0 = 0.9f; w.size1 = 1.5f; w.rot = b3_randf(0, 6.28f); w.spin = 0.04f;
                w.cell = FX_SMOKE; w.additive = false; w.c0 = 0xD0180C28; w.c1 = 0x00100818; w.grav = 0;
                fx_particle(w);
            }
        }
        Particle tr;
        tr.p = p.pos; tr.v = v3(0, 0.3f, 0); tr.age = 0; tr.life = 10; tr.size0 = p.kind == PROJ_FIREBALL ? 0.5f : 0.35f; tr.size1 = 0.05f;
        tr.rot = b3_randf(0, 6.28f); tr.spin = 0; tr.cell = p.kind == PROJ_FIREBALL ? FX_FLAME : FX_GLOW; tr.additive = true;
        if (p.kind == PROJ_WAVE) { tr.c0 = 0xC08040C0; }
        tr.c0 = p.kind == PROJ_FIREBALL ? 0xC0FF9030 : 0xE0D0A0FF; tr.c1 = 0x00000000; tr.grav = 0;
        if (p.kind == PROJ_BOLT) { tr.size0 = 0.55f; tr.life = 14; }
        fx_particle(tr);
        if (p.t >= p.dur) {
            if (s) {
                const int k = p.hit;
                if (k < s->ev.hits.size()) {
                    View* tg = view_of(s->ev.hits[k].target);
                    if (tg && p.kind == PROJ_WAVE) {          // Soul Rend lands: the big one
                        fx_burst(BURST_DARK, view_chest(*tg), p.from);
                        impact(*s, k, true);
                        g_b3.hitstop = 8;
                        g_b3.cam.shake = T_SHAKE_BIG;
                    } else if (tg) {
                        fx_burst(p.kind == PROJ_FIREBALL ? BURST_FIRE : BURST_ARCANE, p.to, p.from);
                        impact(*s, k, false);
                        g_b3.cam.shake = p.kind == PROJ_FIREBALL ? T_SHAKE_MELEE : T_SHAKE_MELEE * 0.6f;
                        if (p.kind == PROJ_FIREBALL && tg->a->hp > 0) {
                            tg->burn_shown = true;
                            fx_text(*tg, "BURN", 0xFFFF8040, 2);
                        }
                    }
                }
            }
            g_b3.projs.swap_remove(i);
            continue;
        }
        ++i;
    }
}

// One presentation step (1/60 s of presentation time). Holding A runs two steps per tick.
static void step() {
    Seq* s = g_b3.queue.empty() ? nullptr : &g_b3.queue[0];
    const bool frozen = g_b3.hitstop > 0;
    if (frozen) --g_b3.hitstop;
    if (s && !frozen) {
        if (!s->started) {
            s->started = true;
            if (!start_seq(*s)) {
#ifdef B3_DEBUG_SEQ
                char m[96]; snprintf(m, sizeof m, "battle3d: no sequence for event kind %d skill %d hits %d", (int)s->ev.kind, s->ev.skill, s->ev.hits.size());
                eng::dbg_print(m);
#endif
                g_b3.queue.erase_first(); return;
            }
        }
        ++s->t;
        while (s->next < s->cues.size() && s->cues[s->next].t <= s->t) fire_cue(*s, s->cues[s->next++]);
        if (seq_done(*s)) {
            g_b3.queue.erase_first();
        }
    }
    views_step(1.0f / 60.0f, frozen);
    if (g_b3.caption_t > 0 && --g_b3.caption_t == 0) g_b3.caption = CAP_NONE;
    if (g_b3.namecard_t > 0) --g_b3.namecard_t;
    if (g_b3.ring_pulse_t > 0) --g_b3.ring_pulse_t;
    projectiles_step();
    particles_step();
    emitters_step();
    cam_tick();
    ++g_b3.frame;
}

// ---------------------------------------------------------------------------- public
void battle3d_init() {
    if (g_b3.inited) return;
    g_b3.inited = true;
    eng::boot_log("battle3d: animation library");
    const anim::Library* lib = anim::shared_library();    // shared with the field's NPCs (slice 3D-5)
    g_b3.lib_ok = lib != nullptr;
    if (lib) g_b3.lib = *lib;
    eng::boot_log("battle3d: models");
    for (int k = 0; k < K_COUNT; ++k) {
        KindRuntime& kr = g_b3.kinds[k];
        kr.model = models_get(KINDS[k].model);
        kr.ok = kr.model >= 0 && g_b3.lib_ok && anim::bind(g_b3.lib, models_at(kr.model).m, &kr.bind);
        kr.wpn[0] = KINDS[k].wpn_r ? models_get(KINDS[k].wpn_r, false) : -1;
        kr.wpn[1] = KINDS[k].wpn_l ? models_get(KINDS[k].wpn_l, false) : -1;
    }
    b3_draw_init();
    eng::boot_log("battle3d: arenas");
    g_b3.arena_ok = arena_load("/rd/arenas/village.txt", &g_b3.arenas[0]);
    g_b3.arena_ok = arena_load("/rd/arenas/barrow.txt", &g_b3.arenas[1]) && g_b3.arena_ok;
    g_b3.arena = &g_b3.arenas[0];
    char msg[160];
    snprintf(msg, sizeof msg, "battle3d: ready=%d (library %d, arena %d), model arena %d KiB", g_b3.lib_ok && g_b3.arena_ok,
             g_b3.lib_ok, g_b3.arena_ok, dmf::loaded_bytes() / 1024);
    eng::dbg_print(msg);
}

void battle3d_begin(BattleCtx& ctx, const EncounterDef& enc) {
    battle3d_init();
    g_b3.arena = &g_b3.arenas[0];
    for (int i = 0; i < 2; ++i)
        if (!strcmp(g_b3.arenas[i].name, encounter_arena(enc))) g_b3.arena = &g_b3.arenas[i];
    g_b3.boss_vi = -1; g_b3.boss_phase2 = false; g_b3.caption = CAP_NONE; g_b3.caption_t = 0;
    g_b3.namecard_t = 0; g_b3.ring_pulse_t = 0;
    g_b3.num_views = 0;
    g_b3.queue.clear(); g_b3.parts.clear(); g_b3.texts.clear(); g_b3.projs.clear();
    g_b3.hitstop = 0; g_b3.frame = 0; g_b3.rng = 0x1234567u;
    g_b3.outro = 0; g_b3.outro_t = 0; g_b3.skip_hold = 0;
    const Arena& A = *g_b3.arena;
    int n_enemies = 0;
    for (const Actor& a : ctx.actors) if (a.team == Team::Enemy) ++n_enemies;
    const int slots = n_enemies < 1 ? 1 : (n_enemies > 4 ? 4 : n_enemies);
    int ei = 0;
    for (int i = 0; i < ctx.actors.size() && g_b3.num_views < MAX_ACTORS; ++i) {
        Actor& a = ctx.actors[i];
        View& v = g_b3.views[g_b3.num_views++];
        memset(&v, 0, sizeof v);
        v.a = &a;
        v.kind = kind_of(a, &enc, a.team == Team::Enemy ? ei : -1);
        v.present = true;
        v.scale = T_CHAR_SCALE * KINDS[v.kind].scale;
        if (a.team == Team::Player) {
            v.home = v3(A.hero_x, 0, A.hero_z);
            v.home_yaw = 1.57079633f;
        } else {
            const int s = ei < 4 ? ei : 3;
            v.home = v3(A.slot_x[slots - 1][s], 0, A.slot_z[slots - 1][s]);
            v.home_yaw = -1.57079633f;
            ++ei;
        }
        v.pos = v.home; v.yaw = v.yaw_goal = v.home_yaw;
        v.shown_hp = a.hp; v.bar = (float)a.hp; v.alpha = 1.0f;
        v.lit_bucket = -1;
        const KindDef& kd = KINDS[v.kind];
        v.c_idle = clip(kd.idle); v.c_hit = clip(kd.hit); v.c_death = clip(kd.death);
        v.c_atk[0] = clip(kd.atk0); v.c_atk[1] = clip(kd.atk1);
        v.collapse_t = anim::clip_event(g_b3.lib, v.c_death, anim::EV_COLLAPSE);
        if (v.collapse_t < 0) v.collapse_t = 30;
        anim::animator_init(v.an, v.c_idle, 0.37f * (float)i);
        if (a.team == Team::Enemy) {                         // hold the awaken clip's first frame until the intro wakes them
            const int aw = clip("skel_awaken");
            if (aw >= 0) { anim::animator_init(v.an, aw, 0.0f); v.an.t = 0.0f; v.an.then = -1; }
            v.asleep = true;
        }
        if (!a.hp) { v.present = false; }
        if (a.role == ROLE_BOSS) g_b3.boss_vi = g_b3.num_views - 1;
        v.hand = v.pos + v3(0, 1.2f, 0);
    }
    eng::set_clear_color(A.sky);
    b3_prebake();
    // intro: the camera starts on the establishing shot
    cam_set_goal(SHOT_INTRO, -1, -1, 1.0f);
    g_b3.cam.cur = shot_for(SHOT_INTRO, nullptr, nullptr);
    g_b3.cam.shake = 0.0f;
    Seq s;
    s.kind = SEQ_INTRO; s.started = false; s.ended = false; s.t = 0; s.next = 0;
    s.ev.kind = EvKind::Action; s.ev.user = nullptr; s.ev.skill = -1; s.ev.result = ResultKind::Noop; s.ev.dmg = 0;
    g_b3.queue.push(s);
}

void battle3d_tick(BattleCtx& ctx) {
    if (!g_b3.num_views) return;
    if (g_b3.skip_hold > 0) --g_b3.skip_hold;          // a skip keeps the tick of its press busy (below)
    // new events -> queued sequences (built when they start)
    for (const BattleEvent& ev : ctx.events) {
        Seq s;
        s.kind = SEQ_NONE; s.started = false; s.ended = false; s.t = 0; s.next = 0; s.ev = ev;
        if (!g_b3.queue.push(s)) eng::dbg_print("battle3d: sequence queue full");
    }
    ctx.events.clear();
    // the intro: A skips it (the press is consumed here: busy stays true this tick)
    if (!g_b3.queue.empty() && g_b3.queue[0].kind == SEQ_INTRO && g_b3.queue[0].started && input_pressed(eng::KEY_FIRE)) {
        for (int i = 0; i < g_b3.num_views; ++i) {
            View& v = g_b3.views[i];
            if (v.kind != K_HERO && v.present) view_play(v, v.c_idle, 0.15f, -1);
            v.asleep = false;
        }
        g_b3.queue.erase_first();
        cam_set_goal(SHOT_OVERVIEW, -1, -1, 0.2f);
        g_b3.skip_hold = 1;
        if (g_b3.namecard_t > 20) g_b3.namecard_t = 20;        // fade the name card out
    }
    // UI-driven camera when no sequence plays
    if (g_b3.queue.empty()) {
        int shot = SHOT_OVERVIEW, t = -1;
        if (ctx.state == BattleState::Command && ctx.active && ctx.active->team == Team::Player) {
            if (ctx.ui.state == UiState::Target) {
                ActorList en = alive_enemies(ctx, *ctx.active);
                if (!en.empty()) { shot = SHOT_LEAN; t = vidx(view_of(en[clampv(ctx.ui.target_i, 0, en.size() - 1)])); }
            } else if (ctx.ui.state == UiState::TargetAll) {
                shot = SHOT_PULLBACK;
            }
        }
        if (ctx.state == BattleState::Win || ctx.state == BattleState::Lose) {
            if (!g_b3.outro) {
                g_b3.outro = ctx.state == BattleState::Win ? 1 : 2;
                g_b3.outro_t = 0;
                View* h = nullptr;
                for (int i = 0; i < g_b3.num_views; ++i) if (g_b3.views[i].kind == K_HERO) h = &g_b3.views[i];
                if (h && g_b3.outro == 1) { h->yaw_goal = 0.6f; view_play(*h, clip("jump_short"), 0.15f, h->c_idle); }
                if (g_b3.outro == 2)
                    for (int i = 0; i < g_b3.num_views; ++i) {
                        View& v = g_b3.views[i];
                        if (v.kind != K_HERO && v.present && !v.dying) view_play(v, clip("skel_taunt"), 0.15f, v.c_idle);
                    }
            }
            shot = g_b3.outro == 1 ? SHOT_VICTORY : SHOT_DEFEAT;
            for (int i = 0; i < g_b3.num_views; ++i) if (g_b3.views[i].kind == K_HERO) t = i;
            cam_set_goal(shot, t, -1, 0.06f);
        } else {
            cam_set_goal(shot, -1, t, T_CAM_RATE);
        }
    }
    if (g_b3.outro) ++g_b3.outro_t;
    // hold A during a sequence: twice the speed (not during the intro: A skips it)
    const bool fast = T_FAST > 0.5f && input_held(eng::KEY_FIRE) && !g_b3.queue.empty() && g_b3.queue[0].kind != SEQ_INTRO;
    step();
    if (fast) step();
    g_b3.ctx = &ctx;
}

bool battle3d_busy() { return !g_b3.queue.empty() || g_b3.skip_hold > 0; }

Battle3dInfo battle3d_info() {
    Battle3dInfo r;
    memset(&r, 0, sizeof r);
    r.ready = g_b3.lib_ok && g_b3.arena_ok;
    r.seq_kind = g_b3.queue.empty() ? SEQ_NONE : g_b3.queue[0].kind;
    r.seq_t = g_b3.queue.empty() ? 0 : g_b3.queue[0].t;
    r.queued = g_b3.queue.size();
    r.busy = battle3d_busy();
    r.intro = !g_b3.queue.empty() && g_b3.queue[0].kind == SEQ_INTRO;
    r.outro = g_b3.outro != 0;
    r.particles = g_b3.parts.size();
    r.projectiles = g_b3.projs.size();
    r.cam_eye[0] = g_b3.cam.cur.eye.x; r.cam_eye[1] = g_b3.cam.cur.eye.y; r.cam_eye[2] = g_b3.cam.cur.eye.z;
    r.cam_fov = g_b3.cam.cur.fov;
    r.cam_target[0] = g_b3.cam.cur.target.x; r.cam_target[1] = g_b3.cam.cur.target.y; r.cam_target[2] = g_b3.cam.cur.target.z;
    for (int i = 0; i < g_b3.num_views && i < MAX_ACTORS; ++i) r.shown_hp[i] = g_b3.views[i].shown_hp;
    r.n_enemies = g_b3.n_enemy_sx;
    for (int i = 0; i < g_b3.n_enemy_sx; ++i) r.enemy_sx[i] = g_b3.enemy_sx[i];
    r.cost_chars = g_b3.cost[0]; r.cost_weapons = g_b3.cost[1]; r.cost_ground = g_b3.cost[2]; r.cost_props = g_b3.cost[3]; r.cost_fx = g_b3.cost[4];
    return r;
}

} // namespace bt
