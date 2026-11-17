// dialog_ui.h - the dialog's 2D presentation and input (slice 3D-5, task D1; GDD 6 T3/T4, 7.1, 7.3, 7.5):
// letterbox, box, name plate, portraits, typewriter text, advance marker, choices, sys: pages and the
// in-dialog fades. It reads the script VM's blocking op (script.h) and answers it: "page read" and
// "option k picked". Everything is counted in ticks and integers (deterministic; host traces).
//
// Layout at 640x480 (docs/NEXT_SLICE_DIALOG.md 5.2): bars 0-36 / 444-480; box (24,318) 592x118, TR,
// alpha 0.88; portrait (31,325) 104x104 in a 2 px gold frame; text x 150, lines at y 334/356/378;
// plate (144,300) 96x22 in the speaker's colour; advance marker (601,421); choice box right-aligned
// at x 616, bottom edge 24 px above the box, rows 26 px.
#pragma once
#include <stdint.h>

namespace bt {

// Timings (ticks at 60 Hz): GDD 6 T3/T4 and 7.1/7.5.
const int DLG_BARS_TICKS = 12;        // letterbox in / out
const int DLG_BOX_AT = 12;            // the box and the first page appear when the bars are in
const int DLG_CAM_TICKS = 20;         // camera ease in / out (field3d)
const int DLG_BAR_PX = 36;
const int DLG_SLIDE_TICKS = 8;        // a new speaker's portrait slides in
const int DLG_PAUSE_STOP = 6;         // after . ! ?
const int DLG_PAUSE_COMMA = 3;        // after ,
const int DLG_FAST = 4;               // glyphs per tick while B is held
const int DLG_STICK_STEP = 64;        // the stick crossing +-64 moves the choice cursor

// A new session: T3 starts now (tick 0). npc: the entity spoken to (-1: a script with no NPC, e.g.
// after_battle - no camera move). Reads the VM's first blocking op (preloads its portrait).
void dialog_begin(int npc);
// One tick of the open dialog (Dialog mode): typewriter, input, answers the VM. Returns true while the
// script still runs; false on the tick it ended (T4 starts; the game returns to the Field at once).
bool dialog_tick();
void dialog_tick_closing();           // T4 (Field mode): the bars slide out
void dialog_reset();                  // no dialog at all (a checkpoint retry)
bool dialog_open();                   // a session runs (Dialog mode)
bool dialog_visible();                // open or closing (bars on screen)
int  dialog_npc();                    // the NPC spoken to (-1: none)
int  dialog_t();                      // ticks since T3 began (open) / since T4 began (closing)
int  dialog_bars_px();                // current letterbox height
float dialog_cam_weight();            // 0 = field camera .. 1 = dialog shot (smoothstep; T3 in, T4 out)
bool dialog_hides_hud();              // the field HUD is hidden (open, or the bars still on screen)
void dialog_draw();                   // everything above the 3D scene (call after field3d_draw)
// script VM host hooks
void dialog_fade(bool out, int ticks);
void dialog_letterbox(bool on);

// Trace (host 'G' lines): the UI state this tick.
struct DialogInfo {
    bool open, closing, box;
    int  t, bars, page_pc, shown, total, pause;
    bool complete, marker;
    int  choice_pc, sel, n_opts, cancel;
    int  speaker, variant, slot, slide;       // the page's speaker (-1 sys), its portrait slot, slide ticks left
    bool portrait_shown;
    int  fade;                                // in-dialog fade level 0..255
    int  cam_w1000;                           // camera weight x 1000
};
DialogInfo dialog_info();

}
