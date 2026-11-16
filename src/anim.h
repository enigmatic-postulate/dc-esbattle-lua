// anim.h - shared animation library (.dma, docs/DMA.md) and the Animator (slice 3D-4).
//
// Every KayKit character uses the Rig_Medium skeleton, so one copy of each clip serves them all:
// the library is loaded once (one file read into a static arena, validated, no parsing, no heap)
// and bound to a model by bone name. Sampling follows the .dmf pose rule (dmf.h), with two
// additions: any bone a clip moves may carry a translation (the skeletons' death throws a leg
// 2 m), and clips carry event times (impact, release, collapse) in 60 Hz ticks.
//
// Reference implementation: tools/battle3d/dmfio.py (read_dma, sample_clip); the two are
// cross-checked by tests/battle3d/run.sh (anim crosscheck).
#pragma once
#include <stdint.h>
#include "dmf.h"

namespace anim {

enum { CLIP_LOOP = 1 };
enum Event { EV_IMPACT = 0, EV_RELEASE = 1, EV_COLLAPSE = 2, EV_COUNT = 4 };
const uint16_t EV_NONE = 0xFFFF;
const int MAX_TBONES = 12;
const uint8_t NO_BONE = 0xFF;

struct Header { char magic[4]; uint16_t version, num_bones; uint32_t file_size; uint16_t num_clips, reserved;
                uint32_t off_names, off_clips; uint8_t pad[40]; };
struct Clip   { char name[20]; uint16_t num_frames; uint8_t flags, num_tbones; float fps; uint32_t t_mask;
                uint16_t ev[4]; float bound[4]; uint32_t off_data, stride; };

struct Library {
    const uint8_t* base;
    const Header*  h;
    const char*    names;      // num_bones x 16 bytes
    const Clip*    clips;
    int            num_bones, num_clips;
};

// Validates an in-memory image (used by load() and by the tests).
bool parse(const void* data, int size, Library* out, char* err, int errlen);
// Reads /rd/... into the library arena (static, 32-byte aligned) and validates it.
bool load(const char* path, Library* out);

int   clip_find(const Library& L, const char* name);            // -1 if absent
int   clip_ticks(const Library& L, int clip);                   // one pass, in 60 Hz ticks
int   clip_event(const Library& L, int clip, Event e);          // tick of the event, -1 if none
bool  clip_loops(const Library& L, int clip);

// Model bone i <-> library bone, by name (KayKit models match 1:1; others map what they share).
struct Binding { uint8_t lib_of[dmf::MAX_BONES]; uint8_t model_of[dmf::MAX_BONES]; int matched; };
bool bind(const Library& L, const dmf::Model& m, Binding* out);   // false if no bone matched

// Pose of `clip` at t_sec (loops wrap, one-shots clamp to their last frame). Bones the library
// lacks keep their bind pose.
void sample(const Library& L, const Binding& b, const dmf::Model& m, int clip, float t_sec, dmf::Pose* out);

// ---------------------------------------------------------------------------- Animator
// One clip playing, optionally cross-fading from the previous one. One-shot clips either hold
// their last frame or hand over to `then` (usually the idle) with the same fade.
struct Animator {
    int16_t clip, prev, then;
    float   t, prev_t;         // seconds into clip / prev
    float   fade, fade_rate;   // 0..1 weight of `clip` over `prev`; rate per second
    float   then_fade;         // fade (seconds) used for the hand-over to `then`
    float   hold_at;           // >= 0: the clip stops at this time and holds (a wind-up held: the CHARGE)
    bool    done;              // a one-shot reached its end (and holds, or has handed over)
};
void animator_init(Animator& a, int clip, float t = 0.0f);
// fade_sec <= 0: cut. then = -1: hold the last frame (one-shots).
void play(Animator& a, const Library& L, int clip, float fade_sec, int then = -1, float start_sec = 0.0f);
void tick(Animator& a, const Library& L, float dt_sec);
void hold_at(Animator& a, float t_sec);     // freeze the current clip once it reaches t_sec
void pose(const Animator& a, const Library& L, const Binding& b, const dmf::Model& m, dmf::Pose* out);

} // namespace anim
