// portrait.h - speaker portraits (slice 3D-5, task E2; GDD 8): two VRAM slots of 128x128 RGB565
// (32 KiB each), slot 0 for whoever speaks other than Rowan, slot 1 for Rowan. Files:
// /rd/portraits/<base>_<variant>.dtx (tools/dialog/make_portraits.py): a 32-byte header
// ('DTX1', u16 w, h, fmt = 1 RGB565, flags, 22 bytes zero) + linear texels.
//
// The files are read once, after the scripts load (portrait_preload); uploads happen at a script's start (during T3, before the box shows) and when a page needs another
// speaker or variant. A slot is rewritten only when the last 2 frames did not draw it (the Dreamcast's
// PVR renders the previous frame while a tick runs): a slot on screen first hides for 2 frames - a
// short blink on an expression change - then uploads. Deterministic: counted in drawn frames.
#pragma once
#include "engine.h"

namespace bt {

const int PORTRAIT_SLOTS = 2;
const int PORTRAIT_SIZE = 128;
const int PORTRAIT_HIDE_FRAMES = 2;

bool portrait_init();                             // the two slot textures (blank); false if no slot is left
// Reads every speaker's three portraits into a static RAM cache (after script_load: the speakers are known),
// so that uploads during play do no file I/O and no allocation (DoD T3). PORTRAIT_CACHE files at most;
// a missing one is logged here, at load time. Returns the number of files cached.
const int PORTRAIT_CACHE = 9;                     // 3 speakers x 3 variants (288 KiB of RAM)
int  portrait_preload();
// The slot should show speaker `speaker` (script speaker index) in `variant`. Loads now if the slot is
// off screen, else after the blink. Unknown files log once and leave the slot as it was.
void portrait_request(int slot, int speaker, int variant);
void portrait_tick();                             // runs a pending upload once the slot has been hidden long enough
bool portrait_ready(int slot);                    // the requested image is in VRAM (draw it)
bool portrait_pending(int slot);                  // an upload is waiting (the slot must stay hidden)
void portrait_drawn(int slot);                    // the slot was drawn this frame (call from the draw)
void portrait_frame_end();                        // once per game_draw: counts frames a slot was not drawn
eng::Sprite portrait_sprite(int slot);
int  portrait_uploads();                          // uploads so far (traces / budget tests)
int  portrait_loaded(int slot, int* variant);     // speaker in VRAM (-1: none)
void portrait_reset();                            // forget what is loaded (new session on a new map)

}
