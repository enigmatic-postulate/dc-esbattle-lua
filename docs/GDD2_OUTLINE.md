# Horizontal slice 2: working outline ("The Restless Dead", expanded)

> **Superseded 2026-11-21 by `docs/GDD_Horizontal_Slice_2.md` (GDD v2.0), which is the document to work from.**
> Kept as the record of the brainstorm.
>
> **Status: brainstorm outline, not the GDD.** `docs/GDD_Horizontal_Slice.pdf` is untouched. This collects the ideas
> the project owner picked on 2026-11-21 into one shape to react to. Names, numbers and counts are proposals; anything
> marked **(open)** needs a decision. Once this is agreed it becomes the brief for GDD v2 and for the slice plans.

**Goal:** grow the 5-10 minute slice into a **35-40 minute** classic RPG chapter: a party of two, levels and
equipment, an inventory, a shop, more battle actions, two new zones, a mid-boss, and a story told in gated acts.
About 2-3x the content, on the same engine, shipped as a **disc image**.

## 1. Decisions taken

| # | Decision | Notes |
|---|---|---|
| 1 | **A party of two**: Rowan (fighter) and Wren (mage) | Wren has a model, portraits and a mage identity already |
| 2 | **Levels and equipment, both** | a short curve (levels 1-8), three equipment slots |
| 3 | **Two new zones**: the Old Road, and the Barrow's interior (the crypt) | plus more to do in the village and on the barrow grounds |
| 4 | **Disc build** (`.cdi`) instead of a bare `.elf` | Flycast loads `.cdi` / `.gdi` / `.chd` / `.cue`; removes the RAM and music ceilings |
| 5 | **Art from what is on disk, plus generated pieces**; no purchase planned | inventory in §6 |
| 6 | Scope is **at least** 8 hours of agent work, with no upper limit set | the slice list in §8 is well past that |
| 7 | The road's **traveller is the future third party member** (the unused Rogue model) | she does not join in this slice; the party systems are built for three so she can later |
| 8 | **A separate pause menu** on Start, as slice 3D-7 planned; the party menu is its own screen on another button | proposal: **X** opens the party menu in the field (the pad's X is unused today) |
| 9 | Real hardware runs over a **broadband adapter** (dcload-ip), not from a burned disc | see §5: assets can come from the PC over the network, so the console needs no CD-R for development |

## 2. The chapter, act by act

| Act | Zone | Minutes | What happens | Gate to the next act |
|---|---|---|---|---|
| 1 | **Hollowmere** at dusk | 7-8 | The intro; Wren's lesson (Fire); Brom's forge becomes a shop; Corwin and the gate token; first patrols; a side errand for Brom | Rowan wins the road patrol; **Wren joins**; Corwin opens the east gate |
| 2 | **The Old Road** (new) | 6-8 | A wooded road at nightfall: scouts and wisps, a broken bridge and the way around it, chests off the path, a wounded traveller, a camp to rest and save | Reach the barrow's outer fence |
| 3 | **Barrow grounds** at night | 5-6 | Today's barrow map with more patrols; the waystone; the crypt door is sealed | Light the two grave-lanterns (each guarded) to open the crypt |
| 4 | **The crypt** (new) | 10-12 | Three or four rooms under the barrow: a gate-and-lever room, an ossuary with a brazier puzzle read from the gravestones, an armoury with the best equipment, a shrine to save at, and the **Bone Captain** (mid-boss) holding the Warden's key | The key opens the ring chamber |
| 5 | **The Warden** | 5-6 | The boss in three phases (today's two, plus one that tests the new skills); the dawn; credits; results | - |

Total 33-40 minutes for a first play. Roughly 12-15 battles (today: 3-4), two bosses, one puzzle, one side quest.

## 3. Systems

### 3.1 Party
* Two members; the battle rules already handle teams, the UI and the 3D presentation assume one hero.
* Wren joins at the end of act 1 (scripted). Before that Rowan fights alone, as today: the tutorial stays simple.
* A fallen member can be revived by an item or at a save point; both down = the death screen and the checkpoint.
* **Built for three.** The traveller met on the Old Road (the Rogue model) is the future third member: she is an
  NPC in this slice, but turn order, targeting, the battle UI, the menus and the save format all allow a third slot
  from the start, so adding her later is content, not a rewrite.

### 3.2 Battle
| Addition | Proposal |
|---|---|
| Skill points (SP) | every skill but Attack and Guard costs SP; SP returns at save points, inns and with an item |
| Rowan's skills | Attack, Guard, **Cleave** (all enemies, lvl 3), **Shield Bash** (damage + stun, lvl 5), **Rally** (party defence up, lvl 7) |
| Wren's skills | Staff strike, **Fire** (today's Fireball), **Mend** (heal one, from the start), **Frost** (damage + slow, lvl 4), **Radiance** (holy, all undead, lvl 6) |
| Elements | fire, frost, holy; each enemy has a weakness and a resistance, shown after the first hit |
| Status effects | burn (exists), **stun** (skip a turn), **slow**, **defence up / down**, **poison** (**open**: is one damage-over-time enough?) |
| Items in battle | an Items command with a submenu; using one takes the turn |
| Flee | allowed in ordinary battles, never against a boss or a scripted fight |
| Command menu | Attack / Skills (submenu) / Guard / Items / Flee, replacing today's flat four |

### 3.3 Progression
* **Levels 1-8.** XP from every battle, shared by the party; a level-up panel on the victory screen.
* Stats: HP, SP, Attack, Defence, Magic, Speed. Speed sets turn order.
* **Equipment, three slots** per member: weapon, off-hand (shield or focus), charm.
  About 10 pieces in all: 3 weapons and 2 off-hands per character tier (start, shop, crypt) and 3-4 charms with one
  effect each (burn immunity, +SP, first strike, XP bonus).
* Upgrades come from three places so each matters: Brom's shop (gold), chests (exploring), the Bone Captain (story).

### 3.4 Inventory, gold, shop
* **Start** stays the pause menu of slice 3D-7 (Resume / Options / Return to title). The **party menu** is a
  separate screen on **X** in the field: **Party / Items / Equipment / Quests**.
* Consumables (about 8): Potion, Hi-Potion, Ether (SP), Salve (cures burn and poison), Revive Charm, Fire Flask
  (damage item), Smoke Bomb (guaranteed flee), Camp Kit (full rest at a camp spot).
* Key items: gate token (exists), crypt key, the two lantern flames, Brom's ore.
* **Gold** from battles and chests. **Brom's shop**: buy, sell, and one forge upgrade paid with gold plus the ore
  from the side errand. Brom's rest becomes a paid inn stay (cheap).
* Chests: wooden (items, gold) and iron (equipment); opened state is saved.

### 3.5 Story structure
* The main quest in the five acts above, each gate a thing the player does, tracked in a **quest log**.
* Side content: **Brom's ore** (act 1 -> act 2 -> back, or delivered at the end for a different reward), the
  **wounded traveller** on the road (a dialog choice: give a potion, or not; she returns the favour in the crypt).
* **Dialog choices** in three or four places, each with a small visible consequence.
* More villagers (3-4, with barks) so Hollowmere reads as a place people live in.

### 3.6 Saving
* The VMU save from slice 3D-7, extended: party, levels, equipment, inventory, gold, chests, quest flags, play time.
* Save points: Brom's inn, the road camp, the waystone, the crypt shrine. Checkpoints stay for retries.
* The save grows past 2 VMU blocks; target 4-6 **(open)**.

## 4. Content lists

| Kind | Today | Proposed |
|---|---|---|
| Zones | village, barrow | + Old Road, + crypt (3-4 rooms), + Brom's shop as a screen |
| Party | Rowan | + Wren |
| Enemies | skeleton warrior, rogue, mage | + **Sentry** (armoured, guards its neighbours), + **Grave Wisp** (a caster made of light; resists fire, weak to frost), + **Risen** (weak, comes in numbers, raised by mages) |
| Bosses | the Warden (2 phases) | + **Bone Captain** (mid-boss), Warden gets a 3rd phase |
| Skills | 4 commands | 10 across two characters |
| Items | potion | ~8 consumables, ~10 equipment, 4-5 key items |
| NPCs | Wren, Brom, Corwin | + 3-4 villagers, + the traveller |
| Music | 8 tracks (187 s) | + road, + crypt, + mid-boss, + shop/inn; longer loops once music streams from the disc |
| Battles in a run | 3-4 | 12-15 |

New enemies stay undead on purpose: the three skeleton bodies, recoloured and re-equipped, share one rig and one
animation library. A living monster (wolf, bat) needs another pack and another rig: **(open)**, not planned.

## 5. Technical consequences

| Topic | Today | What changes |
|---|---|---|
| **Assets off the romdisk** | a `.elf` with a 5.3 MB romdisk held in RAM | one **asset root** chosen at boot: `/cd` (a `.cdi` in Flycast, or any disc), `/pc` (the developer's PC through dcload-ip and the broadband adapter: the console loads the `.elf` and then reads maps, models and music over the network), `/rd` as the fallback. Maps and models load per zone; a small romdisk keeps only what the boot needs. KOS ships `scramble` and `makeip`; the ISO-to-CDI step (cdi4dc or mkdcdisc) is to be confirmed in DreamSDK |
| **Log on hardware** | forced to the serial port (`dbgio_dev_select("scif")`) | with a broadband adapter there is no serial cable: the log must go to dcload's console when dcload-ip is present, and to serial otherwise (Flycast) |
| **Main RAM** | 3.25 MiB free with everything loaded | the audio and most models leave RAM; zones load and unload. Budget to be restated per zone |
| **Music** | 2.0 MiB cap, 22 kHz mono ADPCM from RAM | streamed from disc files with the same codec and stream code (the file source was kept behind one function for this); the cap goes; stereo or 44 kHz become possible |
| **Battle vertices** | one hero + up to 3 enemies inside 18,000 | two heroes + up to 3-4 enemies: needs lighter battle models (a second level of detail) or a cap of 3 enemies. Measured before the encounters are designed |
| **Camera** | fixed, looking north; the art is cut for it | the uncut village pieces (§6) allow whole houses: the title can orbit again, and interiors can use other angles |
| **Texture slots** | 29 of 32 | per-zone loading frees slots; the dungeon pack uses a single shared texture |
| **Save** | 2 blocks planned | larger payload, versioned from the start |
| **Tests** | host suites per slice | the story-route bot gains party, shop and menu steps; a timed full-run check for the 35-40 minute target |

## 6. Art on disk (all CC0)

| Folder | Contents | Use |
|---|---|---|
| `resubmissions/sub2/KayKit` | Knight, Mage, Barbarian, Ranger, Rogue, Rogue (hooded); Skeleton Warrior / Rogue / Mage; 2 swords, shield, skeleton blade / shield / staff; medium-rig animations: general, movement (basic, advanced), melee, special | the party and every enemy. **No ranged or casting animation set** is in the folder: an archer enemy is out unless one is added |
| `resubmissions/sub2/Village` | 19 pieces of Quaternius's free village kit (walls, roof, door, windows, fence, wagon, crate, vines) with full textures | the **uncut** sources of today's houses: rebuild them whole |
| `KayKit_Dungeon_Pack_1.1_FREE` | ~211 pieces in FBX / OBJ / glTF with **one shared texture**: walls (straight, corner, doorway, gated, arched, broken), floors (tile, dirt, wood, grates, spikes), stairs, pillars, columns, torches, candles, chests, keys, coins, barrels, crates, shelves, tables, beds, banners, rubble | the crypt, the shop and inn interior, chests and pickups everywhere. OBJ can be converted without Blender |
| generated | trees, boulders, fences, tombs (already in the game) | the Old Road's scenery; more variants as needed |

Gaps: no forest kit (generated), no non-skeleton monsters (not planned), no bow or casting animations (not needed
by the proposal above), no weapon variety beyond two swords and a shield (upgrades recoloured, **open**).

## 7. Risks

| Risk | Why | Mitigation |
|---|---|---|
| The party changes the battle's core assumptions | UI, targeting, camera shots and sequences were built around one hero | do the rules and a plain UI first, presentation second; keep the solo path working for act 1 |
| Loading over the network on real hardware | `/pc` through dcload-ip is slower and less even than a disc; streamed music may starve | Flycast with a `.cdi` first; measure `/pc` read speed early; music buffers sized for it, with a fallback of loading a track into RAM per zone |
| Frame time with five characters | the costliest frame today is already a four-character battle | measure with stand-in parties before designing encounters |
| 40 minutes of content needs 40 minutes of testing per pass | slow iteration | the route bot plays the whole chapter on the host; per-act routes |
| Scope | this is closer to a second game than a feature | each slice below ends playable; the order puts the riskiest work first |

## 8. Vertical slices (proposal)

Each ends with something playable from the title, host tests green, and a hardware/Flycast checklist. Sizes are
relative (S about a sitting, M two, L three or more).

| # | Slice | Size | Playable at the end | Depends on |
|---|---|---|---|---|
| H2-0 | **Close slice 3D-7**: save / continue, options, pause, splash, footsteps, ducking | M | today's game, complete | - |
| H2-1 | **Disc build**: `make cdi`, assets and music from `/cd`, per-zone loading, RAM budget restated | L | today's game from a `.cdi` in Flycast | H2-0 |
| H2-2 | **Art pipeline**: dungeon pack OBJ -> DMF, whole village houses, the orbit back on the title | M | the village with complete houses; a test room built from the pack | H2-1 |
| H2-3 | **Party battle, rules and UI**: two allies, speed-based turns, ally targeting, the new command menu | L | a test battle with Rowan and Wren, plain presentation | - |
| H2-4 | **Party battle, presentation**: two heroes in the arena, camera shots, sequences, lighter battle models, vertex budget | L | the same battle, looking finished | H2-3 |
| H2-5 | **Skills, SP, elements, statuses**: the ten skills with their effects and sounds | L | every skill usable in a test battle | H2-3 |
| H2-6 | **Levels and the victory screen**: XP, stats, level-ups, learned skills | M | today's game with levels | H2-5 |
| H2-7 | **Menu, inventory, equipment, items in battle**; save format v2 | L | equip and use items in today's game | H2-6, H2-0 |
| H2-8 | **Gold, chests, Brom's shop and inn** | M | buy, sell, forge, rest | H2-7, H2-2 |
| H2-9 | **Act 1 rework**: the village with the shop, villagers, the ore errand, Wren joining | M | act 1 as designed | H2-4, H2-8 |
| H2-10 | **The Old Road**: map, encounters, the traveller, chests, camp, its music | L | acts 1-2 | H2-9 |
| H2-11 | **Barrow grounds rework**: lanterns, more patrols, new enemies (Sentry, Wisp, Risen) | M | acts 1-3 | H2-10 |
| H2-12 | **The crypt**: rooms, the puzzle, the shrine, the Bone Captain | L | acts 1-4 | H2-2, H2-11 |
| H2-13 | **The Warden, phase 3, and the ending**; quest log, dialog choices carried through | M | the whole chapter | H2-12 |
| H2-14 | **Balance and pacing**: the timed bot run, the difficulty curve, economy tuning | M | 35-40 minutes, measured | H2-13 |
| H2-15 | **Sign-off**: hardware pass, the GDD's verification table, docs, licences | M | done | H2-14 |

H2-3 can start while H2-1 and H2-2 are in progress: it touches the rules and the UI, not loading or art.

## 9. Open questions

1. Poison as well as burn, or one damage-over-time effect only?
2. Does each equipment upgrade need its own look, or are recolours enough?
3. Save size: is 4-6 VMU blocks acceptable?
4. Is X the right button for the party menu?
5. The traveller's name and role (the outline assumes a rogue: fast, single-target, items and traps).
6. Music from disc: keep 22 kHz mono ADPCM and simply lengthen the tracks, or step up to stereo / 44 kHz?

Answered 2026-11-21: the traveller is the future third member; the pause menu stays separate; hardware runs over a
broadband adapter.
