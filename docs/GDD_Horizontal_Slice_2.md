# PROJECT HYMN - Game Design Document
## Horizontal Slice 2: "The Restless Dead", the full chapter

Sega Dreamcast (SH4 / PowerVR2, KallistiOS 2.3.0) - 640x480, 60 fps
**Version 2.0 - planning only, no implementation in this step** (2026-11-21)

Builds on horizontal slice 1 (`docs/GDD_Horizontal_Slice.pdf`, v1.0: slices 3D-1 to 3D-7). Where this document is
silent, v1 still holds: the transition catalogue, the dialog system, the script format, the portraits, the death
screen, the title and the credits are unchanged unless a section here says otherwise. Section numbers written
"v1 12.3" point into v1. Every number is a starting value, tuned later with the harness bot. Decisions the owner
took on 2026-11-21 are marked **[decided]**; open ones are collected in §17.

---

## 1. Purpose and scope

Slice 1 proved every system once, in 5-10 minutes. Slice 2 turns it into a **35-40 minute chapter of a classic
RPG**: a party, levels, equipment, an inventory, a shop, more actions in battle, two new zones, a mid-boss, a
story in gated acts. About 2-3x the content on the same engine.

### 1.1 What the slice must contain

| Area | Deliverable | Section |
|---|---|---|
| Story | five acts, each gated by something the player does; one side quest; dialog choices that are remembered | 2, 10 |
| Zones | Hollowmere (expanded), the Old Road (new), the barrow grounds (reworked), the crypt (new, 4 rooms) | 5 |
| Party | Rowan and Wren, built for a third member **[decided]** | 6 |
| Battle | command menu with submenus, skill points, 10 skills, 3 elements, 5 status effects, items, Flee | 6 |
| Progression | levels 1-8, XP, a victory panel with level-ups | 7 |
| Equipment, inventory | 3 slots per member, ~12 pieces, ~8 consumables, key items, a party menu | 8 |
| Economy | gold, chests, Brom's shop, forge and inn | 8 |
| Enemies | 6 enemy types (3 new), a mid-boss, the Warden with a third phase | 9 |
| Saving | VMU save at save points, CONTINUE, larger payload | 10.4 |
| Audio | 4 more music tracks, ~15 more effects, music streamed from the asset root | 12 |
| Technology | assets off the romdisk (disc image or PC over the broadband adapter), per-zone loading | 13 |
| Art | every asset mapped to its work and its slice | 14 |

### 1.2 Target experience

* **Length:** 35-40 minutes for a first-time player; about 20 for someone who knows the route.
* **Tone:** as v1 (a warm diorama village, something old and sad under it), with a darker second half: the road at
  nightfall, the crypt.
* **Pillars (v1's four, plus two):** (1) readable at a glance from the diorama camera; (2) every action has a
  satisfying 3D response; (3) no dead ends: the player always knows the next objective; (4) 60 fps on a real
  Dreamcast; **(5) every fight teaches or tests one thing**; **(6) the player is never more than five minutes from
  a reward** (a level, a chest, a new skill, a piece of equipment, a story beat).

### 1.3 Baseline: what exists after slice 1

| System | State today |
|---|---|
| Flow | title, intro, field, dialog, battle, map change, death, ending, credits, results; 11 transitions |
| Field | two maps (village 40x30, barrow), analog movement, see-through props, fixed diorama camera, painted ground |
| Characters | Rowan (Knight), Wren (Mage), Brom (Barbarian), Corwin (Ranger); Skeleton Warrior / Rogue / Mage; the Warden |
| Battle | 3D, one hero vs up to 3; Attack, Fire (+Burn), Guard, Potion; agility turn order; boss with 2 phases |
| Quest | one quest, 8 stages, flags, RAM checkpoints, objective HUD |
| Audio | AICA backend, 8 tracks, 34 effects (slice 3D-7, in progress) |
| Save | planned in slice 3D-7 (not built yet) |
| Build | a `.elf` with a 5.3 MB romdisk held in RAM; 3.25 MiB of main RAM free |

### 1.4 Out of scope

A third playable member (the systems allow one; the content does not ship one). A world map. Random encounters
(every enemy is visible, as in v1). Crafting beyond one forge upgrade. Living (non-undead) monsters. Voice.
Localisation (hooks only). Multiple save slots.

---

## 2. Premise, cast and story

### 2.1 Premise

As v1: the Bone Warden's seal has cracked and the dead walk the fields of Hollowmere. New in v2: the seal did not
crack by itself. Someone opened the crypt under the barrow and took its lantern flames; the Warden's captain
guards what is left. Rowan and Wren follow the old road to the barrow, relight the grave-lanterns, go down into
the crypt, and lay captain and Warden to rest. Who opened the crypt is left for the next chapter (the traveller
knows more than she says).

### 2.2 Cast

| Character | Model | Role in v2 |
|---|---|---|
| **Ser Rowan** | Knight | player character; the party's fighter |
| **Wren** | Mage | quest giver in act 1, then **party member**: the mage and healer |
| **Brom** | Barbarian | smith: shop, forge, inn; gives the ore errand |
| **Corwin** | Ranger | gate watch; opens the east gate to the road |
| **Tamsin** (working name) | Rogue (hooded) | the wounded traveller on the Old Road; **the future third party member** **[decided]**; an NPC in this slice |
| Villagers (3-4) | Rogue, and recolours of Barbarian / Ranger / Mage | one-line barks; one of them starts nothing, all of them react to the story's stage |
| Skeletons | Skeleton Warrior / Rogue / Mage | patrols, and the base of the three new enemy types (§9) |
| **The Bone Captain** | Skeleton Warrior at 1.3x, two-handed sword | mid-boss in the crypt |
| **The Bone Warden** | Skeleton Mage at 1.5x (as today) | boss, three phases |

### 2.3 Acts

| Act | Zone | Min | Beats | Gate |
|---|---|---|---|---|
| **1. Dusk** | Hollowmere | 7-8 | intro; Wren's request and the Fire lesson; Brom's shop opens (first gold from the first patrol); the ore errand offered; two patrols; the report | **Wren joins**; Corwin opens the east gate |
| **2. Nightfall** | the Old Road | 6-8 | first two-member battles; the broken bridge and the ford around it; chests off the path; Tamsin, wounded (a choice: give a potion or not); the camp (rest, save); Brom's ore in the old cart | reach the barrow fence |
| **3. Night** | barrow grounds | 5-6 | patrols with the new enemies; the waystone (save); the crypt door is dark; two grave-lanterns, each guarded | both lanterns lit: the crypt opens |
| **4. Below** | the crypt | 10-12 | antechamber (a lever and a gate); ossuary (the brazier puzzle, its order read from three gravestones); armoury (the best equipment, behind a spike corridor); shrine (save); the Bone Captain | the Captain's key opens the ring chamber |
| **5. The Warden** | the ring | 5-6 | the boss in three phases; dawn; the gathering; credits; results | - |

About 13-15 battles, two bosses, one puzzle, one side quest, five chests, four save points.

---

## 3. Game flow

v1's flow plus four additions.

| Addition | Behaviour |
|---|---|
| **Party menu** | **X** in the field (not in dialog, battle or a transition) opens Party / Items / Equipment / Quests. B closes. The game clock stops. **[decided: separate from the pause menu]** |
| **Pause menu** | **Start**, as slice 3D-7 specifies: Resume / Options / Return to title |
| **Shop** | a dialog branch with Brom opens the shop screen (Buy / Sell / Forge / Rest) over the field; no interior map |
| **Zone loading** | a map change (T7) unloads the zone's models and textures and loads the next zone's from the asset root, under the black. The party's models, the UI and the battle set stay loaded |
| **Save points** | an interactable (inn, camp, waystone, shrine): "Rest and save?" restores HP and SP, revives, saves to the VMU |

Battles end with a **victory panel** (XP, gold, drops, level-ups) between the VICTORY banner and T6.

## 4. Input

| Button | Field | Battle | Menus |
|---|---|---|---|
| Stick / D-pad | move | choose | choose |
| A | talk, open, confirm | confirm | confirm |
| B | - | back | back / close |
| X | **party menu** (new; the engine reads X from now on) | - | - |
| Y | - | jump to Guard (as today) | sort (items) |
| Start | pause | - | - |
| L / R | - | - | switch member (equipment, party) |

---

## 5. Zones

Tile size, camera and collision as today. Each zone has a painted ground texture (512x512 VQ, one slot, as the
village), its own light presets and its own model set.

| Zone | Size | Contents | Encounters | Save |
|---|---|---|---|---|
| **Hollowmere** | 40x30 (today's map) | + shop dressing at Brom's (barrels, crates, a keg, a sign), + 3-4 villagers, + 1 chest behind the south houses | 2 patrols (as v1), solo | Brom's inn |
| **The Old Road** | 48x24, west to east | woodland either side, a stream with a broken bridge (the ford is north, past a patrol), an overturned cart (Brom's ore), a camp clearing, Tamsin under a tree, 2 chests | 4 visible patrols, 1 avoidable; 1 scripted ambush at the bridge | the camp |
| **Barrow grounds** | today's barrow map, extended north | + two grave-lanterns (north-west, south-east), + the crypt door in the mound, + 1 chest | 4 patrols (2 guard the lanterns) | the waystone |
| **Crypt: antechamber** | 16x12 | lever, portcullis, torches, rubble | 1 patrol | - |
| **Crypt: ossuary** | 16x14 | four braziers, three gravestones with the clue, bone piles, the sealed inner door | 2 patrols; a wrong brazier order raises a pack of Risen | - |
| **Crypt: armoury** | 12x10 | spike corridor (timed), weapon racks, the iron chest (equipment), 1 wooden chest | 1 patrol | - |
| **Crypt: the Captain's hall** | 16x12 | pillars, banners, the shrine at its entrance, the ring door behind the Captain | the Bone Captain | the shrine |
| **The ring** | today's boss arena | as v1 | the Warden | - |

The crypt's four rooms are four small maps joined by doorways (T7, 12 + 12 ticks) and share one model set and one
ground atlas, so moving between them loads nothing.

**The puzzle (ossuary).** Three gravestones each carry a line ("...first the eldest...", a name and a year). The
four braziers bear four names. Lighting them from the eldest to the youngest opens the inner door; a wrong order
puts them out and raises 3 Risen (a fight, not a failure). The fourth name is on no stone: it is the one left.
Wren comments after the second mistake (pillar 3: no dead ends).

**The spike corridor (armoury).** Floor spikes on a 2-second cycle, three rows. Touching them costs 3 HP each (never
the last HP) and pushes the party back a tile. Optional: it guards the best equipment, not the way forward.

---

## 6. Party and battle

### 6.1 Party

* Up to **three** slots in every structure (rules, UI, save); **two** are filled in this slice.
* Act 1 is solo (Rowan), exactly as v1's tutorial fights. Wren joins by script at the report.
* A member at 0 HP is **down**: no turns, revived by a Revive Charm (25 % HP) or at a save point (full). The
  battle is lost when every member is down. After a won battle a downed member stands up with 1 HP.
* In the field only the leader (Rowan) is shown walking; the others appear in dialog and cutscenes (as v1 stages
  its NPCs). **(open, §17 O6: followers on the field.)**

### 6.2 Stats

HP, SP, STR (physical damage), DEF, INT (magic damage and healing), MDEF, AGI (turn order). v1's formulas stay:
physical = STR - DEF, magic = (INT - MDEF) x the skill's factor, minimum 1; Guard halves physical damage until
the guard's next turn.

| | Lv | HP | SP | STR | DEF | INT | MDEF | AGI |
|---|---|---|---|---|---|---|---|---|
| Rowan | 1 | 20 | 6 | 6 | 2 | 2 | 2 | 6 |
| Rowan | 8 | 48 | 20 | 13 | 6 | 4 | 5 | 9 |
| Wren | 2 (joins) | 16 | 14 | 3 | 1 | 7 | 4 | 7 |
| Wren | 8 | 36 | 34 | 5 | 3 | 14 | 8 | 10 |

Wren's blessing (v1: max HP 30) is replaced by levels: Rowan is level 2-3 at the report.

### 6.3 Turn order

Every actor acts once per round in AGI order (ties: party first, then slot order), as today with more actors.
The battle HUD shows the next five turns as a strip of small portraits (new; needed once the party is two).

### 6.4 Commands

`Attack` / `Skills >` / `Guard` / `Items >` / `Flee`. The ring of v1 becomes a short vertical list with two
submenus; Y still jumps to Guard. A skill the member has not learned is not listed; one that costs more SP than
is left is greyed (`menu_denied`).

### 6.5 Skills

| Member | Skill | Lv | SP | Target | Effect | Presentation (clip, FX) |
|---|---|---|---|---|---|---|
| Rowan | Attack | 1 | 0 | one enemy | STR - DEF | as today |
| Rowan | Guard | 1 | 0 | self | halves physical damage until his next turn | as today |
| Rowan | **Cleave** | 3 | 4 | all enemies | 0.7 x physical each | wide horizontal slice (`slice_h`), an arc of sparks |
| Rowan | **Shield Bash** | 5 | 5 | one enemy | physical + **Stun** (skips its next turn; bosses: -50 % AGI for a round instead) | block-and-strike clip, a white burst |
| Rowan | **Rally** | 7 | 6 | party | **Defence up** for 3 rounds (+50 % DEF) | cheer clip, a gold ring under each member |
| Wren | Staff | 2 | 0 | one enemy | STR - DEF (weak; her fallback) | one-handed chop |
| Wren | **Fire** | 2 | 4 | all enemies | 0.65 x magic + **Burn** 2 for 2 turns (v1's Fireball; Rowan no longer casts it after she joins) | as today |
| Wren | **Mend** | 2 | 3 | one ally | heals 8 + INT | use-item clip, the green heal burst |
| Wren | **Frost** | 4 | 5 | one enemy | 1.2 x magic, element frost + **Slow** (-50 % AGI, 2 rounds) | the bolt recoloured blue-white, ice shards |
| Wren | **Radiance** | 6 | 8 | all enemies | 1.0 x magic, element holy | arms raised, a white-gold flash from above |

Before Wren joins, Rowan keeps v1's learned **Fire** (cast from a scroll Wren gives him; SP 4); when she joins the
scroll is hers and his list is the fighter's. This keeps v1's "one moment of progression" in act 1.

### 6.6 Elements

| Element | Weak to it | Resists it |
|---|---|---|
| Fire | Risen, Sentry (its wooden shield) | Grave Wisp |
| Frost | Grave Wisp | Skeleton Mage |
| Holy | every undead (x1.25), the Captain (x1.5) | - |
| Physical | - | Sentry while its shield is up |

Weak = x1.5, resists = x0.5. The first hit shows WEAK or RESIST over the target; from then on the target cursor
carries a small icon.

### 6.7 Status effects

| Status | Effect | Source | Cure |
|---|---|---|---|
| Burn | damage each turn, 2 turns | Fire, Grave Fire, Fire Flask | Salve, time |
| Stun | skips the next turn | Shield Bash, the Captain's Hilt Strike | time |
| Slow | -50 % AGI, 2 rounds | Frost, the Wisp's Chill | Salve, time |
| Defence up / down | +-50 % DEF, 3 rounds | Rally / the Captain's Sunder | time |
| Charging | v1's telegraph | the Warden, the Captain | - |

One damage-over-time effect only (Burn): **(open, §17 O1)** whether to add Poison.

### 6.8 Items in battle, and Flee

* **Items >** lists the consumables (§8.2); using one takes the member's turn.
* **Flee**: succeeds with 75 % (+25 % per failed attempt), never in boss or scripted battles (the command is
  greyed). The party returns to the field one tile back; the enemy group stays where it was. A Smoke Bomb always
  succeeds.

---

## 7. Progression

| Level | XP to reach | Typical point in the chapter |
|---|---|---|
| 1 | 0 | start |
| 2 | 10 | after the first patrol |
| 3 | 26 | the report, after both patrols (Cleave) |
| 4 | 56 | mid road (Wren: Frost) |
| 5 | 96 | the barrow fence (Shield Bash) |
| 6 | 150 | the lanterns (Wren: Radiance) |
| 7 | 222 | the armoury (Rally) |
| 8 | 315 | reachable before the Warden by fighting everything |

* XP is shared: every member gets the battle's full XP, including a downed one (no catching up).
* The victory panel shows XP bars filling, then "LEVEL UP" per member with the stat gains and any new skill.
* Avoiding the avoidable fights leaves the party at level 6 for the Warden: harder, still fair (§15, balance).

---

## 8. Equipment, items, economy

### 8.1 Equipment

Three slots per member: **weapon**, **off-hand**, **charm**.

| Piece | Who | Slot | Effect | Source |
|---|---|---|---|---|
| Traveller's sword | Rowan | weapon | STR +0 | start |
| Steel sword | Rowan | weapon | STR +2 | shop, 90 g |
| **Hollowmere blade** | Rowan | weapon | STR +3, holy damage | the forge: steel sword + Brom's ore + 60 g |
| Captain's greatsword | Rowan | weapon | STR +5, AGI -1, no shield | the Bone Captain |
| Wooden shield | Rowan | off-hand | DEF +1 | start |
| Kite shield | Rowan | off-hand | DEF +2, Guard also halves magic | the armoury's iron chest |
| Apprentice staff | Wren | weapon | INT +0 | start |
| Ashwood staff | Wren | weapon | INT +2 | shop, 80 g |
| Lantern staff | Wren | weapon | INT +3, Radiance costs 6 | the second grave-lantern |
| Spell book | Wren | off-hand | SP +6 | road chest |
| Ember charm | any | charm | immune to Burn | barrow chest |
| Swift charm | any | charm | AGI +2 | shop, 70 g |
| Keepsake | any | charm | XP +20 % | Tamsin, if she was given the potion |

Equipment changes the member's stats only; **(open, §17 O2)** whether a weapon also changes its model.

### 8.2 Consumables and key items

| Item | Effect | Price | Also found |
|---|---|---|---|
| Potion | heals 12 HP | 10 g | drops, chests |
| Hi-Potion | heals 30 HP | 30 g | crypt chests |
| Ether | restores 10 SP | 25 g | chests |
| Salve | cures Burn and Slow | 12 g | drops |
| Revive Charm | revives at 25 % HP | 60 g | 1 in the crypt |
| Fire Flask | 8 fire damage to all enemies | 20 g | road chest |
| Smoke Bomb | flee, always | 15 g | - |
| Camp Kit | a full rest at any save point without paying; or once on the road | 40 g | - |

Stack limit 9. Key items (no stack, not sold): gate token, Brom's ore, lantern flame x2, crypt key.

### 8.3 Gold

Battles pay 5-30 g, chests 20-60 g. Expected income by the crypt: ~420 g fighting everything, ~260 g avoiding
what can be avoided. Shop total for "everything useful": ~330 g. The player can afford most, not all (a choice).

### 8.4 Brom's shop

`Buy` / `Sell` (half price) / `Forge` (one recipe, §8.1) / `Rest` (5 g: the inn; saves). Stock grows once, when
Wren joins (the staff, the Swift charm, Hi-Potions). The ore errand: Brom asks in act 1, the ore is in the cart on
the road, the forge needs a return to the village (the east gate stays open; a 60-second walk back, or done at the
end before the celebration for the Keepsake-style reward of seeing it in the ending).

### 8.5 Chests

Wooden (items, gold) and iron (equipment). A opens; the lid animates; a panel names the contents. Opened chests
stay open and are saved.

---

## 9. Enemies and bosses

### 9.1 Enemy types

| Enemy | Base | HP | STR | DEF | INT | MDEF | AGI | Behaviour | XP / gold |
|---|---|---|---|---|---|---|---|---|---|
| Skeleton Warrior | exists | 12 | 4 | 1 | 0 | 1 | 4 | attacks the member with the most HP | 5 / 6 |
| Skeleton Rogue | exists | 9 | 5 | 0 | 0 | 1 | 8 | attacks the member with the least HP | 5 / 8 |
| Skeleton Mage | exists | 10 | 2 | 0 | 5 | 3 | 5 | bolt on one member; raises 1 Risen once | 7 / 8 |
| **Risen** | Skeleton Rogue, unarmed, 0.85x, pale | 6 | 3 | 0 | 0 | 0 | 5 | comes in threes and fours; weak to fire: the Cleave and Fire lesson | 2 / 2 |
| **Sentry** | Skeleton Warrior + large shield, darker | 18 | 5 | 4 | 0 | 2 | 3 | Guards itself and the ally beside it every other turn; the shield burns (fire removes its physical resistance for the battle) | 10 / 12 |
| **Grave Wisp** | particle effects only (no model) | 8 | 0 | 6 | 6 | 2 | 9 | Chill (magic + Slow); physical attacks barely hurt it; weak to frost | 9 / 10 |

Encounters are built so each teaches one thing: Risen packs (area damage), a Sentry with a Mage behind it (burn the
shield, or Bash the Mage), Wisps (use Frost, do not swing at them), mixed groups later.

### 9.2 The Bone Captain (mid-boss)

HP 60, STR 9, DEF 3, INT 0, MDEF 2, AGI 6. Weak to holy (x1.5). Two Sentries stand with him at the start.

| Skill | Effect |
|---|---|
| Greatsword | physical on one member |
| Sweep | 0.7 x physical on the whole party |
| Sunder | physical + Defence down (3 rounds) |
| Hilt Strike | weak physical + Stun on one member |
| **Rallying Cry** (at 50 % HP, once) | CHARGE-style telegraph, then both Sentries (if down) rise again with half HP |

The fight asks for what act 3 taught: burn the shields, keep Rally up against Sunder, Mend through Sweep, save SP
for Radiance.

### 9.3 The Bone Warden (boss, three phases)

Phases 1 and 2 as v1 12.3-12.4 with stats raised for a level 6-8 party of two (HP 44 -> 110; Soul Rend hits one
member, Grave Fire the whole party). **Phase 3** (below 25 % HP, a short cutscene): he stops raising the dead and
turns the ring itself: **Barrow's Weight** (the whole party Slowed for 2 rounds, once), then alternates Soul Rend
(telegraphed, as before) with **Last Rite** (magic on the whole party, stronger each time it is cast). A race the
player wins with Radiance and what is left of the items. Balance targets as v1 12.6: good play wins >= 95 %, naive
play 30-60 %.

---

## 10. Quest, choices, saving

### 10.1 State

v1's Progress grows: stage 0-15 (five acts), party (per slot: present, level, XP, HP, SP, equipment x3, down),
inventory (item id -> count, 16 kinds), gold, key-item flags, chests (bitmask per zone), defeated[] per zone (as
today), puzzle state (braziers), choice flags (gave_potion, ore_delivered, ...), play time. About 200 bytes;
`static_assert`ed and versioned like v1's.

### 10.2 Quests

| Quest | Stages | Reward |
|---|---|---|
| **The Restless Dead** (main) | speak with Wren -> the patrols -> report -> the east gate -> the Old Road -> the barrow fence -> the two lanterns -> the crypt -> the lever -> the braziers -> the Captain -> the ring -> the Warden | the chapter |
| **Brom's ore** (side) | accept -> find the cart -> return | the forge recipe |
| **The traveller** (side, implicit) | meet Tamsin -> give a potion, or not | given: the Keepsake, and she has unlocked the armoury's side door (the spike corridor can be skipped). Not given: she is gone when the party returns, and the corridor is the only way |

The **quest log** (party menu) lists active and finished quests with the current objective; the objective HUD
shows the main quest's line as today.

### 10.3 Dialog choices that matter

| Where | Choice | Remembered as |
|---|---|---|
| Wren's request | "I will." / "What's in it for me?" | her tone at the report; +10 g if he asked |
| Tamsin | give a potion / "I have none to spare" | §10.2 |
| Corwin at the gate | wait for his briefing / walk past | the briefing marks the ford on the objective line |
| The Captain's hall | Wren: "Rest first?" yes / no | uses the shrine (no cost); only a nudge |
| After the Warden | lay his staff on the stone / keep it | one line in the ending; the results card notes it |

### 10.4 Saving

* **Save points only** (inn, camp, waystone, shrine): rest and save together. Checkpoints (RAM, v1) stay for
  retries: a lost battle returns to the last checkpoint, not the last save.
* CONTINUE loads the VMU save: the party stands at the save point, rested.
* The payload is ~256 bytes plus the package header and icon: **3 blocks** (slice 3D-7 planned 2). **(open, §17 O3.)**
* The format carries a version and a third party slot from day one.

---

## 11. User interface

| Screen | Layout | Notes |
|---|---|---|
| Battle HUD | party panels bottom left (name, HP, SP, status icons) for up to 3; the turn strip top centre; enemy bars as today | panels are 150 px wide: three fit in 480 |
| Command list | a vertical list by the acting member, submenus slide right | replaces the ring; A / B / stick, Y = Guard |
| Target cursor | enemies as today; allies get the same cursor on their panel and a ring under the model | WEAK / RESIST icon once known |
| Victory panel | XP bars per member, gold, drops; "LEVEL UP" with stat lines | A advances; hold A to skip the bars |
| Party menu | four tabs; the member's portrait and stats left, the list right | 2D only; the field stays drawn, dimmed |
| Equipment | slot list, candidates with the stat change in green / red | L / R switches member |
| Shop | Brom's portrait, list with prices, gold top right, "owned" count | sell at half price |
| Chest / pickup | a one-line panel over the field | `waystone`-style chime |
| Save point | "Rest and save?" yes / no, then the VMU indicator of slice 3D-7 | |

All 2D, on the existing UI atlas (new icons: 5 statuses, 3 elements, 12 item kinds, slot icons: ~24 cells of 16x16).

---

## 12. Audio

| Kind | Additions |
|---|---|
| Music | **road** (night walk, the village tune's rhythm slowed, in a minor mode), **crypt** (sparse, low, the barrow's bell), **mid-boss** (the battle track's drive, heavier), **shop** (Brom's: the village tune on accordion, short), and a **level-up jingle** (2 s, played as an effect) |
| Effects | cleave, shield bash, rally, staff hit, mend, frost cast / impact, radiance, flee, stun, wisp chill, chest open, coin, equip, buy / sell, lever, gate, brazier, spikes, save |
| Delivery | streamed from the asset root with the same codec and stream code (the file source is already one function). The 2.0 MiB cap goes; tracks may run 60-90 s. **(open, §17 O5: stay 22 kHz mono or step up.)** |

---

## 13. Technology

### 13.1 The asset root **[decided: disc image and broadband adapter]**

At boot the game picks where its files are:

| Root | When | How |
|---|---|---|
| `/cd` | a disc image in Flycast (`.cdi`), or a burned disc | `make cdi`: scramble + IP.BIN + ISO + CDI; Flycast loads `.cdi` |
| `/pc` | a real Dreamcast with the **broadband adapter** and dcload-ip | the console receives the `.elf` over the network and reads the same asset folder from the PC |
| `/rd` | fallback, and the boot's own needs | a small romdisk: fonts, UI atlas, the boot screen, the menu sounds |

One function resolves a path (`asset_path("models/knight.dmf")`); nothing else knows the root. The log goes to
dcload's console when dcload-ip is present and to the serial port otherwise (today it is forced to serial, which a
broadband setup cannot read).

### 13.2 Zone loading

| Set | Loaded | Contents |
|---|---|---|
| Core | at boot, never unloaded | UI, fonts, party models, weapons, the animation library, the battle FX, effects in sound RAM |
| Zone | at a map change, under the black | the zone's props, its ground texture, its NPCs and enemy models, its arena, its music file |

Budget per zone (to be restated after the first measurement, §16 H2-1): models <= 1.8 MiB, textures <= 1.0 MiB of
VRAM, a load <= 1.5 s on `/cd`. Over `/pc` loads are slower; the black holds until the zone is ready.

### 13.3 Frame budgets

The limits stay: **18,000 written vertices and 1.60 M estimated instructions per frame**, 60 fps, 0 TA errors.

| Scene | Today | v2 worst case | Plan |
|---|---|---|---|
| Field, village | 15.8k | + villagers (3 x ~1.9k if all in view) | villagers use the light models (§14.2) and stand apart |
| Field, road | - | trees and rocks, 1-2 patrols | built to <= 14k from the start; the sweep tool decides the layout |
| Field, crypt | - | ~20 wall pieces + props + 1 patrol | walls decimated and cut (§14.4): <= 150 vertices each |
| Battle | 17.4k (1 hero + 3 skeletons) | **3 party + 4 enemies** eventually; 2 + 4 in this slice | **battle models at a second level of detail** (<= 1,200 vertices each) for every combatant, and arenas <= 5k. This is the slice's largest technical risk and is measured first (H2-4) |

### 13.4 Memory

With audio and zone models off the romdisk the program image falls from ~12 MiB to ~6 MiB. Targets: >= 4 MiB of
main RAM free in every zone; sound RAM as slice 3D-7 (effects <= 320 KiB grows to <= 420 KiB); VRAM <= 2.5 MiB of
textures.

---

## 14. Art: what exists, what work it needs, and in which slice

All sources are **CC0**. "Slice" is the milestone of §16 that must deliver the asset in the game. Vertex counts
are the game's (`.dmf`, strips); triangle counts are the source files'.

### 14.1 Where the sources are

| Id | Folder | Contents |
|---|---|---|
| **KK** | `resubmissions/sub2/KayKit` | 6 adventurers, 3 skeletons, 6 weapons, 5 animation sets (medium rig) |
| **QV** | `resubmissions/sub2/Village` | 19 pieces of Quaternius's free village kit, 9 base-colour textures (up to 4 MB each) |
| **KD** | `KayKit_Dungeon_Pack_1.1_FREE` | 216 pieces (FBX / OBJ / glTF), 90,240 triangles in all, **one 1024x1024 texture** |
| **GEN** | `tools/` (generated) | trees, bushes, boulders, fences, tombs, the crypt mound, FX textures, grounds |
| **IN** | `romdisk/` (already in the game) | converted and budgeted in slice 1 |

Pipelines: characters - `tools/vs1art/vs1_fbx2dmf.py` (Python 3.12, assimp / ufbx / meshoptimizer); animations -
`tools/battle3d/build_anims.py` (**needs Blender's Python**); dungeon pieces - a new OBJ converter (no Blender);
village pieces - a new assembler on the FBX reader the character tool already uses.

### 14.2 Characters

| Asset | Source | State today | Work needed | Target | Slice |
|---|---|---|---|---|---|
| Rowan (Knight) | KK | IN, 2,280 v | **battle LOD**: a second model, simplified per part | <= 1,200 v | H2-4 |
| Wren (Mage) | KK | IN, 2,047 v (field NPC) | battle LOD; a hand slot check for the staff; battle clips mapped (§14.3) | <= 1,200 v | H2-4 |
| Brom (Barbarian) | KK | IN, 1,903 v | none | - | - |
| Corwin (Ranger) | KK | IN, 1,929 v | none | - | - |
| **Tamsin (Rogue, hooded)** | KK `Rogue_Hooded.fbx` | **not converted** | convert (field quality), 3 portraits (neutral, happy, worried), a lying / sitting pose | <= 2,000 v | H2-10 |
| **Villager (Rogue)** | KK `Rogue.fbx` | **not converted** | convert as a **light** model; 1 portrait | <= 1,000 v | H2-9 |
| Villagers 2-4 | KK Barbarian / Ranger / Mage | IN (as Brom, Corwin, Wren) | light models from the same sources + **recoloured textures** (64x64 each) | <= 1,000 v | H2-9 |
| Skeleton Warrior | KK | IN, 2,007 v | battle LOD | <= 1,100 v | H2-4 |
| Skeleton Rogue | KK | IN, 1,580 v | battle LOD | <= 1,000 v | H2-4 |
| Skeleton Mage | KK | IN, 1,671 v | battle LOD | <= 1,000 v | H2-4 |
| **Risen** | KK Skeleton Rogue | - | the Rogue's battle LOD at 0.85x, **pale texture variant**, no weapon | <= 800 v | H2-11 |
| **Sentry** | KK Skeleton Warrior + `Skeleton_Shield_Large_A` | shield IN (`w_skel_shield`) | the Warrior's LOD + shield, **dark texture variant**; a "shield burnt" state (shield hidden) | <= 1,300 v with the shield | H2-11 |
| **Grave Wisp** | GEN | FX atlas IN | no model: 3-4 additive billboards + a glow, a new FX flipbook cell row | ~40 v | H2-11 |
| **Bone Captain** | KK Skeleton Warrior + `sword_2handed_color` | sword **not converted** | Warrior field model at 1.3x, **armour texture variant**, the two-handed sword converted; portrait x3 | <= 2,300 v field, <= 1,300 v battle | H2-12 |
| The Warden | KK Skeleton Mage | IN | phase-3 look (texture tint, the existing glow); battle LOD not needed (one model, few minions) | - | H2-13 |

Texture variants are palette swaps of the 64x64 character textures (each KayKit character uses a small colour
atlas): generated by a tool, 8 KiB each.

### 14.3 Weapons and animation clips

| Asset | Source | State | Work needed | Slice |
|---|---|---|---|---|
| Sword, shield (Rowan) | KK | IN (`w_sword`, `w_shield`) | tinted variants for the steel sword and the Hollowmere blade (vertex colour, no new model) | H2-7 |
| `sword_2handed_color` | KK | not converted | convert (~150 v): the Captain's sword, and Rowan's drop | H2-12 |
| `shield_badge_color` | KK | not converted | convert (~200 v): the kite shield | H2-7 |
| Skeleton blade / shield / staff | KK | IN | none; the staff recoloured as **Wren's staffs** (3 tints). No mage staff is in the folder: **gap, covered by the recolour** | H2-4 |
| Clips in the library | KK | IN, 29 clips (265 KiB) | none | - |
| **New clips** | KK `Rig_Medium_CombatMelee`, `_General`, `_Special` | in the FBX sets, not in the library | add ~8: block-and-strike (Shield Bash), cheer (Rally, victory), one-handed chop for the staff, sit / lie idle (Tamsin, the camp), pick-up (chests, levers), two-handed sweep (the Captain), a kneel (the ending) | H2-5 (battle), H2-10 (field) |
| Spell-cast clip | **not on disk** (KayKit's ranged / magic set is not in the folder) | - | keep `throw` as the cast (as today) and `use_item` for Mend; **optional download** of the free set later | - |

Each clip adds ~9 KiB to the library: +8 clips = ~340 KiB total. **Adding clips needs Blender on the machine that
builds them** (§17 R4).

### 14.4 The dungeon pack (KD) - the crypt, chests, shop dressing

The pack is drawn for free cameras and modern hardware: a plain wall is 494 triangles, and a room shows twenty of
them. Three rules bring it into budget:

1. **Floors are painted, not drawn.** The floor tiles are rendered from above into each room's ground texture (the
   village's method): 0 vertices for all floor pieces.
2. **Walls are cut for the camera and decimated.** Faces the fixed camera cannot see (backs, undersides, the tops of
   south walls) are removed and the bevels simplified, as slice 3D-2 did for the village.
3. **One small texture.** The 1024x1024 atlas is a colour palette; it is resampled to **128x128 RGB565 (32 KiB,
   one slot)**. If slots run short the colours move into the vertices and the slot is freed.

| Group | Pieces used (of 216) | Source triangles each | Work | Target each | Slice |
|---|---|---|---|---|---|
| Floors | `floor_tile_large`, `_small`, `_small_broken_A/B`, `_small_decorated`, `floor_tile_large_rocks`, `floor_dirt_large` | 66-390 | **bake into the ground texture**; not converted as models | 0 v | H2-12 |
| Hazards on the floor | `floor_tile_big_spikes`, `floor_tile_grate` | 530, 142 | spikes: decimate, two states (up / down); grate: baked | spikes <= 120 v | H2-12 |
| Walls | `wall`, `wall_corner`, `wall_doorway`, `wall_gated`, `wall_half`, `wall_endcap`, `wall_pillar`, `wall_Tsplit`, `wall_broken`, `wall_arched` | 222-784 | cut for the camera + decimate (bevels, back faces) | **<= 150 v** (<= 250 v for doorway, gated, arched) | H2-12 |
| Too heavy as they are | `wall_cracked` (2,010), `wall_shelves` (1,582) | - | **not used**; cracks painted on `wall` in the texture pass if wanted | - | - |
| Structure | `pillar` (136), `column` (44), `stairs` (358), `barrier` (90) | as listed | convert; `stairs` cut for the camera | pillar <= 110 v, stairs <= 200 v | H2-12 |
| Light | `torch_mounted` (278), `torch_lit` (270), `candle_triple` (198) | as listed | decimate; the flame is an FX billboard + a light preset flicker | <= 90 v | H2-12 |
| Puzzle | braziers: **no brazier piece exists** - built from `pillar` (short) + `torch_lit`'s bowl; lever: **none** - GEN (a box and a handle) | - | assemble / generate; lit and unlit states | <= 150 v | H2-12 |
| Chests | `chest` (266), `chest_gold` (1,106) | as listed | `chest`: convert, lid as a second part (open / closed); `chest_gold`: decimate hard for the iron chest | <= 180 v, <= 260 v | **H2-8** (first chest is in the village) |
| Pickups | `key` (160), `coin_stack_small` (416) | as listed | decimate; shown in the pickup panel and on the Captain's belt | <= 80 v | H2-8, H2-12 |
| Decor, crypt | `rubble_half` (390), `rubble_large` (788), `banner_patternA_red`, `banner_shield_blue` (639), `sword_shield` (542), `barrel_large` (561) | as listed | decimate to about a third; banners cut to one side | <= 150 v each | H2-12 |
| Decor, shop | `barrel_large`, `box_large` (188), `crates_stacked` (1,331), `keg` (807), `table_medium` (256), `stool` (172) | as listed | decimate (`crates_stacked`, `keg` to a quarter); placed outside Brom's house | <= 200 v each | H2-8 |
| Camp (road) | `bed_floor`, `barrel_small`, `torch_lit` as the fire's base | small | convert; the fire is FX | <= 100 v each | H2-10 |
| Unused | ~160 pieces (tables with cloth, plates, bottles, beds, wooden stairs, most banners, window walls, scaffolds) | - | none; available for later chapters | - | - |

Estimated crypt set: ~28 converted pieces, ~95 KiB of models, 1 texture slot, 4 ground textures sharing one
512x512 atlas. A crypt room in view: ~20 walls x 150 + ~12 props x 130 + a patrol = about 8-9k vertices.

### 14.5 The village kit (QV) and generated scenery

| Asset | Source | State | Work needed | Slice |
|---|---|---|---|---|
| The five houses, wagon, crate, fence, arch, border, vines | QV | IN, **cut for the fixed camera** (no back walls) | none for the field | - |
| Whole houses for the title | QV, the uncut pieces | not built (the old assembler is no longer in the repo) | a new assembler from the 19 pieces; decimate as `docs/village_qc/report.md` (roof 1,996 -> 1,376, door 470 -> 382...); two whole house prefabs, used by the title only | H2-2 (**could**: the title's sweep works without it) |
| Village textures | QV, 2-4 MB PNGs | IN at 64-128 px | none | - |
| Brom's sign, shop awning | GEN | - | generate (a board, two posts, a cloth quad) | H2-8 |
| Trees, bushes, dead trees | GEN | IN (`tree`, `bush`, `dead_tree`) | 2 more tree variants and a fallen trunk for the road | H2-10 |
| Boulders | QV-derived | IN (4 variants, 462-1,148 v) | none; reused on the road | - |
| Stream, bridge | GEN + KD (`floor_wood_large` planks) | - | the stream is painted in the ground with an animated UV strip (could); the broken bridge: generated planks and posts, two halves | H2-10 |
| Overturned cart | QV wagon | IN (`wagon_r0`, 1,544 v) | a rotated instance + a spilled-ore prop (GEN, ~60 v) | H2-10 |
| Road signpost, camp tent | GEN | - | generate | H2-10 |
| Grave-lanterns | GEN + KD `candle_lit` | - | a stone post (GEN) with a lantern cage; lit / unlit | H2-11 |
| Crypt door in the mound | GEN `crypt` | IN | a door variant: sealed (dark) / open | H2-11 |
| Tombstones with text | GEN | IN (`tomb_round`, `tomb_cross`) | three "readable" stones: same models, an interact marker | H2-12 |
| Grounds | GEN (`tools/ground`) | village, barrow IN | paint the road (48x24) and the four crypt rooms; extend the barrow north | H2-10, H2-11, H2-12 |
| Arenas | GEN | village, barrow IN | **road arena** (trees, the stream), **crypt arena** (walls, pillars, torches), both <= 5k vertices | H2-10, H2-12 |

### 14.6 2D art

| Asset | Work | Slice |
|---|---|---|
| Portraits | Tamsin x3, the Captain x3, 1 villager; Wren and Rowan gain "hurt" for the battle panels (could) | H2-9 to H2-12 |
| UI atlas | status (5), element (3), item (12), slot (3) icons; the turn strip's small faces (from the portraits) | H2-3, H2-7 |
| FX atlas | frost shards, holy flash, wisp body, gold ring (Rally), coin sparkle | H2-5, H2-11 |
| VMU icon | as slice 3D-7 | H2-0 |
| Disc boot | IP.BIN text and the boot logo (`makeip`) | H2-1 |

### 14.7 Gaps: nothing on disk

| Need | Covered by | Buy? |
|---|---|---|
| A mage's staff | the skeleton staff recoloured | no |
| A cast animation | `throw` (as today) | no; the free KayKit ranged / magic set would improve it |
| Brazier, lever, lantern, signpost, tent, bridge | generated | no |
| Forest pieces beyond trees and rocks | generated | no; a forest kit would make the road richer |
| Living monsters | not planned | only if the design changes |

---

## 15. Verification

v1's table stays; v2 adds:

| Test | Proves | How |
|---|---|---|
| Full chapter | title -> results with no soft lock, in the expected stages, with saves at all four points | the route bot, per act and whole; float == double |
| **Timed run** | a first play is 35-40 minutes | the bot plays at a human pace model (reading speed, walking, menu time); reports minutes per act |
| Party rules | turn order, ally targeting, down / revive, loss when all are down | turn-level logs |
| Skills | each skill's damage, cost, status and element against expected values | one scripted battle per skill |
| Progression | XP sums, level thresholds, stat tables, skills learned at their levels | table-driven |
| Economy | income vs prices along three routes (fight all, avoid all, shop-heavy): never stuck without a Potion's price | the bot with three policies |
| Items, equipment | every item's effect; equip changes stats exactly; stack limits; sell prices | table-driven |
| Choices | each choice's flag and its consequence | route variants |
| Puzzle | right order opens; wrong orders reset and raise the Risen; the hint appears | route variants |
| Save | every save point: CONTINUE == the saved state byte for byte; v1's fault cases; a v1 save is rejected cleanly | slice 3D-7's harness |
| Bosses | the Captain and the Warden meet the balance targets at levels 5-8 | bot policies x 50 seeds |
| Budgets | every zone and both arenas with the largest groups: <= 18,000 / 1.60 M | sweeps |
| Asset root | the same run from `/rd`, `/cd` and a `/pc` stand-in gives identical traces | host |
| Hardware | each zone's frame times, load times over the broadband adapter, music with no underrun | DIAG, checklist |

---

## 16. Milestones (vertical slices)

Each ends playable from the title, with its tests green. Sizes are relative (S: a sitting, M: two, L: three or
more). The art column lists what §14 assigns to the slice.

| # | Slice | Size | Playable at the end | Art delivered | Depends on |
|---|---|---|---|---|---|
| **H2-0** | Close slice 3D-7: save, continue, options, pause, splash, footsteps, ducking | M | today's game, complete | VMU icon | - |
| **H2-1** | Asset root and zone loading: `make cdi`, `/cd`, `/pc`, the log over dcload, music streamed from the root | L | today's game from a `.cdi` in Flycast and over the broadband adapter | boot logo | H2-0 |
| **H2-2** | Art pipeline: the OBJ converter with camera cut and decimation, the palette texture, QC sheets; (could) whole village houses | M | a test room built from the pack | converter proven on walls, a chest, a pillar | H2-1 |
| **H2-3** | Party battle, rules and UI: three slots, turn strip, ally targeting, the command list, down / revive | L | Rowan and Wren in a test battle, plain presentation | UI icons | - |
| **H2-4** | Party battle, presentation: two heroes in the arena, shots, sequences, **battle LOD models**, the vertex budget | L | the same battle, finished | LODs of Rowan, Wren, 3 skeletons; Wren's staff | H2-3 |
| **H2-5** | Skills, SP, elements, statuses, with sounds | L | every skill in a test battle | ~5 battle clips, FX cells | H2-4 |
| **H2-6** | Levels and the victory panel | M | today's game with XP and level-ups | - | H2-5 |
| **H2-7** | Party menu, inventory, equipment, items in battle; save format v2 | L | equip and use items in today's game | item / slot icons, kite shield, sword tints | H2-6, H2-0 |
| **H2-8** | Gold, chests, Brom's shop, forge, inn | M | buy, sell, forge, rest | chests, coin, key, shop dressing, sign | H2-7, H2-2 |
| **H2-9** | Act 1: the village reworked, villagers, the ore errand, Wren joins | M | act 1 as designed | Rogue villager + 3 recolours, portraits | H2-4, H2-8 |
| **H2-10** | Act 2: the Old Road | L | acts 1-2 | Tamsin + portraits, road scenery, bridge, cart, camp, road ground and arena, field clips, road music | H2-9 |
| **H2-11** | Act 3: the barrow grounds, the lanterns, the three new enemies | M | acts 1-3 | Risen, Sentry, Wisp, lanterns, crypt door, barrow ground | H2-10 |
| **H2-12** | Act 4: the crypt and the Bone Captain | L | acts 1-4 | the crypt set (~28 pieces), 4 grounds, crypt arena, the Captain, two-handed sword, crypt and mid-boss music | H2-2, H2-11 |
| **H2-13** | Act 5: the Warden's third phase, the ending, the choices carried through, the quest log complete | M | the whole chapter | Warden phase-3 look | H2-12 |
| **H2-14** | Balance and pacing: the timed run, the curve, the economy | M | 35-40 minutes, measured | - | H2-13 |
| **H2-15** | Sign-off: hardware over the broadband adapter, the verification table, docs, licences | M | done | - | H2-14 |

H2-3 can run beside H2-1 and H2-2 (rules and UI touch neither loading nor art).

### Definition of done

* A new player goes from the title to the results card in 35-40 minutes without help, at 60 fps on a Dreamcast.
* Every row of §1.1 exists at production quality.
* Every test of §15 passes; both bosses meet their balance targets.
* The budgets of §13 hold in every zone; 0 TA errors; no per-frame heap allocation.
* Every asset of §14 is in the game from a reproducible tool, with its licence recorded.
* Docs: HOWTO (a zone, an enemy, a skill, an item, a chest, a shop entry, a quest), TRIAGE, the hardware checklist.

---

## 17. Open decisions and risks

### Open decisions

| # | Decision | Recommendation |
|---|---|---|
| O1 | Poison as a second damage-over-time status | no: Burn covers it; add Poison with living monsters |
| O2 | Do weapons change their model when equipped? | tints for swords and staffs; the kite shield and the Captain's greatsword are real models |
| O3 | Save size: 3 VMU blocks | yes |
| O4 | X for the party menu | yes |
| O5 | Music quality once streamed | stay 22 kHz mono, spend the freedom on length; revisit after H2-1's measurements |
| O6 | Party members following the leader on the field | no for this slice (each follower is ~2,000 vertices in every field frame); revisit with the light models |
| O7 | Tamsin's name and her role as a rogue | working name; scripts are data |
| O8 | Whole village houses and the title's full orbit | could, inside H2-2, only if the assembler is cheap |

### Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Battle frame time** with five to seven characters | battle LODs and light arenas, measured in H2-4 before any encounter is designed; fall back to a cap of 3 enemies |
| R2 | The party changes assumptions throughout the battle code (one hero) | rules and a plain UI first (H2-3), presentation second (H2-4); act 1 keeps the solo path |
| R3 | Loading over the network is slow or uneven | measure `/pc` early in H2-1; hold the black; load the zone's music into RAM as a fallback |
| R4 | New animation clips need Blender | do all clip additions in two batches (H2-5, H2-10); confirm a Blender install (or its Python module) before H2-5 |
| R5 | The dungeon pack's density | the three rules of §14.4; the converter and its QC sheets come first (H2-2) |
| R6 | Forty minutes of content means slow test passes | per-act routes; the bot's timed model; saves at four points for manual checks |
| R7 | Scope | sixteen slices, each playable; H2-13 is a complete game even if H2-14's tuning runs long |
