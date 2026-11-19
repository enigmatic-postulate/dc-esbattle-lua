// screens.h - the frame around the game (slice 3D-6, GDD 5, 13, 14; docs/NEXT_SLICE_BARROW.md 5.8-5.10): the title's
// logo and menu, the title cards (T2's text card, THE END, the boss's name card), the death screen, the credits roll
// and the results card. 2D only: the backdrops are the field renderer's (the title's dusk orbit, the credits'
// flyover) or the battle's (the death screen), drawn first by game.cpp. Logic in ticks and integers; the 2D draws
// never change state.
#pragma once
#include <stdint.h>

namespace bt {

bool screens_init();                        // the logo texture (romdisk/tex/logo.tex) and credits.txt, once at boot

// ---- title (GDD 5): the logo fades in over 45 t, PRESS START blinks (30 t), Start or A opens the menu (12 t grow)
enum TitleAction { TITLE_NONE, TITLE_NEW_GAME, TITLE_CREDITS };
void title_enter(bool cleared);             // at the logo again (T1 / T11 just ran)
TitleAction title_tick();                   // input after the logo is in
void title_draw();
int  title_idle_ticks();                    // ticks without any input (the attract demo starts at 2,400)
// The title's backdrop camera: a slow orbit around the village square (one loop = 60 s), tick t.
void title_orbit(int t, float eye[3], float look[3], float* fov);

// ---- a centred title card over the scene (the script's `card`), and the boss's name card (`namecard`)
void card_draw(const char* text, int t, int len, bool logo);   // fades in 20 t, out 20 t
void namecard_draw(const char* text, int t, int len);          // the battle's style: a band, gold rule
void t2_card_draw(int t);                                      // "Hollowmere. The night the barrow woke." (90 t)

// ---- death screen (GDD 13): presentation ticks since the loss; options from tick 100, input from 160
enum DeathTip { TIP_BOSS, TIP_PATROL, TIP_POTIONS };
enum DeathAction { DEATH_NONE, DEATH_RETRY, DEATH_TITLE };
const int DEATH_OPTIONS_AT = 100, DEATH_INPUT_AT = 160;
void death_enter(DeathTip tip);
DeathAction death_tick(int t);
void death_draw(int t);
int  death_choice();                        // 0 retry, 1 title (traces)

// ---- credits roll (GDD 14 E5): romdisk/scripts/credits.txt scrolling; Start skips; ~60 s
void credits_enter();
bool credits_tick(int t);                   // true when finished (rolled out, or Start)
void credits_draw(int t);
void credits_flyover(int t, float eye[3], float look[3], float* fov);   // the village flyover under it
int  credits_length();                      // ticks of the full roll

// ---- results card (GDD 14 E6)
struct SessionStats { int play_ticks, battles_won, hero_turns, dmg_dealt, dmg_taken, potions_used, retries; };
enum Rank { RANK_GOLD, RANK_SILVER, RANK_BRONZE };
Rank results_rank(const SessionStats& s);   // D8: Gold < 20 min and 0 retries; Silver < 30 min and <= 2; else Bronze
void results_enter(const SessionStats& s);
bool results_tick(int t);                   // A (after 60 t) -> true
void results_draw(int t);

}
