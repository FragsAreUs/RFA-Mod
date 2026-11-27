// What Red Faction: Guerrilla's player gear (its weapons, its upgrades, and later its backpacks) needs
// from Armageddon's engine that it doesn't do itself. Its upgrades, bought in an F7 window, are in
// upgrades.cpp. The weapons are made from the player's own copy
// of Guerrilla by GuerrillaBuilder.exe (never shipped): see RfaFormats\Guerrilla\GuerrillaWeapons.cs;
// the guerrilla_world mod hands the game their tables at start-up.
//
// Room for more inventory items: every weapon a player can carry has an inventory item of its own
// (items_inventory.xtbl; a weapon is given by its inventory item, and the game finds the weapon whose
// Inventory_Item it is). The game keeps them in one table of 85 (MAX_INV_ITEM_TYPES; 0x30 bytes an
// item, from 0x19F8578, how many at 0x19F8570), filled while it starts (the loader 0x7A7710: "Too many
// inventory item types. Get a programmer to increase MAX_INV_ITEM_TYPES." at 85). Armageddon's own are
// 75 and its Path to War add-on's 4 more while one of its levels is loaded; Guerrilla's campaign weapons
// are about 25. The table is a global the game's code reaches by its address, written into five
// instructions: the loader (its next entry, `add edx,19F8578h` at 0x7A782A), the lookup by name
// (0x7A7DA0: `mov dword ptr [ebp-8],19F8578h` at 0x7A7DB7), the lookup by name hash (0x7A82F0: the
// hash at +4, `add ecx,19F857Ch` at 0x7A8317, and the entry, `add eax,19F8578h` at 0x7A832D), and the
// start-up constructor (0xF2C330: `push 19F8578h` at 0xF2C33C, which builds each entry with 0x963420
// through the vector constructor 0x422890). Its size is in two: the loader's check (`cmp dword ptr
// [19F8570h],55h` at 0x7A77F0) and the constructor's `push 55h` at 0xF2C338. Both sizes are one-byte
// values in the instructions (signed: at most 127), so the table can grow to 127 there. Nothing else
// reaches the table (a scan of every 4-byte value in the exe found these five; the other matches are
// parts of call and jump instructions), and each item's number in it is a byte (+0x24).
//
// The mod moves the table to one of 127 of its own, all or nothing: each of those values is checked
// against the game's first. The game's start-up constructor runs after the mods load and builds the
// new table's entries; the mod builds them too in case it has already run.
//
// Room for the animations' triggers. While the game reads its animation sets (0x979C20, at start-up) it
// copies each anim_files.xtbl entry's lists into five static pools, a record each: effects (0x97B940,
// 16 bytes, 64 at 0x261C4D0), voice lines (0x97B820, 8, 320 at 0x261C8D0), sounds (0x97B640, 16, 960 at
// 0x261D2D0), props (0x97ADA0, 36, 256 at 0x2620ED0) and hand IKs (0x97B400, 8, 1,300 at 0x26232D0),
// each with a count of records in use (0x2632190, 0x263218C, 0x2632188, 0x2632184, 0x2632180); each
// animation keeps a pointer to its first record. No room is checked: Armageddon's own fill them nearly
// full, and with Guerrilla's people, vehicles and player animations four ran over, spoiling what
// follows (a cutscene's prop trigger with a sound's numbers in it stopped the campaign). Each pool's
// address is in exactly two instructions, both in its reader (everything after reads the animations'
// own pointers), so the mod moves the five to bigger tables of its own (16 times or more), all or
// nothing, each place checked against the game's value first, and logs how full each is.
//
// Guerrilla's player animations for its guns (GuerrillaBuilder.exe builds them with the guns: see
// RfaFormats\Guerrilla\GuerrillaPlayerAnimations.cs): a set of their own, rfg_player, built on Darius's
// (vs_player) with the variant "confident", which nothing of Armageddon's uses. A character's set is
// chosen when it is made; the game's variant functions (Guerrilla's, still in Armageddon: a drunk or
// wounded civilian plays its variant set) switch a character to the set built on its own that has a
// variant, and back. The mod needs three things for it:
//   - the switch: when the player takes up a gun the game sets his animation group, the gun's
//     (character_instance::set_animation_group, 0x969110, thiscall: the group, a blend, ...; from
//     human::equip, 0x84DD70, with the gun just taken up at human+0x1394 -> +8 -> +0x20, its weapon
//     entry, whose +0 is its name). The mod runs it with the player's set switched first
//     (vanim_set_base_animation_set_variant, 0x98C310, cdecl: the character's animation state, the
//     character instance's +4; the variant, -1 for none) to rfg_player for one of the guns
//     mods\guerrilla_gear\player_weapons.txt names, and back to Darius's own for any other;
//   - room in the (set, animation) map: every set's states and actions are one place each in three
//     tables and a bit set the game makes while it reads the sets (0x97D6B0, from the sets' loading
//     0x979C20), from the Permanent Mempool (0x26AEE78), 6,344 places (the value at 0x1107C58, read only
//     there). The sets' reader doesn't check it: more states and actions than that spoil the memory
//     after the tables. Armageddon's own and Guerrilla's vehicles' and people's sets come close to it,
//     and rfg_player adds about 650, so the mod raises the value to 16,384 just before the sets are
//     read (a value in the game's data, not its code), if the mempool has room for it, and says how
//     many are in use after;
//   - the animations: written into mods\guerrilla_gear\animations\ (rfgpl_<Guerrilla's name>.anim_pc,
//     Armageddon's packing), loaded once and kept, as Armageddon keeps its player's: right after the
//     sets' loading (the animation library was emptied just before it, 0xD89F00 from 0x967B90, and
//     nothing empties it after), each one overlaid on its place in the library by name (as the
//     guerrilla_people mod loads its people's).
//
// Guerrilla's dropped guns. Guerrilla's NPCs carry the player's own guns (its EDF the m16 and the
// enforcer), and a gun one drops (dying, ragdolling, the AI's drop action) can be picked up as any
// other. What it carries is Guerrilla's own rule (weapon::adjust_dropped_ammo, for a gun anyone but the
// player drops): the rounds it and its owner's reserve held × its Dropped_ammo_scale (0.4 for every EDF
// gun) × the difficulty's "player ammo pickup" (Easy 2.0, Medium 1.0, Hard 0.9, Insane 0.8); a
// magazine of 0.5 to 1.5 × the scale of a full one, at least 1 round; the rest in reserve. Picking it
// up: walking over it gives its rounds as ammo if the player has that gun, else the gun itself with
// them into a free slot; with none free, the action button swaps it for the gun in hand. Armageddon
// keeps the rules (its pickup, 0x7CAE20, the items' give_item touch; its swap, 0x8BEB00; its drop,
// 0x8A2DB0, which calls the same step, 0x731A10, for an NPC's gun only) but not the numbers: its step
// keeps a full magazine and moves the scale × the owner's reserve into the gun, its pickup gives the
// firing mode's Rounds_On_Pickup instead of the gun's rounds, and its "player ammo pickup" is 1.0 at
// every difficulty. So, for Guerrilla's guns (player_weapons.txt), the mod runs Guerrilla's step in
// place of Armageddon's (0x731A10 wrapped: thiscall, the weapon) and remembers the rounds; the game's
// pickup (0x7CAE20 wrapped: cdecl, the human, the item, two flags) gives a gun he has its firing mode's
// Rounds_On_Pickup (mode+0x144, through 0x8A39C0: as much as his reserve holds, shown as "+N"), so for
// that one call the mod sets it to the dropped gun's rounds: he gets Guerrilla's, and the "+N" says so.
// A gun he hasn't, into a free slot, and the swapped-in gun (0x85B3E0 wrapped: cdecl, the human, the
// item, a slot, three flags) get their magazine (the game's setter 0x7310F0, thiscall: the rounds, the
// mode) and reserve set to them, through the game's own ammo store (0x84E8F0; its reserve 0x70EF40, add
// 0x70F4F0, take 0x70F650). A gun's magazine is its mode's +0x118 (weapon+0x2E0, what firing takes
// from, 0x730FB0); +0x134 (weapon+0x2FC) is the rounds a gun lying on the ground carries besides (what
// Armageddon's own step adds to and its pickup empties, 0x731BB0). Guerrilla's difficulty values come
// from the player's Guerrilla (mods\guerrilla_gear\ammo_pickup.txt), the game's difficulty from 0x10F7E98.
//
// How much ammo he carries: Guerrilla's weapon::get_max_rounds is the weapon's Max_Rounds (the loaded
// magazine counted in), Max_Rounds_Upgrade once its weapon upgrade is bought to 2 ("... AMMO",
// upgrade_weapon_has_extra_ammo), × the worn backpack's ammo_factor (from its active abilities while on, else its
// passive ones: none of Guerrilla's packs has one in either). Armageddon's
// ammo type holds its Max_Reserve (ammo info +4; +8 in multiplayer) × the store's factor less the rounds
// loaded in his guns of that type (0x70EB80), so the weapons build writes Max_Rounds there, and the mod
// sets it to the upgraded number × the pack's factor while they apply (mods\guerrilla_gear\
// ammo_capacity.txt, from the build; the ammo types by name 0x70FDA0).
//
// The player turns to where he walks. Armageddon keeps Darius facing the camera: its heading
// step (0x84C6D0, cdecl, the human; once a frame for each one on foot) sets the player's turn
// (human+0x664) toward the camera unless he sprints (player+0x1830, 0x20), and its movement step
// (0x89D0C0) turns a character toward where it goes only if its character entry lacks "always face
// target" (entry+0x28A, 0x04; Armageddon added it, its player has it, Guerrilla's Mason didn't). Its
// sprint is the one time Darius turns all the way round. The player keeps Armageddon's movement and
// does the same while he walks or runs, Darius (vs_player) in every world and Mason (rfg_mason, the
// Guerrilla world's player): the mod wraps the heading step, and for the player playing one of those
// sets, on foot, moving (this frame's movement input, 0x11BE7C4 / 0x11BE7C0), not sprinting, not
// aiming (fine aim 0x8BB2F0 or a scope, the zoom state player +0x17E4, or the aim button held) and not
// attacking (the attack buttons, 0x4E4800, and 1.5 s after), it lifts the
// flag from his entry (the entries are one table, 0x21B7538, kept for the whole game) and lets no
// camera turn through, as the sprint does; aiming or attacking he faces the camera as before.
// Standing still, Mason doesn't turn either: Guerrilla turns its player (human_set_steering_heading_offset)
// to the camera while he is combat ready, to where he goes while he moves, and not at all while he
// stands, so its camera goes round him. Darius does the same in every world (the user: "Darius too").
// Standing, the flag is lifted too: with it the movement step sets the player's facing to his target,
// where the camera aims (0x89D58C, 0x868910), every frame. And standing he looks ahead, not round at the
// camera's aim (his spine, neck and head bent toward it): Guerrilla's player looks the camera's way only
// while combat ready (see "Standing still, he looks ahead" below).
//
// Guerrilla's attacks that hold Mason where he stands. A human carries the action that holds him
// (human+0x6F0, an action handle; -1 for none) and how (+0x704: 0 his place, 1 his heading, 2 both, as
// Guerrilla's block_movement_type), set when an attack, throw, equip or landing starts. While that action
// plays an animation flagged Zero_Input, the player's movement step (0x8BDCF0) takes no input; the game
// lets go at the animation's "release movement" trigger or its end (0x89E9E0), and moving after that
// ramps the rest of it out (0x855BE0); a hold of his heading (1) keeps the heading step (0x84C6D0) from
// turning him to the camera. That is Guerrilla's rule, function for function. Guerrilla's melee start
// (human_combat_play_combat_move) holds everyone swinging; Armageddon's twin (0x880420, cdecl: the human,
// the move, two flags) holds a human swinging on the move (the move's "Forward" variant, his heading
// held on the camera's way) but one standing only if it isn't the player (0x880952), so Mason walked
// through his standing swings. The mod wraps it and, for the player playing Mason's set, holds him on the
// melee slot (0x2007) as Guerrilla does. Throws need nothing: Armageddon's throw start (0x8A3FC0) holds
// everyone as Guerrilla's does (the throw slot; his heading for a throw, both placing or throwing on the
// move). Each kind of hold Mason is put in is written to the log once ("Held: ...").
//
// Room for more projectiles (the user, 2026-11-06: "Yes, enlarge it"). Every projectile type
// (projectiles.xtbl: Armageddon's 88, its add-on's 3, Guerrilla's thrown and launched weapons' and its
// turrets') takes a place in one pool of 100 (0x2174F90, 0xC0 bytes a type), which its start-up
// constructor (0x82FE60, run with the game's own start-up code, after the mods start) builds in a
// buffer inside the pool object; a type past it is dropped without a word. Their names go into a
// 2 KB pool ("Proj String Pool") and their stages into a 96 KB one ("Projectile Mempool"), both blocks
// the projectile loader (0x82E9F0) takes from the Permanent Mempool once. The constructor's count is a
// one-byte value (127 at most) and its buffer a fixed part of the object, so the mod runs the
// constructor itself (its same calls: the pool's base, the pool made in a memory pool laid over a
// buffer, the memory pool dropped) with a buffer of its own for 256, and changes the loader's four
// sizes (each a 4-byte value) to 8 KB for the names and 256 KB for the stages: each checked against
// the game's first, none changed if one differs, those already changed put back if one can't be.
// After the loader it logs how many places, name bytes and stage bytes are in use.
//
// Room for more melee moves (the user, 2026-11-06: "Yes, enlarge it"). Every melee move (melee.xtbl:
// Armageddon's 22, Guerrilla's under rfg_ names) takes a place in a pool of 32 (0x21AFEF0, 0xE0 bytes a
// move) its start-up constructor (0x881BC0) builds as the projectiles' is built, in a buffer inside the
// object with a one-byte count; past it the reader stops. The mod runs that constructor too, with a
// buffer of its own for 64 (its three values checked first), and logs the use after melee.xtbl is read
// (0x876900 wrapped). The moves' names go into a string pool of their own, "Melee Combat Strings"
// (0x21B1BD8), of 512 bytes: the melee set-up (0x8777E0) takes 512 bytes from the Permanent Mempool
// (`push 200h` at 0x8777F2) and makes the pool over them (`push 200h` at 0x87783C); Armageddon's 22 names
// nearly fill it, and with Guerrilla's the game stopped ("Length of string pool exceeded", string_pool.cpp,
// the user's 19:57 run). Both values are made 4 KB, checked first, all or nothing, before the set-up runs
// (it runs once, at start-up); the use is logged after the reader and shown in F7 Limits. The vehicle
// reader's names pool is the same kind and as small for Guerrilla's 153 vehicles (the user, 2026-11-06:
// "Yes, raise both"): "Vehicle info strings" (0x14A15F8: the vehicles' names, models, display names,
// components, lights and props, about 9.3 KB of 8 KB; `push 2000h` at 0x6271DF and 0x627227); it is made
// 32 KB the same way. (Its neighbour "Vehicle interaction info strings", whose overflow stopped the user's
// 20:02 run, is raised by the guerrilla_vehicles mod.)
//
// Room for the preloaded items (the user, 2026-11-06: "Yes, raise it"). At start-up (0xA45600) the game
// allocates a block for its "item_preload" pool (0xA44A20: _aligned_malloc from the process heap, size
// pushed at 0xA47520) and makes the pool of it (0x512B30 on 0x26AF1A0, the same size pushed at
// 0xA4754E): 17 MB. Every "General (preloaded)" item's own container goes into it, then Armageddon's
// items_containers.gpeg_pc (its items' low-detail textures, 8.8 MB); with Guerrilla's weapons' and
// pickups' containers the peg no longer fit (the user's 18:44 and 19:38 runs). The mod makes both values
// 32 MB, before the game's start-up code runs: each checked against the game's first, neither changed if
// one differs, the first put back if the second can't be written. The pool's use shows in F7 Limits.
// The Permanent Mempool (0x26AEE78, what the game keeps for good from start-up) is made the same way just
// before it, 12 MB (`push 0C00000h` at 0xA47276 and 0xA472A4); Armageddon's own and the mods' raised tables
// no longer fit (the user's 21:19 run: "Failed aligned alloc ... in Permanent mempool cpu pool"), so it is
// made 24 MB the same way. Both pools' use is logged at the player's first step.
//
// Mason's sledgehammer. Guerrilla's Mason always has his hammer: it starts in his kit (slot 3) and
// that slot can't be swapped (player_can_swap_inv_item). With Guerrilla's hammer built (the weapons
// build's rfg_sledgehammer, sledge_upgrade_1, sledge_upgrade_2), the player playing Mason's set
// (rfg_mason) who has none of them is given the plain one, into slot 3 if it's free (the game's own
// add-by-inventory-item, 0x85B260, which takes the slot; give_player_item asks it for -1, the first
// free), checked every two seconds from the heading step above. And the game's swap (0x8BEB00 wrapped:
// cdecl, the human, the item on the ground, the slot or -1 for the gun in his hands) is refused for the
// player when the gun it would drop is one of Guerrilla's hammers. With Stonebreaker or Shattermaster
// bought (upgrades.cpp), that one is the hammer he is given: one he carries that is lesser is dropped
// (the game's own drop of an inventory record, 0x8A2DB0: cdecl, the human, the record; from his hands
// or his slots) and the better given into its slot.
//
// A carried thing thrown (Guerrilla's propane tank). Guerrilla's Mason picks a tank up and throws it: its
// weapon is "non-inventory", and launch_thrown_weapon (RM 0xBE79A0) throws the item itself, no projectile:
// out of his hands, enable_physics, human_remove_item_from_inventory, havok_body_apply_linear_velocity and a
// random spin, then player_equip_item (his last weapon). The tank bounces and rolls, and goes off only from
// damage (its item: can_explode, damaged_by_bullets, damaged_by_explosions); carried, its melee is the bash
// (propane_tank_melee). Armageddon kept that launch (a "Thrown" shot: the player's attack 0x8BAAF0 ->
// 0x8A3FC0, let go at the animation's "impact start", 0x8A1E50 -> the weapon's launch 0x7323C0: thiscall,
// position, orientation, velocity, placed; a non-inventory weapon: unequipped 0x85D3C0, let fly, its record
// out of the inventory 0x85C260, the last weapon equipped 0x8BB420). So the throw is the game's own; the
// launch is wrapped only to log it and to watch his hands after: should the game find no last weapon to go
// back to, the heading step gives him his sledgehammer (Guerrilla's player_equip_sledgehammer) or the first
// weapon in his slots. (Until 2026-11-19 the mod threw the weapon's projectile instead, which blew up on
// contact: the user's "they only explode when shot", Re-Mars-tered's way.) Set down (another weapon, a swap)
// it leaves his hands as Guerrilla's (SetDown, below).

// Materials the game draws red (a diagnostic). The game's shader library (0x6F2660 gives it) keeps each
// shader under its name with a version for each kind of mesh and variant, a 4 by 4 table filled from the
// shader files' endings (0x11234D8: kind 1 still _s, 2 skinned _c, 3 terrain _t; variant bit 0 "b", bit 1
// "m": _bs, _ms, _bms, _bc, _mc, _bmc, _ts). Binding a material's shader for a draw (0xD4D7D0, thiscall:
// the shader's number, two more) asks for the version of the mesh being drawn (the renderer's 0xD4E060 and
// 0xD4E0B0; 0xD4D650 says whether there is one), and with none, or with no shader at all (-1), draws with
// rl_debug_highlight instead: flat bright red. The binding is wrapped only to write to the log, once
// each, which shader was asked for which kind when that happens ("Red: a material on gw_standard1 is
// drawn as a mesh of the kind _c, and there is no gw_standard1_c"); what the game draws is unchanged.

#define RFA_MOD_NAME "guerrilla_gear"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "gear_api.h"
#include "campaign/campaign_api.h"
#include "rfa_game.h"
#include "rfa_menu.h"
#include "guerrilla_gear.h"
#include <malloc.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr DWORD kInventoryItems = 0x19F8578;
constexpr DWORD kInventoryItemSize = 0x30;
constexpr DWORD kInventoryItemCount = 0x19F8570;
constexpr DWORD kGameMaxInventoryItems = 85;
constexpr DWORD kMaxInventoryItems = 127;  // the most a one-byte, signed size in the game's code can hold
// The 4-byte values in the game's code that hold the table's address (or its first item's +4).
constexpr DWORD kInventoryItemReferences[] = {0x7A782C, 0x7A7DBA, 0x7A8319, 0x7A832E, 0xF2C33D};
// The one-byte values that hold its size: the loader's check and the start-up constructor's push.
constexpr DWORD kInventoryItemLimits[] = {0x7A77F6, 0xF2C339};
alignas(16) BYTE g_inventoryItems[kMaxInventoryItems * kInventoryItemSize];
auto const BuildInventoryItem = reinterpret_cast<void*(__fastcall*)(void* item, void* unused)>(0x963420);

void MoreInventoryItems() {
    const int count = *reinterpret_cast<const int*>(kInventoryItemCount);
    if (count != 0) {
        rfa::Log("Inventory items: the game has already read %d; it keeps its %lu", count, kGameMaxInventoryItems);
        return;
    }
    for (DWORD at : kInventoryItemReferences) {
        const DWORD value = rfa::CodeValue(at);
        if (value < kInventoryItems || value >= kInventoryItems + kInventoryItemSize) {
            rfa::Log("Inventory items: the game's code at 0x%08lX isn't as expected (0x%08lX); the game keeps its %lu", at, value, kGameMaxInventoryItems);
            return;
        }
    }
    for (DWORD at : kInventoryItemLimits) {
        const BYTE size = *reinterpret_cast<const BYTE*>(at);
        if (size != kGameMaxInventoryItems) {
            rfa::Log("Inventory items: the game's size at 0x%08lX isn't %lu (it's %u); the game keeps its own", at, kGameMaxInventoryItems, size);
            return;
        }
    }
    for (DWORD i = 0; i < kMaxInventoryItems; ++i) BuildInventoryItem(g_inventoryItems + i * kInventoryItemSize, nullptr);
    const DWORD moved = reinterpret_cast<DWORD>(g_inventoryItems);
    for (DWORD at : kInventoryItemReferences) {
        const DWORD value = moved + (rfa::CodeValue(at) - kInventoryItems);
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &value, sizeof(value))) {
            rfa::Log("Inventory items: could not change the game's code at 0x%08lX (part of it already points at the new table)", at);
            return;
        }
    }
    const BYTE size = static_cast<BYTE>(kMaxInventoryItems);
    for (DWORD at : kInventoryItemLimits)
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &size, sizeof(size))) {
            rfa::Log("Inventory items: could not change the game's size at 0x%08lX", at);
            return;
        }
    rfa::LogDetail("Inventory items: the game holds %lu (its own %lu): the table moved to 0x%08lX, %u places in its code changed",
             kMaxInventoryItems, kGameMaxInventoryItems, moved,
             static_cast<unsigned>(std::size(kInventoryItemReferences) + std::size(kInventoryItemLimits)));
}

// ---- Room for the animations' triggers (see the top)

// One of the pools the sets' reader copies each animation's anim_files.xtbl lists into: the game's
// own (a static array), its record size and room, the count of records in use (the reader adds each
// animation's to it), the two places in the reader's code that hold the pool's address (an instruction's
// 4-byte value; each pool's address is in its reader only), and our room.
struct TriggerPool {
    const char* name;
    DWORD gameBase;
    DWORD recordSize;
    DWORD gameRoom;
    DWORD count;
    DWORD references[2];
    DWORD room;
    BYTE* ours;
};
alignas(16) BYTE g_effectPool[1024 * 16];
alignas(16) BYTE g_voicePool[4096 * 8];
alignas(16) BYTE g_soundPool[16384 * 16];
alignas(16) BYTE g_propPool[4096 * 36];
alignas(16) BYTE g_ikPool[16384 * 8];
TriggerPool g_triggerPools[] = {
    // Effects (0x97B940): add ecx,261C4D0h at 0x97B99A, add edx,261C4D0h at 0x97BABC
    {"Effects", 0x261C4D0, 16, 64, 0x2632190, {0x97B99C, 0x97BABE}, 1024, g_effectPool},
    // Voice_Lines (0x97B820): lea eax,[edx*8+261C8D0h] at 0x97B881, lea edx,[ecx*8+261C8D0h] at 0x97B908
    {"Voice_Lines", 0x261C8D0, 8, 320, 0x263218C, {0x97B884, 0x97B90B}, 4096, g_voicePool},
    // Sounds (0x97B640): add edx,261D2D0h at 0x97B6A4, add eax,261D2D0h at 0x97B7E6
    {"Sounds", 0x261D2D0, 16, 960, 0x2632188, {0x97B6A6, 0x97B7E7}, 16384, g_soundPool},
    // Props (0x97ADA0): add ecx,2620ED0h at 0x97ADFC and at 0x97B3C2
    {"Props", 0x2620ED0, 36, 256, 0x2632184, {0x97ADFE, 0x97B3C4}, 4096, g_propPool},
    // IKs (0x97B400): lea eax,[edx*8+26232D0h] at 0x97B46D and at 0x97B60B
    {"IKs", 0x26232D0, 8, 1300, 0x2632180, {0x97B470, 0x97B60E}, 16384, g_ikPool},
};
bool g_triggerPoolsMoved;

void MoreTriggerRoom() {
    // All or nothing: every place checked first, and nothing read yet.
    for (const TriggerPool& pool : g_triggerPools) {
        if (*reinterpret_cast<const DWORD*>(pool.count) != 0) {
            rfa::Log("Animation triggers: the game has already read its %s; it keeps its own pools", pool.name);
            return;
        }
        for (DWORD at : pool.references)
            if (rfa::CodeValue(at) != pool.gameBase) {
                rfa::Log("Animation triggers: the game's code at 0x%08lX isn't as expected (0x%08lX, not 0x%08lX); it keeps its own pools",
                         at, rfa::CodeValue(at), pool.gameBase);
                return;
            }
    }
    unsigned changed = 0;
    for (const TriggerPool& pool : g_triggerPools)
        for (DWORD at : pool.references) {
            const DWORD moved = reinterpret_cast<DWORD>(pool.ours);
            if (!rfa::PatchCode(reinterpret_cast<void*>(at), &moved, sizeof(moved))) {
                // Put back the ones already changed, so no pool is split between two places.
                for (const TriggerPool& back : g_triggerPools)
                    for (DWORD undo : back.references)
                        if (rfa::CodeValue(undo) == reinterpret_cast<DWORD>(back.ours))
                            rfa::PatchCode(reinterpret_cast<void*>(undo), &back.gameBase, sizeof(back.gameBase));
                rfa::Log("Animation triggers: could not change the game's code at 0x%08lX; it keeps its own pools", at);
                return;
            }
            ++changed;
        }
    g_triggerPoolsMoved = true;
    rfa::LogDetail("Animation triggers: the pools moved to the mod's own (effects %lu, voice lines %lu, sounds %lu, props %lu, IKs %lu; "
             "the game's 64, 320, 960, 256, 1300), %u places in its code changed",
             g_triggerPools[0].room, g_triggerPools[1].room, g_triggerPools[2].room, g_triggerPools[3].room, g_triggerPools[4].room, changed);
}

// After the sets are read: how full each pool is.
void LogTriggerPools() {
    for (const TriggerPool& pool : g_triggerPools) {
        const DWORD used = *reinterpret_cast<const DWORD*>(pool.count);
        const DWORD room = g_triggerPoolsMoved ? pool.room : pool.gameRoom;
        rfa::LogDetail("Animation triggers: %s %lu of %lu%s", pool.name, used, room,
                 used > room ? ": MORE than the pool holds, the game's memory after it is spoilt (expect a crash)" : "");
    }
}

// ---- Guerrilla's player animations: the files

std::wstring GearFolder() { return rfa::RootDir() + L"\\mods\\guerrilla_gear\\"; }

// The guns that take rfg_player up (their weapons.xtbl names, one a line).
std::vector<std::string> g_playerWeapons;

void ReadPlayerWeapons() {
    std::ifstream list(GearFolder() + L"player_weapons.txt");
    for (std::string line; std::getline(list, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line.size() < 64) g_playerWeapons.push_back(line);
    }
}

// ---- Guerrilla's player animations: the switch

constexpr DWORD kSetAnimationGroup = 0x969110;
constexpr size_t kSetAnimationGroupPrologue = 6;  // push ebp / mov ebp,esp / sub esp,14h
// thiscall (the character instance) with five values on the stack, which it takes off (ret 14h): the
// group, an 8-byte index alias, a blend time and a flag.
using SetAnimationGroupFn = void(__fastcall*)(BYTE* character, void* unused, int group, DWORD aliasLow, DWORD aliasHigh, float blend, int flag);
SetAnimationGroupFn g_setAnimationGroup;

auto const LocalPlayer = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);
auto const SetBaseVariant = reinterpret_cast<void(__cdecl*)(void* animation, int variant)>(0x98C310);
constexpr DWORD kCharacterInstance = 0x760;  // human -> its character instance
constexpr DWORD kAnimationState = 0x4;       // character instance -> its animation state (v_animate)
constexpr DWORD kHeldItem = 0x1394;          // human -> the item in its hand (-> +8 its info -> +0x20 its weapon entry)
constexpr int kConfident = 10;               // the variant rfg_player has (Armageddon's anim_set_variants: 10 confident)
constexpr int kNoVariant = -1;

// The weapon entry of the item in a human's hand, or null.
const BYTE* HeldWeapon(const BYTE* human) {
    const BYTE* item = *reinterpret_cast<const BYTE* const*>(human + kHeldItem);
    if (!item) return nullptr;
    const BYTE* info = *reinterpret_cast<const BYTE* const*>(item + 8);
    if (!info) return nullptr;
    return *reinterpret_cast<const BYTE* const*>(info + 0x20);
}

const char* WeaponName(const BYTE* weapon) { return weapon ? *reinterpret_cast<const char* const*>(weapon) : nullptr; }

bool IsGuerrillaGun(const BYTE* weapon) {
    const char* name = WeaponName(weapon);
    if (!name) return false;
    for (const std::string& ours : g_playerWeapons)
        if (_stricmp(name, ours.c_str()) == 0) return true;
    return false;
}

int g_playerVariant = kNoVariant;  // the last one set, for the log

// rfg_player builds on Darius's set (its clips are for his skeleton), so it is taken up only by a
// player whose own set is Darius's: the animation state's +0x10 is the character's own set, a set's
// +0 its name (0x97C3BB). A player of another skeleton (Guerrilla's Mason) keeps his own set.
constexpr DWORD kBaseSet = 0x10;
const char* const kDariusSet = "vs_player";

bool PlaysDariusSet(const BYTE* animation) {
    const BYTE* set = *reinterpret_cast<const BYTE* const*>(animation + kBaseSet);
    const char* name = set ? *reinterpret_cast<const char* const*>(set) : nullptr;
    return name && _stricmp(name, kDariusSet) == 0;
}

void __fastcall SetAnimationGroup(BYTE* character, void* unused, int group, DWORD aliasLow, DWORD aliasHigh, float blend, int flag) {
    BYTE* player = LocalPlayer();
    if (character && player && *reinterpret_cast<BYTE**>(player + kCharacterInstance) == character) {
        BYTE* animation = *reinterpret_cast<BYTE**>(character + kAnimationState);
        const BYTE* weapon = HeldWeapon(player);
        const int variant = IsGuerrillaGun(weapon) ? kConfident : kNoVariant;
        if (animation && PlaysDariusSet(animation)) {
            SetBaseVariant(animation, variant);
            if (variant != g_playerVariant) {
                g_playerVariant = variant;
                const char* name = WeaponName(weapon);
                rfa::LogDetail("Player animations: %s (%s)", variant == kConfident ? "Guerrilla's" : "Darius's own", name ? name : "no gun");
            }
        }
    }
    g_setAnimationGroup(character, unused, group, aliasLow, aliasHigh, blend, flag);
}

// ---- Guerrilla's player animations: room in the (set, animation) map, and the animations

constexpr DWORD kLoadAnimationSets = 0x979C20;
constexpr size_t kLoadAnimationSetsPrologue = 8;  // push ebp / mov ebp,esp / mov eax,11E4h
using LoadAnimationSetsFn = int(__cdecl*)(bool addOn, int which);
LoadAnimationSetsFn g_loadAnimationSets;

constexpr DWORD kMapPlaces = 0x1107C58;      // how many places the map gets (the game's data)
constexpr DWORD kGameMapPlaces = 6344, kOurMapPlaces = 16384;
constexpr DWORD kMapBytesPerPlace = 12;      // three tables of 4-byte values (and a bit each in the bit set)
constexpr DWORD kPermanentMempool = 0x26AEE78;  // 12 MB, 24 MB with MoreStartupRoom (set up at 0xA472A4)
constexpr DWORD kMempoolSize = 0x44, kMempoolUsed = 0x4C;
constexpr DWORD kMempoolMargin = 1024 * 1024;  // left for what the game takes from it after (the sets' groups take ~350 KB next)
// The groups' records the lookup reads (44 of them, 12 bytes each: +4 each name's count of sets, a
// byte each; +8 each name's first place, a 2-byte value each), and how many state and action names.
constexpr DWORD kGroupRecords = 0x261C270, kGroupRecordSize = 12;
constexpr int kGroups = 44;
constexpr DWORD kStateNames = 0x25CD744, kActionNames = 0x25CD748;

DWORD MempoolRoom() {
    const DWORD size = *reinterpret_cast<const DWORD*>(kPermanentMempool + kMempoolSize);
    const DWORD used = *reinterpret_cast<const DWORD*>(kPermanentMempool + kMempoolUsed);
    return size > used ? size - used : 0;
}

void MoreMapRoom() {
    DWORD& places = *reinterpret_cast<DWORD*>(kMapPlaces);
    if (places != kGameMapPlaces) {
        rfa::LogDetail("Animation map: the game gives it %lu places, not its %lu; left as it is", places, kGameMapPlaces);
        return;
    }
    const DWORD more = (kOurMapPlaces - kGameMapPlaces) * kMapBytesPerPlace + (kOurMapPlaces - kGameMapPlaces) / 8 + 64;
    const DWORD room = MempoolRoom();
    if (room < more + kMempoolMargin) {
        rfa::Log("Animation map: the Permanent Mempool has %lu bytes left, too few for %lu more places; it keeps the game's %lu",
                 room, kOurMapPlaces - kGameMapPlaces, kGameMapPlaces);
        return;
    }
    places = kOurMapPlaces;
    rfa::LogDetail("Animation map: %lu places (the game's %lu): %lu bytes more of the Permanent Mempool's %lu left", kOurMapPlaces, kGameMapPlaces, more, room);
}

// How many places the sets read take: the furthest any group's names reach.
DWORD MapUse() {
    const int names = *reinterpret_cast<const int*>(kStateNames) + *reinterpret_cast<const int*>(kActionNames);
    DWORD use = 0;
    for (int g = 0; g < kGroups; ++g) {
        const BYTE* record = reinterpret_cast<const BYTE*>(kGroupRecords + g * kGroupRecordSize);
        const BYTE* counts = *reinterpret_cast<const BYTE* const*>(record + 4);
        const WORD* firsts = *reinterpret_cast<const WORD* const*>(record + 8);
        if (!counts || !firsts) continue;
        for (int i = 0; i < names; ++i)
            if (counts[i] && static_cast<DWORD>(firsts[i]) + counts[i] > use) use = static_cast<DWORD>(firsts[i]) + counts[i];
    }
    return use;
}

// The animation library: an animation's place by its name ("x.anim"; -1 if none), a new place for a
// name, and the data overlaid on the place a name has (keep: an unload passes over it).
auto const FindAnimation = reinterpret_cast<int(__cdecl*)(const char* name)>(0xD8C2C0);
auto const NewAnimation = reinterpret_cast<int(__cdecl*)(const char* name)>(0xD875F0);
auto const OverlayAnimation = reinterpret_cast<int(__cdecl*)(const char* name, void* data, unsigned size, bool keep)>(0xD8C0F0);
std::vector<std::unique_ptr<std::string>> g_animationNames;  // the library keeps the pointers

// Reads one file whole into memory aligned as the game's animation memory (16 bytes), kept for good.
void* ReadAnimation(const std::wstring& path, unsigned& size) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;
    const std::streamoff length = file.tellg();
    if (length <= 0 || length > 64 * 1024 * 1024) return nullptr;
    void* data = _aligned_malloc(static_cast<size_t>(length), 16);
    if (!data) return nullptr;
    file.seekg(0);
    if (!file.read(static_cast<char*>(data), length)) {
        _aligned_free(data);
        return nullptr;
    }
    size = static_cast<unsigned>(length);
    return data;
}

void LoadPlayerAnimations() {
    const std::wstring folder = GearFolder() + L"animations\\";
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((folder + L"*.anim_pc").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    unsigned loaded = 0, failed = 0, noRoom = 0;
    size_t bytes = 0;
    do {
        std::wstring file = found.cFileName;
        std::string stem;
        for (wchar_t c : file.substr(0, file.size() - 8)) stem += static_cast<char>(c);  // without .anim_pc
        if (stem.empty() || stem.size() > 58) { ++failed; continue; }
        g_animationNames.push_back(std::make_unique<std::string>(stem + ".anim"));
        const char* name = g_animationNames.back()->c_str();
        if (FindAnimation(name) < 0 && NewAnimation(name) < 0) { ++noRoom; continue; }
        unsigned size = 0;
        void* data = ReadAnimation(folder + file, size);
        if (!data) { ++failed; continue; }
        // (A place that has data already, or is kept already, is passed over.)
        if (OverlayAnimation(name, data, size, true) < 0) { _aligned_free(data); ++noRoom; continue; }
        ++loaded;
        bytes += size;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    rfa::LogDetail("Player animations: %u of Guerrilla's loaded and kept (%.1f MB)", loaded, bytes / 1048576.0);
    if (noRoom) rfa::Log("Player animations: %u had no place in the animation library (it is full: is guerrilla_vehicles on?)", noRoom);
    if (failed) rfa::Log("Player animations: %u could not be read", failed);
}

int __cdecl LoadAnimationSets(bool addOn, int which) {
    if (!addOn) MoreMapRoom();
    const int result = g_loadAnimationSets(addOn, which);
    if (!addOn) {
        const DWORD use = MapUse(), places = *reinterpret_cast<const DWORD*>(kMapPlaces);
        rfa::LogDetail("Animation map: %lu of its %lu places in use", use, places);
        if (use > places) rfa::Log("Animation map: MORE in use than it has places: the game's memory after it is spoilt (expect a crash)");
        LogTriggerPools();
        LoadPlayerAnimations();
    }
    return result;
}

void PlayerAnimations() {
    ReadPlayerWeapons();
    g_loadAnimationSets = reinterpret_cast<LoadAnimationSetsFn>(
        rfa::HookFunction(kLoadAnimationSets, kLoadAnimationSetsPrologue, reinterpret_cast<void*>(&LoadAnimationSets)));
    if (!g_loadAnimationSets)
        rfa::Log("Animation map: could not wrap the sets' loading; the map keeps its 6,344 places and Guerrilla's player animations aren't loaded");
    if (g_playerWeapons.empty()) {
        rfa::Log("Player animations: no guns take Guerrilla's up (no mods\\guerrilla_gear\\player_weapons.txt); Darius plays his own");
        return;
    }
    g_setAnimationGroup = reinterpret_cast<SetAnimationGroupFn>(
        rfa::HookFunction(kSetAnimationGroup, kSetAnimationGroupPrologue, reinterpret_cast<void*>(&SetAnimationGroup)));
    if (!g_setAnimationGroup) rfa::Log("Player animations: could not wrap the animation group's setting; Darius plays his own");
    else rfa::LogDetail("Player animations: Guerrilla's for %u guns", static_cast<unsigned>(g_playerWeapons.size()));
}

// ---- Guerrilla's dropped guns (see the top)

constexpr DWORD kAdjustDroppedAmmo = 0x731A10;
constexpr size_t kAdjustDroppedAmmoPrologue = 6;  // push ebp / mov ebp,esp / sub esp,18h
using AdjustDroppedAmmoFn = void(__fastcall*)(BYTE* weapon, void* unused);  // thiscall, no arguments
AdjustDroppedAmmoFn g_adjustDroppedAmmo;

constexpr DWORD kGiveItem = 0x7CAE20;
constexpr size_t kGiveItemPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0A8h
using GiveItemFn = bool(__cdecl*)(BYTE* human, BYTE* item, bool* ask, bool* other);
GiveItemFn g_giveItem;

constexpr DWORD kAddItem = 0x85B3E0;
constexpr size_t kAddItemPrologue = 9;  // push ebp / mov ebp,esp / sub esp,98h
using AddItemFn = BYTE*(__cdecl*)(BYTE* human, BYTE* item, int slot, bool first, bool second, bool third);
AddItemFn g_addItem;

constexpr DWORD kWeaponInfo = 0x188;          // a weapon -> its weapons.xtbl entry (whose +0 is its name)
constexpr DWORD kAdjustedOnlyIf = 0x18C;      // Armageddon's adjustment runs only while this is -1
constexpr DWORD kFiringModeSlot = 0x1C8;      // the weapon's first firing mode slot
constexpr DWORD kRoundsInMagazine = 0x2E0;    // and its rounds in the magazine (the slot's +0x118)
constexpr DWORD kMagazineSize = 0x13E;        // a firing mode's Magazine_Size (2 bytes)
constexpr DWORD kRoundsOnPickup = 0x144;      // and its Rounds_On_Pickup
constexpr DWORD kDroppedAmmoScale = 0x148;    // and its Dropped_Ammo_Scale
auto const SetMagazine = reinterpret_cast<void(__fastcall*)(BYTE* weapon, void* unused, int rounds, int mode)>(0x7310F0);
constexpr DWORD kInventoryInfo = 0x18;        // a weapon entry -> its inventory item; an inventory item -> its weapon's handle
constexpr DWORD kDifficulty = 0x10F7E98;      // the game's difficulty: 0 easy, 1 medium, 2 hard, 3 insane
auto const FiringModeInfo = reinterpret_cast<BYTE*(__fastcall*)(BYTE* slot, void* unused)>(0x42A1C0);
auto const OwnersAmmo = reinterpret_cast<void*(__fastcall*)(BYTE* weapon, void* unused)>(0x741310);
auto const HumansAmmo = reinterpret_cast<void*(__fastcall*)(BYTE* human, void* unused)>(0x84E8F0);
auto const ReserveRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot)>(0x70EF40);
auto const AddRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot, int rounds)>(0x70F4F0);
auto const TakeRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot, int rounds)>(0x70F650);
auto const HasItem = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human, const void* inventoryInfo)>(0x85CD70);
auto const ObjectFromHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x436250);
void* const kWorld = reinterpret_cast<void*>(0x1870630);

// Guerrilla's "player ammo pickup" at each difficulty (mods\guerrilla_gear\ammo_pickup.txt, from the
// player's own Guerrilla; 1.0 where it says nothing).
float g_ammoPickup[4] = {1.0f, 1.0f, 1.0f, 1.0f};

void ReadAmmoPickup() {
    std::ifstream list(GearFolder() + L"ammo_pickup.txt");
    const char* const levels[] = {"Easy", "Medium", "Hard", "Insane"};
    std::string level;
    float value = 0;
    while (list >> level >> value)
        for (int i = 0; i < 4; ++i)
            if (_stricmp(level.c_str(), levels[i]) == 0 && value > 0) g_ammoPickup[i] = value;
}

float AmmoPickup() {
    const int level = *reinterpret_cast<const int*>(kDifficulty);
    return g_ammoPickup[level >= 0 && level < 4 ? level : 1];  // Custom (4) as Medium (the campaign's rule)
}

float RandomRange(float low, float high) {
    static std::mt19937 random(std::random_device{}());
    return std::uniform_real_distribution<float>(low, high)(random);
}

// A gun an NPC dropped: what it carries, as Guerrilla gives it (the item is the weapon itself).
struct DroppedGun {
    const BYTE* info;  // its weapon entry, to tell the same object reused for another gun
    int magazine, reserve;
};
std::map<const BYTE*, DroppedGun> g_droppedGuns;

const DroppedGun* Dropped(const BYTE* item) {
    auto found = g_droppedGuns.find(item);
    if (found == g_droppedGuns.end()) return nullptr;
    if (*reinterpret_cast<const BYTE* const*>(item + kWeaponInfo) != found->second.info) {
        g_droppedGuns.erase(found);
        return nullptr;
    }
    return &found->second;
}

const char* NameOf(const BYTE* info) { return info ? *reinterpret_cast<const char* const*>(info) : "?"; }

// Guerrilla's weapon::adjust_dropped_ammo for one of its guns, in Armageddon's terms: the total the
// gun and its owner's reserve held × its Dropped_Ammo_Scale × the difficulty's player ammo pickup; a
// magazine of 0.5 to 1.5 × the scale of a full one (at least 1 round, at most a full magazine); the
// rest in reserve (as much as the ammo type holds, when it's given). Armageddon's own (any other gun)
// keeps the magazine and moves the scale × the owner's reserve into the gun.
void __fastcall AdjustDroppedAmmo(BYTE* weapon, void* unused) {
    const BYTE* info = weapon ? *reinterpret_cast<const BYTE* const*>(weapon + kWeaponInfo) : nullptr;
    BYTE* mode = info && IsGuerrillaGun(info) && *reinterpret_cast<const int*>(weapon + kAdjustedOnlyIf) == -1
                     ? FiringModeInfo(weapon + kFiringModeSlot, nullptr) : nullptr;
    if (!mode) {
        g_adjustDroppedAmmo(weapon, unused);
        return;
    }
    const int magazineSize = *reinterpret_cast<const WORD*>(mode + kMagazineSize);
    const float scale = *reinterpret_cast<const float*>(mode + kDroppedAmmoScale);
    const int magazine = *reinterpret_cast<const int*>(weapon + kRoundsInMagazine);
    void* ammo = OwnersAmmo(weapon, nullptr);
    const int reserve = ammo ? ReserveRounds(ammo, nullptr, weapon, 0) : 0;
    const int total = static_cast<int>(scale * static_cast<float>(magazine + reserve) * AmmoPickup());
    int dropped = static_cast<int>(static_cast<float>(magazineSize) * (RandomRange(0.5f, 1.5f) * scale));
    dropped = (std::max)(1, (std::min)(dropped, magazineSize));
    const int rest = (std::max)(0, total - dropped);
    rfa::LogDetail("Dropped guns: %s dropped by an NPC with %d | %d (it had %d | %d; Guerrilla's: x %.2f, difficulty x %.2f)",
             NameOf(info), dropped, rest, magazine, reserve, scale, AmmoPickup());
    g_droppedGuns[weapon] = {info, dropped, rest};  // given when he picks it up (below)
}

// The player's reserve of a gun's ammo type set to one value, through the game's own ammo store
// (adding is held to what the type holds).
void SetReserve(void* ammo, BYTE* weapon, int now, int wanted) {
    if (now > wanted) TakeRounds(ammo, nullptr, weapon, 0, now - wanted);
    else if (now < wanted) AddRounds(ammo, nullptr, weapon, 0, wanted - now);
}

// Walking over a gun (items_3d Touch_Script give_item). Armageddon's: a gun the player has gives him
// its Rounds_On_Pickup as ammo (and the "+N" shows it); one he hasn't, with a free slot (0-3), is added
// as a new gun of his; with none, the prompt to swap. Guerrilla's (item_touch_give_item) gives a dropped
// gun's own rounds: as ammo if he has the gun, else the gun itself with them. So for one of Guerrilla's
// guns an NPC dropped, the game's pickup runs with the gun's rounds as its Rounds_On_Pickup (put back
// after), and a new gun's magazine and reserve are then made Guerrilla's.
bool __cdecl GiveItem(BYTE* human, BYTE* item, bool* ask, bool* other) {
    BYTE* player = LocalPlayer();
    const DroppedGun* found = human && human == player && item ? Dropped(item) : nullptr;
    if (!found) return g_giveItem(human, item, ask, other);
    const DroppedGun drop = *found;
    const void* inventoryInfo = *reinterpret_cast<const void* const*>(drop.info + kInventoryInfo);
    void* ammo = HumansAmmo(player, nullptr);
    const bool had = HasItem(player, inventoryInfo) != nullptr;
    const int before = ammo ? ReserveRounds(ammo, nullptr, item, 0) : 0;
    BYTE* mode = FiringModeInfo(item + kFiringModeSlot, nullptr);
    int* onPickup = mode ? reinterpret_cast<int*>(mode + kRoundsOnPickup) : nullptr;  // the gun's weapons.xtbl entry's
    const int ownOnPickup = onPickup ? *onPickup : 0;
    if (onPickup) *onPickup = had ? drop.magazine + drop.reserve : drop.reserve;
    const bool result = g_giveItem(human, item, ask, other);
    if (onPickup) *onPickup = ownOnPickup;
    BYTE* held = HasItem(player, inventoryInfo);
    if (!ammo || !held) return result;  // not taken (no free slot: the swap, below)
    const int after = ReserveRounds(ammo, nullptr, item, 0);
    if (had && after == before) return result;  // his reserve is full: nothing given, the gun stays
    if (!had) {  // the magazine first: what's loaded counts against what the reserve holds
        BYTE* gun = ObjectFromHandle(kWorld, nullptr, *reinterpret_cast<const DWORD*>(held + kInventoryInfo));
        if (gun) SetMagazine(gun, nullptr, drop.magazine, 0);
        SetReserve(ammo, item, ReserveRounds(ammo, nullptr, item, 0), before);
        AddRounds(ammo, nullptr, item, 0, drop.reserve);
    }
    rfa::LogDetail("Dropped guns: %s picked up %s: %d | %d (reserve %d before, %d after the game's pickup, %d now)", NameOf(drop.info),
             had ? "as ammo" : "into a free slot", drop.magazine, drop.reserve, before, after, ReserveRounds(ammo, nullptr, item, 0));
    g_droppedGuns.erase(item);
    return result;
}

// Swapping for a gun on the ground (the action button, 0x8BEB00: the gun in hand dropped, the one on
// the ground added, itself): it comes with Guerrilla's rounds.
BYTE* __cdecl AddItem(BYTE* human, BYTE* item, int slot, bool first, bool second, bool third) {
    BYTE* player = LocalPlayer();
    const DroppedGun* found = human && human == player && item ? Dropped(item) : nullptr;
    if (!found) return g_addItem(human, item, slot, first, second, third);
    const DroppedGun drop = *found;
    void* ammo = HumansAmmo(player, nullptr);
    const int before = ammo ? ReserveRounds(ammo, nullptr, item, 0) : 0;
    BYTE* added = g_addItem(human, item, slot, first, second, third);
    if (!added || !ammo) return added;
    SetMagazine(item, nullptr, drop.magazine, 0);  // first: what's loaded counts against what the reserve holds
    SetReserve(ammo, item, ReserveRounds(ammo, nullptr, item, 0), before);
    AddRounds(ammo, nullptr, item, 0, drop.reserve);
    rfa::LogDetail("Dropped guns: %s swapped in: %d | %d (reserve %d before, %d now)", NameOf(drop.info), drop.magazine, drop.reserve,
             before, ReserveRounds(ammo, nullptr, item, 0));
    g_droppedGuns.erase(item);
    return added;
}

void DroppedGuns() {
    ReadAmmoPickup();
    if (g_playerWeapons.empty()) return;  // no Guerrilla guns built
    g_adjustDroppedAmmo = reinterpret_cast<AdjustDroppedAmmoFn>(
        rfa::HookFunction(kAdjustDroppedAmmo, kAdjustDroppedAmmoPrologue, reinterpret_cast<void*>(&AdjustDroppedAmmo)));
    g_giveItem = reinterpret_cast<GiveItemFn>(rfa::HookFunction(kGiveItem, kGiveItemPrologue, reinterpret_cast<void*>(&GiveItem)));
    g_addItem = reinterpret_cast<AddItemFn>(rfa::HookFunction(kAddItem, kAddItemPrologue, reinterpret_cast<void*>(&AddItem)));
    if (!g_adjustDroppedAmmo || !g_giveItem || !g_addItem)
        rfa::Log("Dropped guns: could not wrap the game's %s; Guerrilla's guns an NPC drops carry Armageddon's rounds",
                 !g_adjustDroppedAmmo ? "dropped-ammo step" : !g_giveItem ? "pickup" : "inventory");
    else
        rfa::LogDetail("Dropped guns: Guerrilla's rounds for its guns an NPC drops (player ammo pickup %.2f / %.2f / %.2f / %.2f)",
                 g_ammoPickup[0], g_ammoPickup[1], g_ammoPickup[2], g_ammoPickup[3]);
}

// ---- How much ammo he carries (see the top)

auto const AmmoTypeNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x70FDA0);  // ammo.xtbl's entry (0x40 bytes)
constexpr DWORD kAmmoMaxReserve = 0x4, kAmmoMaxReserveMp = 0x8;  // an ammo type's Max_Reserve, Max_Reserve_MP

struct AmmoCapacity {
    std::string weapon, ammo;  // our weapons.xtbl name, its ammo type
    int max = 0, upgraded = 0; // Guerrilla's Max_Rounds, Max_Rounds_Upgrade (0: none)
    BYTE* type = nullptr;      // the game's ammo type, once found
    int set = -1;              // what the mod last made it (-1: not yet)
    bool wrong = false;        // the type held something else: left alone
};
std::vector<AmmoCapacity> g_ammoCapacities;
ULONGLONG g_nextAmmoCapacity = 0;

void ReadAmmoCapacity() {
    std::ifstream list(GearFolder() + L"ammo_capacity.txt");
    std::string line;
    while (std::getline(list, line)) {
        std::istringstream fields(line);
        std::string kind;
        AmmoCapacity c;
        if (fields >> kind >> c.weapon >> c.ammo >> c.max >> c.upgraded && kind == "ammo" && c.max > 0) g_ammoCapacities.push_back(c);
    }
    if (g_ammoCapacities.empty())
        rfa::Log("Ammo capacity: no mods\\guerrilla_gear\\ammo_capacity.txt (build Guerrilla's weapons again); the ammo types keep what the build gave them");
    else
        rfa::LogDetail("Ammo capacity: Guerrilla's rounds for %zu guns (Max_Rounds; Max_Rounds_Upgrade with the gun's ammo upgrade; x a worn pack's ammo_factor)",
                 g_ammoCapacities.size());
}

// Once a second, on the game's thread: each gun's ammo type holds what Guerrilla's get_max_rounds gives.
// The type's number is written only where it holds the build's (or what the mod set): anything else
// means the table isn't this build's, and it is left alone.
void KeepAmmoCapacity() {
    const ULONGLONG now = GetTickCount64();
    if (g_ammoCapacities.empty() || now < g_nextAmmoCapacity) return;
    g_nextAmmoCapacity = now + 1000;
    const float factor = backpacks::AmmoFactor();
    for (AmmoCapacity& c : g_ammoCapacities) {
        if (c.wrong) continue;
        if (!c.type) c.type = AmmoTypeNamed(c.ammo.c_str());
        if (!c.type) continue;
        int& maxReserve = *reinterpret_cast<int*>(c.type + kAmmoMaxReserve);
        int& maxReserveMp = *reinterpret_cast<int*>(c.type + kAmmoMaxReserveMp);
        if (maxReserve != c.max && maxReserve != c.set) {
            rfa::Log("Ammo capacity: %s's ammo type %s holds %d, not the build's %d; left alone (build Guerrilla's weapons again)",
                     c.weapon.c_str(), c.ammo.c_str(), maxReserve, c.max);
            c.wrong = true;
            continue;
        }
        const bool extra = c.upgraded > 0 && upgrades::WeaponValue(c.weapon.c_str()) >= 2;
        const int wanted = static_cast<int>(static_cast<double>(extra ? c.upgraded : c.max) * factor);
        if (maxReserve == wanted && maxReserveMp == wanted) {
            c.set = wanted;
            continue;
        }
        rfa::LogDetail("Ammo capacity: %s carries %d rounds (Guerrilla's %s %d%s)", c.weapon.c_str(), wanted,
                 extra ? "Max_Rounds_Upgrade" : "Max_Rounds", extra ? c.upgraded : c.max,
                 factor != 1.0f ? (std::string(", x ") + std::to_string(factor).substr(0, 4) + " for the worn pack").c_str() : "");
        maxReserve = maxReserveMp = c.set = wanted;
    }
}

// ---- The player turns to where he walks (see the top)

constexpr DWORD kHeadingStep = 0x84C6D0;
constexpr size_t kHeadingStepPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0A4h
using HeadingStepFn = void(__cdecl*)(BYTE* human);
HeadingStepFn g_headingStep;

constexpr DWORD kCharacterEntry = 0x4FC;     // human -> its character entry (character.xtbl)
constexpr DWORD kEntryFlags = 0x28A;         // character entry: a flags byte; 0x04 "always face target"
constexpr BYTE kAlwaysFaceTarget = 0x04;
constexpr DWORD kOnFootState = 0x720;        // human: 0 on foot, as the heading step's own branch
constexpr DWORD kHeadingOffset = 0x664;      // human: the turn the heading step asks for
constexpr DWORD kPlayerStateFlags = 0x1830;  // player: 0x20 sprinting
constexpr DWORD kSprinting = 0x20;
const char* const kMasonSet = "rfg_mason";  // (Darius's, vs_player, is kDariusSet above)

auto const IsFineAiming = reinterpret_cast<bool(__cdecl*)(const BYTE* player)>(0x8BB2F0);  // player +0x17E4 == 2
// The player's zoom state (player +0x17E4, set by 0x8BAED0; Guerrilla's player_zoom_state: 0 off, 1 sprint, 2 fine aim,
// 3 scope) and the aim buttons (the controls "zoom" 0x1D and "zoom_toggle" 0x1E).
constexpr DWORD kZoomState = 0x17E4;
constexpr int kZoomFineAim = 2, kZoomScope = 3;
constexpr int kZoomControl = 0x1D, kZoomToggleControl = 0x1E;
auto const ControlHeld = reinterpret_cast<bool(__cdecl*)(int control, int controller, int unused)>(0x4E4800);
const int* const kLocalController = reinterpret_cast<const int*>(0x11C0580);
const float* const kMoveInputX = reinterpret_cast<const float*>(0x11BE7C4);  // this frame's movement input
const float* const kMoveInputY = reinterpret_cast<const float*>(0x11BE7C0);
constexpr int kPrimaryAttack = 0x11, kAltAttack = 0x12, kMeleeAttack = 0x13;
constexpr ULONGLONG kFightingMs = 1500;  // after the last attack he keeps facing the camera this long

BYTE* g_liftedEntry;        // the character entry whose "always face target" the mod has lifted, if any
ULONGLONG g_lastAttack;
const char* g_walkLogged;   // the set last logged walking free
const char* g_stillLogged;  // the set last logged standing still

bool PlaysSet(const BYTE* human, const char* setName) {
    const BYTE* character = *reinterpret_cast<const BYTE* const*>(human + kCharacterInstance);
    const BYTE* animation = character ? *reinterpret_cast<const BYTE* const*>(character + kAnimationState) : nullptr;
    const BYTE* set = animation ? *reinterpret_cast<const BYTE* const*>(animation + kBaseSet) : nullptr;
    const char* name = set ? *reinterpret_cast<const char* const*>(set) : nullptr;
    return name && _stricmp(name, setName) == 0;
}

// How the player turns this frame: to the camera (as Armageddon has it), to where he walks, or not at
// all (standing still).
enum class Facing { Camera, Walking, Still };

// Darius or Mason, on foot, not sprinting (Armageddon's own sprint already turns him free), not aiming
// and not attacking (nor in the moment after): moving, he walks free; standing, he stays as he is.
Facing PlayerFacing(BYTE* player, const char** why) {
    *why = "not Darius or Mason";
    const bool mason = PlaysSet(player, kMasonSet);
    if (!mason && !PlaysSet(player, kDariusSet)) return Facing::Camera;
    *why = "not on foot";
    if (*reinterpret_cast<const int*>(player + kOnFootState) != 0) return Facing::Camera;
    *why = "sprinting";
    if (*reinterpret_cast<const DWORD*>(player + kPlayerStateFlags) & kSprinting) return Facing::Camera;
    const int controller = *kLocalController;
    const ULONGLONG now = GetTickCount64();
    if (ControlHeld(kPrimaryAttack, controller, 0) || ControlHeld(kAltAttack, controller, 0) || ControlHeld(kMeleeAttack, controller, 0))
        g_lastAttack = now;
    // Aiming (the user, 2026-11-13: "we can still do the 360 movment when adsing"): fine aim (0x8BB2F0), a scope, or
    // the aim button held, whichever comes first; the plain walk is the only one that turns him all round.
    *why = "aiming";
    const int zoom = *reinterpret_cast<const int*>(player + kZoomState);
    if (IsFineAiming(player) || zoom == kZoomFineAim || zoom == kZoomScope || ControlHeld(kZoomControl, controller, 0) ||
        ControlHeld(kZoomToggleControl, controller, 0))
        return Facing::Camera;
    *why = "an attack button in the last 1.5 s";
    if (now - g_lastAttack < kFightingMs) return Facing::Camera;
    *why = "walking";
    if ((*kMoveInputX) * (*kMoveInputX) + (*kMoveInputY) * (*kMoveInputY) > 0.0001f) return Facing::Walking;
    *why = "standing still";
    return Facing::Still;  // Darius too (the user, 2026-11-12: "Darius too")
}

// ---- Guerrilla's attacks that hold Mason where he stands (see the top)

struct ActionHandle {  // the game's 8-byte action handle: 7 and a slot (0x2000 + n), 1 and an action, or 1, -1
    int kind;
    int value;
};
constexpr DWORD kHeldBy = 0x6F0;      // human: the action that holds him
constexpr DWORD kHoldKind = 0x704;    // human: 0 his place, 1 his heading, 2 both
constexpr int kMeleeSlot = 0x2007;    // the slot melee moves play in
constexpr int kThrowSlot = 0x2004;    // the slot throws play in
constexpr DWORD kAnimationFlags = 0x18;  // an animation's flags (anim_files.xtbl Flags)
constexpr DWORD kZeroInput = 0x10;       // Zero_Input
auto const SlotHandle = reinterpret_cast<ActionHandle*(__fastcall*)(ActionHandle* self, void* unused, int slot)>(0x98D940);
auto const HandleIsNot = reinterpret_cast<BOOL(__fastcall*)(const ActionHandle* self, void* unused, int value)>(0x98DAA0);
auto const AnimationPlaying = reinterpret_cast<int(__cdecl*)(void* animation_state, ActionHandle action)>(0x987570);  // -1: none
auto const AnimationInfo = reinterpret_cast<const BYTE*(__cdecl*)(int animation)>(0x95DE50);

constexpr DWORD kMeleeStart = 0x880420;
constexpr size_t kMeleeStartPrologue = 6;  // push ebp / mov ebp,esp / sub esp,5Ch
using MeleeStartFn = bool(__cdecl*)(BYTE* human, const void* move, bool first, bool second);
MeleeStartFn g_meleeStart;

bool IsHeld(const BYTE* human) {
    return HandleIsNot(reinterpret_cast<const ActionHandle*>(human + kHeldBy), nullptr, -1) != FALSE;
}

bool __cdecl MeleeStart(BYTE* human, const void* move, bool first, bool second) {
    const bool started = g_meleeStart(human, move, first, second);
    // Swung on the move the game has held his heading already; standing, the player isn't held at all.
    if (started && human && human == LocalPlayer() && PlaysSet(human, kMasonSet) && !IsHeld(human))
        SlotHandle(reinterpret_cast<ActionHandle*>(human + kHeldBy), nullptr, kMeleeSlot);
    return started;
}

// Each kind of hold Mason is put in, once a run: the action, how, and whether its animation takes his input.
constexpr int kMaxHoldLines = 16;
int g_holdLines;
ActionHandle g_lastHold = {1, -1};
int g_holdsLogged[kMaxHoldLines];

void LogHolds(BYTE* player) {
    if (g_holdLines >= kMaxHoldLines || !PlaysSet(player, kMasonSet)) return;
    const ActionHandle heldBy = *reinterpret_cast<const ActionHandle*>(player + kHeldBy);
    if (heldBy.kind == g_lastHold.kind && heldBy.value == g_lastHold.value) return;
    g_lastHold = heldBy;
    if (!IsHeld(player)) return;
    const BYTE* character = *reinterpret_cast<BYTE* const*>(player + kCharacterInstance);
    void* state = character ? *reinterpret_cast<void* const*>(character + kAnimationState) : nullptr;
    const int animation = state ? AnimationPlaying(state, heldBy) : -1;
    const BYTE* info = animation != -1 ? AnimationInfo(animation) : nullptr;
    const bool zeroInput = info && (*reinterpret_cast<const DWORD*>(info + kAnimationFlags) & kZeroInput);
    const int how = *reinterpret_cast<const int*>(player + kHoldKind);
    const int key = (heldBy.value << 4) | ((how & 3) << 1) | (zeroInput ? 1 : 0);
    for (int i = 0; i < g_holdLines; ++i)
        if (g_holdsLogged[i] == key) return;
    g_holdsLogged[g_holdLines++] = key;
    const char* what = heldBy.value == kMeleeSlot ? "a swing (the melee slot)"
                     : heldBy.value == kThrowSlot ? "a throw (the throw slot)" : nullptr;
    char other[32];
    if (!what) {
        std::snprintf(other, sizeof other, "the action 0x%X", heldBy.value);
        what = other;
    }
    rfa::LogDetail("Held: %s holds Mason (his %s); its animation %s", what,
             how == 0 ? "place" : how == 1 ? "heading" : how == 2 ? "place and heading" : "?",
             !info ? "isn't found" : zeroInput ? "takes his movement input until its release" : "lets him move (no Zero_Input)");
}

void HeldInPlace() {
    g_meleeStart = reinterpret_cast<MeleeStartFn>(
        rfa::HookFunction(kMeleeStart, kMeleeStartPrologue, reinterpret_cast<void*>(&MeleeStart)));
    if (!g_meleeStart) rfa::Log("Held: could not wrap the melee start; Mason can walk through a standing swing");
    else rfa::LogDetail("Held: Mason's standing swings hold him where he stands, as Guerrilla's do");
}

// The camera's look-at bone. A character entry keeps its skeleton's "camera" and "camera-lookat" bones
// (+0x1E0, +0x1E4; 0x886840 looks both up by name, 0xD8A120, -1 for none). Darius's skeleton has both:
// camera-lookat is a bone of its own at his feet that his dodges, rolls and other moves carry along,
// and their "camera lookat as rotate" triggers turn the camera toward it. Mason's (Guerrilla's) has only
// "camera", on his root at his hips, so his entry holds -1, and the camera (0x4CFAD8, 0x4CFC82) asks
// the skeleton where bone -1 is (0xD8AE50) and goes there: under the ground. The mod gives such an
// entry its "camera" bone instead, so the camera turns toward his hips through the move.
constexpr DWORD kCameraBone = 0x1E0, kCameraLookatBone = 0x1E4;
bool g_lookatLogged;

void GiveCameraLookat(BYTE* entry) {
    int& lookat = *reinterpret_cast<int*>(entry + kCameraLookatBone);
    const int camera = *reinterpret_cast<const int*>(entry + kCameraBone);
    if (lookat >= 0 || camera < 0) return;
    lookat = camera;
    if (!g_lookatLogged) {
        g_lookatLogged = true;
        rfa::Log("Camera: the player's skeleton has no camera-lookat bone; his camera bone (%d) stands in for it", camera);
    }
}

// ---- Why the player turns standing still (a diagnostic: log only, nothing changes)
//
// Standing still the mod zeroes the heading step's turn, yet Mason still turned with the camera (the
// user, 2026-11-12). The rest of a human's turning is the movement step's (0x89D0C0, cdecl: the human;
// once a frame): it points him along a direction it picks (his steering, human+0x62C, rotated by the
// heading step's turn; the camera's own way in camera mode 8, 0x11B9C40) and either sets his facing to
// it or turns him through the stand-turn step (0x89BAD0, cdecl: the human, an angle), which adds the
// playing animation's own turn (0xD8B700) to the angle. Both are wrapped to measure his heading before
// and after while he stands still, and the heading step measures what turned him anywhere else.
constexpr DWORD kMovementStep = 0x89D0C0;
constexpr size_t kMovementStepPrologue = 9;  // push ebp / mov ebp,esp / sub esp,260h
constexpr DWORD kStandTurn = 0x89BAD0;
constexpr size_t kStandTurnPrologue = 6;     // push ebp / mov ebp,esp / sub esp,5Ch
using MovementStepFn = void(__cdecl*)(BYTE* human);
using StandTurnFn = void(__cdecl*)(BYTE* human, float angle);
MovementStepFn g_movementStep;
StandTurnFn g_standTurn;
const int* const kCameraMode = reinterpret_cast<const int*>(0x11B9C40);
constexpr DWORD kSteering = 0x62C;           // human: the way he is steered
constexpr DWORD kForward = 0x28;             // human: his facing (the orientation's third row)
constexpr float kRadiansToDegrees = 57.2957795f;
constexpr float kNoticeableTurn = 0.004f;    // about a quarter of a degree
constexpr int kMaxStillTurnLines = 24;

bool g_watchingStill;          // the player stands still this frame
float g_headingAsked;          // the heading step's turn this frame, before the mod zeroes it
bool g_inMovementStep;
float g_standTurned, g_standAsked, g_movementTurned;
float g_lastStillYaw;
bool g_haveStillYaw;
int g_stillTurnLines;
ULONGLONG g_nextStillTurnLog;
const char* g_lastFacingWhy;
int g_facingWhyLines;

float Yaw(const BYTE* human) {
    const float* forward = reinterpret_cast<const float*>(human + kForward);
    return std::atan2(forward[0], forward[2]);
}

float Wrapped(float angle) {
    while (angle > 3.14159265f) angle -= 6.28318531f;
    while (angle < -3.14159265f) angle += 6.28318531f;
    return angle;
}

bool StillTurnLogDue() {
    const ULONGLONG now = GetTickCount64();
    if (g_stillTurnLines >= kMaxStillTurnLines || now < g_nextStillTurnLog) return false;
    g_nextStillTurnLog = now + 400;
    ++g_stillTurnLines;
    return true;
}

void __cdecl StandTurn(BYTE* human, float angle) {
    if (!g_inMovementStep || !human) {
        g_standTurn(human, angle);
        return;
    }
    const float before = Yaw(human);
    g_standTurn(human, angle);
    g_standTurned += Wrapped(Yaw(human) - before);
    g_standAsked += angle;
}

void __cdecl MovementStep(BYTE* human) {
    if (!g_watchingStill || !human || human != LocalPlayer()) {
        g_movementStep(human);
        return;
    }
    const float before = Yaw(human);
    g_standTurned = g_standAsked = 0.0f;
    g_inMovementStep = true;
    g_movementStep(human);
    g_inMovementStep = false;
    const float turned = Wrapped(Yaw(human) - before);
    g_movementTurned += turned;
    if (std::fabs(turned) < kNoticeableTurn || !StillTurnLogDue()) return;
    const float* steering = reinterpret_cast<const float*>(human + kSteering);
    rfa::LogDetail("Still turn: %s turned %.1f degrees in the movement step standing still: %.1f in the stand-turn step "
             "(asked %.1f, so %.1f the animation's own turn), %.1f set by the movement step itself; the heading "
             "step asked %.1f (zeroed); steering (%.2f, %.2f, %.2f); camera mode %d; held: %s",
             PlaysSet(human, kMasonSet) ? "Mason" : "Darius", turned * kRadiansToDegrees,
             g_standTurned * kRadiansToDegrees, g_standAsked * kRadiansToDegrees,
             (g_standTurned - g_standAsked) * kRadiansToDegrees, (turned - g_standTurned) * kRadiansToDegrees,
             g_headingAsked * kRadiansToDegrees, steering[0], steering[1], steering[2], *kCameraMode,
             IsHeld(human) ? "yes" : "no");
}

// From the heading step, once a frame: says why he faces the camera when he stands without walking
// free (once for each reason), and what turned him since the last frame outside the movement step.
void WatchStillFacing(BYTE* player, Facing facing, const char* why) {
    if (!rfa::DetailLog()) return;  // a diagnosis, with Log=1
    const bool standing = facing != Facing::Walking;
    if (standing && why != g_lastFacingWhy && g_facingWhyLines < kMaxStillTurnLines) {
        g_lastFacingWhy = why;
        ++g_facingWhyLines;
        rfa::LogDetail("Still turn: standing, %s %s (%s)", PlaysSet(player, kMasonSet) ? "Mason" : "Darius",
                 facing == Facing::Still ? "keeps his heading" : "faces the camera", why);
    }
    const float yaw = Yaw(player);
    if (facing == Facing::Still && g_watchingStill && g_haveStillYaw) {
        const float elsewhere = Wrapped(yaw - g_lastStillYaw - g_movementTurned);
        if (std::fabs(elsewhere) >= kNoticeableTurn && StillTurnLogDue())
            rfa::LogDetail("Still turn: %s turned %.1f degrees standing still outside the movement step (camera mode %d)",
                     PlaysSet(player, kMasonSet) ? "Mason" : "Darius", elsewhere * kRadiansToDegrees, *kCameraMode);
    }
    g_watchingStill = facing == Facing::Still;
    g_lastStillYaw = yaw;
    g_haveStillYaw = true;
    g_movementTurned = 0.0f;
}

void WatchStillTurns() {
    g_movementStep = reinterpret_cast<MovementStepFn>(
        rfa::HookFunction(kMovementStep, kMovementStepPrologue, reinterpret_cast<void*>(&MovementStep)));
    g_standTurn = reinterpret_cast<StandTurnFn>(
        rfa::HookFunction(kStandTurn, kStandTurnPrologue, reinterpret_cast<void*>(&StandTurn)));
    if (!g_movementStep || !g_standTurn) rfa::Log("Still turn: could not wrap the movement step; no \"Still turn:\" lines");
}

// ---- Standing still, he looks ahead
//
// Guerrilla's player_do_frame sets Mason's look-at (human_set_look_at_target: his place + a direction x
// 10000) along the camera's forward (player +0x1250) only while he is combat ready (human_combat's
// combat_ready_timestamp, human+0x9D4, still running) in its third-person camera, else along his own
// forward; human_spinebend_pre_process bends his spine, neck and head toward it (through
// human_get_look_at_vector_from_eye_pos; determine_spinebend_filter: not combat ready, the "head" bones,
// which in his spinebend.xtbl entry are spine, spine1, spine2, neck and head). So idle he looks ahead.
// Armageddon's player looks where the camera aims: its spine bend step (in 0x89BC50, at 0x89BD65) and its
// heading step (0x84CBC6) take the look direction from 0x868700 (cdecl: the human, out: the direction
// from his eye to what he looks at), so standing still, his feet kept, his torso and head twisted to the
// camera's aim, up to about 145 degrees: behind him when the camera looked at his front. While he stands
// still (the heading step's "standing still": Mason or Darius, on foot, not moving, aiming, attacking or
// sprinting) that direction is his own forward (human+0x28), as Guerrilla's idle Mason's; aiming,
// attacking or moving, it is the game's as before.
constexpr DWORD kLookVector = 0x868700;
constexpr size_t kLookVectorPrologue = 6;  // push ebp / mov ebp,esp / sub esp,4Ch
using LookVectorFn = void(__cdecl*)(BYTE* human, float* out);
LookVectorFn g_lookVector;
const BYTE* g_lookAhead = nullptr;  // the player while the heading step finds him standing still
ULONGLONG g_lookAheadAt = 0;        // when it last did (a stale one, as in a vehicle, counts for nothing)
int g_lookAheadLogs = 0;

void __cdecl LookVector(BYTE* human, float* out) {
    g_lookVector(human, out);
    if (!human || !out || human != g_lookAhead || GetTickCount64() - g_lookAheadAt > 250) return;
    const float* forward = reinterpret_cast<const float*>(human + kForward);
    if (g_lookAheadLogs < 8) {
        const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
        const float along = length > 0.0f ? (out[0] * forward[0] + out[1] * forward[1] + out[2] * forward[2]) / length : 1.0f;
        const float degrees = std::acos((std::max)(-1.0f, (std::min)(1.0f, along))) * kRadiansToDegrees;
        if (degrees >= 30.0f) {
            ++g_lookAheadLogs;
            rfa::LogDetail("Look: %s standing still looks ahead, not %.0f degrees round to the camera's aim (Guerrilla: the camera's way only "
                     "while combat ready)", PlaysSet(human, kMasonSet) ? "Mason" : "Darius", degrees);
        }
    }
    out[0] = forward[0];
    out[1] = forward[1];
    out[2] = forward[2];
}

void LookAheadStill() {
    g_lookVector = reinterpret_cast<LookVectorFn>(rfa::HookFunction(kLookVector, kLookVectorPrologue, reinterpret_cast<void*>(&LookVector)));
    if (!g_lookVector) rfa::Log("Look: could not wrap the look direction; standing still, his torso and head still turn to the camera");
    else rfa::LogDetail("Look: standing still, Mason and Darius look ahead (Guerrilla's: the camera's way only while combat ready)");
}

void KeepHammer(BYTE* player);  // (Mason's sledgehammer, below)
void KeepSprint();              // (Guerrilla's sprint, below)
void AfterCarriedThrow(BYTE* player);  // (a carried thing thrown, below)
void AfterCarriedPickup(BYTE* player); // (and picked up)
void LogItemPreload();          // (room for the preloaded items, below)
void LogNameRoom();             // (room in the tables' name pools, below)
bool g_itemPreloadLogged = false;

void __cdecl HeadingStep(BYTE* human) {
    BYTE* player = LocalPlayer();
    if (!human || human != player) {
        g_headingStep(human);
        return;
    }
    if (!g_itemPreloadLogged) {  // the first step in a level: every preloaded item is in by now
        g_itemPreloadLogged = true;
        LogItemPreload();
        LogNameRoom();
    }
    AfterCarriedThrow(player);
    AfterCarriedPickup(player);
    KeepSprint();
    upgrades::Step(player);
    backpacks::Step(player);
    KeepAmmoCapacity();
    KeepHammer(player);
    LogHolds(player);
    const char* why = nullptr;
    const Facing facing = PlayerFacing(player, &why);
    WatchStillFacing(player, facing, why);
    g_lookAhead = facing == Facing::Still ? player : nullptr;  // his look direction (LookVector, above)
    g_lookAheadAt = GetTickCount64();
    const bool walking = facing == Facing::Walking;
    const bool free = facing != Facing::Camera;  // walking free or standing still
    BYTE* entry = *reinterpret_cast<BYTE**>(player + kCharacterEntry);
    if (entry) GiveCameraLookat(entry);
    // Without the flag the movement step (0x89D0C0) turns him toward where he goes, as it turns
    // Guerrilla's people (and as Guerrilla turned Mason), and standing still nothing turns him; with it,
    // walking he keeps his heading, and standing (entry+0x290 == 1, the player's) it sets his facing to
    // his target, where the camera aims (0x89D58C, 0x868910). The flag goes back the moment he faces the
    // camera again (or another character is the player).
    if (g_liftedEntry && (!free || entry != g_liftedEntry)) {
        g_liftedEntry[kEntryFlags] |= kAlwaysFaceTarget;
        g_liftedEntry = nullptr;
    }
    if (free && entry && (entry[kEntryFlags] & kAlwaysFaceTarget)) {
        entry[kEntryFlags] &= static_cast<BYTE>(~kAlwaysFaceTarget);
        g_liftedEntry = entry;
    }
    g_headingStep(player);
    g_headingAsked = *reinterpret_cast<const float*>(player + kHeadingOffset);
    // The heading step turns the player to the camera unless he sprints; walking free or standing
    // still, he doesn't either.
    if (free) *reinterpret_cast<float*>(player + kHeadingOffset) = 0.0f;
    if (walking) {
        const char* who = PlaysSet(player, kMasonSet) ? "Mason" : "Darius";
        if (!g_walkLogged || std::strcmp(g_walkLogged, who) != 0) {
            g_walkLogged = who;
            rfa::LogDetail("Movement: %s turns to where he walks and runs (aiming or attacking, to the camera)", who);
        }
    } else if (facing == Facing::Still) {
        const char* who = PlaysSet(player, kMasonSet) ? "Mason" : "Darius";
        if (!g_stillLogged || std::strcmp(g_stillLogged, who) != 0) {
            g_stillLogged = who;
            rfa::LogDetail("Movement: %s standing still doesn't turn with the camera", who);
        }
    }
}

// Guerrilla's sprint: no limit. Guerrilla's control_gameplay_process (RM 0x701772) clears the player's sprinting flag
// (+0xDCC, 0x40) each frame and sets it again while the sprint button is held, unless an action stops it: no meter, no
// cooldown (its two sprint timestamps, +0x24B8 and +0x24BC, only hold off firing after a sprint and mark its start;
// Sprint_duration is a stat). Armageddon runs a meter (player +0x27D8, 1 full; 0x8B68F0): sprinting takes
// Player_sprint_decay_rate (0.1) a second, so 10 s, and at 0 he stops; it refills at Player_sprint_regen_rate (0.5)
// a second after Player_sprint_regen_delay_ms (2000). Its own Player_unlimited_sprint (a byte, 0x22714C1; the console
// command unlimited_sprint) keeps the meter full. Both top out at Player_max_sprint_speed 8. The user: "turn on the
// infinite sprinting as it was intended as this is a open world they only limited the sprinting in RFA due to it
// being linear". So in the Guerrilla worlds ([GuerrillaWorld] Worlds=, rfg) the switch is on; elsewhere it is what it
// was (the console's, or off), so Armageddon's levels keep their limit.
constexpr DWORD kUnlimitedSprint = 0x22714C1;
std::vector<std::string> g_sprintWorlds;
int g_sprintInOurWorld = -1;   // last step's answer (-1 before the first)
BYTE g_sprintSwitchWas = 0;    // the switch as it was before our world turned it on

void ReadSprintWorlds() {
    std::string current;
    for (wchar_t c : rfa::IniString(L"GuerrillaWorld", L"Worlds", L"rfg") + L",") {
        if (c == L',' || c == L' ' || c == L'\t') {
            if (!current.empty()) g_sprintWorlds.push_back(current);
            current.clear();
        } else {
            current += static_cast<char>(c);
        }
    }
}

void KeepSprint() {
    const char* world = reinterpret_cast<const char*>(kWorld) + 0x29EA;  // the world's name: "rfg", "world", ...
    bool ours = false;
    for (const std::string& name : g_sprintWorlds)
        if (_stricmp(world, name.c_str()) == 0) ours = true;
    BYTE& unlimited = *reinterpret_cast<BYTE*>(kUnlimitedSprint);
    if (static_cast<int>(ours) != g_sprintInOurWorld) {
        if (ours) {
            g_sprintSwitchWas = unlimited;
            rfa::LogDetail("Sprint: world %s: as long as he likes, as Guerrilla's (Armageddon's limit off)", world);
        } else if (g_sprintInOurWorld == 1) {
            unlimited = g_sprintSwitchWas;
            rfa::LogDetail("Sprint: world %s: Armageddon's limit %s", world, unlimited ? "off (as it was set)" : "back");
        }
        g_sprintInOurWorld = ours;
    }
    if (ours) unlimited = 1;
}

void PlayerMovement() {
    ReadSprintWorlds();
    g_headingStep = reinterpret_cast<HeadingStepFn>(
        rfa::HookFunction(kHeadingStep, kHeadingStepPrologue, reinterpret_cast<void*>(&HeadingStep)));
    if (!g_headingStep) rfa::Log("Movement: could not wrap the heading step; the player faces the camera as Armageddon has it");
    else rfa::LogDetail("Movement: Darius and Mason turn to face where they walk and run, through all 360 degrees; aiming and attacking face the camera");
}

// ---- Mason's sledgehammer (see the top)

constexpr DWORD kWeaponTable = 0x18F8048;      // the weapon entries (0xE0 bytes each, +0 the name)
constexpr DWORD kWeaponTableCount = 0x18F804C;
constexpr DWORD kWeaponEntrySize = 0xE0;
constexpr int kHammerSlot = 3;                 // Guerrilla's: the hammer's slot
constexpr int kRadialSlots = 4;
constexpr ULONGLONG kHammerCheckMs = 2000;

constexpr DWORD kSwapWeapon = 0x8BEB00;
constexpr size_t kSwapWeaponPrologue = 6;      // push ebp / mov ebp,esp / sub esp,24h
using SwapWeaponFn = void(__cdecl*)(BYTE* human, BYTE* item, int slot);
SwapWeaponFn g_swapWeapon;

// The game's add-by-inventory-item (cdecl: the human, the inventory item, rounds (0: its own), the slot
// (-1: the first free, as give_player_item asks), three flags), the record in a slot (cdecl: the human,
// the slot) and the one in his hands (cdecl: the human). A record's +0x18 is its weapon's handle.
auto const GiveByInventoryItem = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human, const void* inventoryItem, int rounds, int slot, bool, bool, bool)>(0x85B260);
auto const RecordInSlot = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human, int slot)>(0x7A8290);
auto const RecordInHands = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x8BE980);

// Guerrilla's hammers, as the weapons build names them: the sledgehammer and its upgrades (Stonebreaker,
// Shattermaster), the first the one Mason is given.
const char* const kHammers[] = {"rfg_sledgehammer", "sledge_upgrade_1", "sledge_upgrade_2"};

ULONGLONG g_nextHammerCheck;
ULONGLONG g_lastHammerNote;

const BYTE* WeaponEntryNamed(const char* name) {
    BYTE* entries = *reinterpret_cast<BYTE* const*>(kWeaponTable);
    const int count = *reinterpret_cast<const int*>(kWeaponTableCount);
    for (int i = 0; entries && i < count; ++i) {
        const char* own = *reinterpret_cast<const char* const*>(entries + i * kWeaponEntrySize);
        if (own && _stricmp(own, name) == 0) return entries + i * kWeaponEntrySize;
    }
    return nullptr;
}

bool IsHammer(const BYTE* info) {
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    if (!name) return false;
    for (const char* hammer : kHammers)
        if (_stricmp(name, hammer) == 0) return true;
    return false;
}

// The weapon entry of an inventory record, or null.
const BYTE* RecordWeapon(const BYTE* record) {
    if (!record) return nullptr;
    const BYTE* weapon = ObjectFromHandle(kWorld, nullptr, *reinterpret_cast<const DWORD*>(record + kInventoryInfo));
    return weapon ? *reinterpret_cast<const BYTE* const*>(weapon + kWeaponInfo) : nullptr;
}

auto const DropRecord = reinterpret_cast<void(__cdecl*)(BYTE* human, BYTE* record)>(0x8A2DB0);

// Mason always carries his hammer (Guerrilla's start kit, character.xtbl rfg_PC: the sledgehammer in
// slot 3): every two seconds on foot, the player playing Mason's set who has none of the three is given
// the best he has bought (Guerrilla's upgrades: Stonebreaker, Shattermaster; else the plain one), into
// slot 3 if it's free, else the first free one. One he carries that is lesser than that is dropped first.
// Not while an activity has put its own weapons in (guerrilla_campaign's GuerrillaCampaign_KitLocked):
// a demolitions master challenge gives the hammer only if its table does, and its plain one stays.
void KeepHammer(BYTE* player) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_nextHammerCheck) return;
    g_nextHammerCheck = now + kHammerCheckMs;
    if (!PlaysSet(player, kMasonSet)) return;
    if (GuerrillaCampaign_KitLocked()) return;
    // The hammers in order (plain, Stonebreaker, Shattermaster), and the one he should have: the best bought.
    const std::string bought = upgrades::BestHammer();
    constexpr int kCount = static_cast<int>(sizeof(kHammers) / sizeof(kHammers[0]));
    const void* items[kCount] = {};
    int best = -1;
    for (int i = 0; i < kCount; ++i) {
        const BYTE* info = WeaponEntryNamed(kHammers[i]);
        items[i] = info ? *reinterpret_cast<const void* const*>(info + kInventoryInfo) : nullptr;
        if (items[i] && (best < 0 || _stricmp(kHammers[i], bought.c_str()) == 0)) best = i;
    }
    if (best < 0) return;  // Guerrilla's hammer isn't built
    const void* give = items[best];
    BYTE* lesser = nullptr;
    for (int i = 0; i < kCount; ++i) {
        BYTE* record = items[i] ? HasItem(player, items[i]) : nullptr;
        if (!record) continue;
        if (i >= best) return;  // he has that one, or a better
        lesser = record;
    }
    if (lesser) {
        DropRecord(player, lesser);
        rfa::LogDetail("Hammer: dropped the lesser hammer for %s (bought)", bought.c_str());
    }
    int slot = RecordInSlot(player, kHammerSlot) ? -1 : kHammerSlot;
    for (int s = 0; slot < 0 && s < kRadialSlots; ++s)
        if (!RecordInSlot(player, s)) slot = s;
    if (slot < 0) {
        if (now - g_lastHammerNote > 60000) {
            g_lastHammerNote = now;
            rfa::LogDetail("Hammer: Mason has no sledgehammer and no free slot for it (four guns); it waits for a free slot");
        }
        return;
    }
    const BYTE* given = GiveByInventoryItem(player, give, 0, slot, false, false, false);
    rfa::LogDetail("Hammer: Mason %s; given %s %s, slot %d", lesser ? "had a lesser hammer" : "had no sledgehammer", given ? "him" : "nothing (the game refused)",
             kHammers[best], slot);
}

// ---- A carried thing thrown (see the top)

constexpr DWORD kLaunchThrown = 0x7323C0;
constexpr size_t kLaunchThrownPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0F8h
using LaunchThrownFn = BYTE*(__fastcall*)(BYTE* weapon, void* unused, const float* position, const float* orientation,
                                          const float* velocity, bool placed);
LaunchThrownFn g_launchThrown;

constexpr DWORD kWeaponFlagsB = 0xA;          // a weapon entry's second byte of flags
constexpr BYTE kNonInventory = 0x10;          // "non-inventory": carried in both hands, in no slot
constexpr ULONGLONG kHandsWatchMs = 5000;     // how long after a carried throw his hands are watched
constexpr ULONGLONG kHandsSettleMs = 300;     // the game's own equip gets this long first, and between tries
// The record in a hand (0: the main one) and the one on its way there (thiscall: the human, the hand),
// and the player's equip (cdecl: the player, the record or null, the hand, two flags).
auto const RecordEquipped = reinterpret_cast<BYTE*(__fastcall*)(BYTE* human, void* unused, int hand)>(0x84E840);
auto const RecordWanted = reinterpret_cast<BYTE*(__fastcall*)(BYTE* human, void* unused, int hand)>(0x84E860);
auto const PlayerEquip = reinterpret_cast<bool(__cdecl*)(BYTE* player, BYTE* record, int hand, bool first, bool second)>(0x8BB420);

ULONGLONG g_carriedThrownAt;  // when the player last threw a carried weapon (0: his hands aren't watched)
ULONGLONG g_nextHandsCheck;

const char* RecordName(const BYTE* record) {
    const char* name = WeaponName(RecordWeapon(record));
    return name ? name : (record ? "a weapon that is gone" : "nothing");
}

BYTE* __fastcall LaunchThrown(BYTE* weapon, void* unused, const float* position, const float* orientation,
                              const float* velocity, bool placed) {
    BYTE* info = weapon ? *reinterpret_cast<BYTE**>(weapon + kWeaponInfo) : nullptr;
    BYTE* player = LocalPlayer();
    const BYTE* inHand = player && info && (info[kWeaponFlagsB] & kNonInventory) && IsGuerrillaGun(info) ? RecordEquipped(player, nullptr, 0) : nullptr;
    const bool carried = inHand && ObjectFromHandle(kWorld, nullptr, *reinterpret_cast<const DWORD*>(inHand + kInventoryInfo)) == weapon;
    // Guerrilla's launch_thrown_weapon (RM 0xBE79A0) throws a carried weapon itself, as the game's own
    // non-inventory launch does: no projectile, the tank flies with its physics on, bounces, and only its
    // damage (shot, blown up) sets it off.
    BYTE* result = g_launchThrown(weapon, unused, position, orientation, velocity, placed);
    if (carried) {
        rfa::LogDetail("Thrown: %s thrown as Guerrilla's (the tank itself; it goes off only when shot or blown up); in his hands now: %s; on its way: %s",
                 WeaponName(info), RecordName(RecordEquipped(player, nullptr, 0)), RecordName(RecordWanted(player, nullptr, 0)));
        g_carriedThrownAt = GetTickCount64();
        g_nextHandsCheck = g_carriedThrownAt + kHandsSettleMs;
    }
    return result;
}

// From the player's heading step, for a few seconds after he threw what he carried: if the game's own
// equip left his hands empty with nothing on its way (no last weapon to go back to), he takes his
// sledgehammer, as Guerrilla has it (human_delete_item_from_inventory: player_equip_sledgehammer), or
// else the first weapon in his slots. Armageddon's player code takes a weapon in the hands for granted.
void AfterCarriedThrow(BYTE* player) {
    if (!g_carriedThrownAt) return;
    const ULONGLONG now = GetTickCount64();
    if (now < g_nextHandsCheck) return;
    BYTE* held = RecordEquipped(player, nullptr, 0);
    BYTE* wanted = RecordWanted(player, nullptr, 0);
    if (held || wanted) {
        rfa::LogDetail("Thrown: after the throw he holds %s (on its way: %s)", RecordName(held), RecordName(wanted));
        g_carriedThrownAt = 0;
        return;
    }
    if (now - g_carriedThrownAt > kHandsWatchMs) {
        rfa::Log("Thrown: his hands are still empty %llu ms after the throw; no weapon of his could be equipped", now - g_carriedThrownAt);
        g_carriedThrownAt = 0;
        return;
    }
    g_nextHandsCheck = now + kHandsSettleMs;
    BYTE* record = RecordInSlot(player, kHammerSlot);
    for (int slot = 0; !record && slot < kRadialSlots; ++slot) record = RecordInSlot(player, slot);
    if (!record) return;
    const bool taken = PlayerEquip(player, record, 0, false, false);
    rfa::LogDetail("Thrown: his hands were empty after the throw; %s %s", RecordName(record), taken ? "equipped" : "not equipped (the game refused; trying again)");
}

// Setting a carried thing down (Guerrilla's player_set_down_non_inventory, RM 0xABE780: when he equips another
// weapon, human_equip_inventory_item, or swaps, player_do_weapon_swap). Guerrilla plays no animation: it stops the
// equip (ANIM_ALIAS_EQUIP) if it plays, takes the item out of his hands (player_remove_item_from_hands) and puts it
// 0.3 m before where it is, along his facing, as it lies (its function +0x30 with true): a world object again,
// falling and rolling as any. Armageddon's set-down (0x8BA7C0, cdecl: the human; from the controls 0x4E1478, the
// equip 0x85D287 and the swap 0x8BEC11/0x8BECDF) plays a put-down animation (the "pickup" the user saw when
// changing to an empty slot) and leaves the thing placed where it floats. For the player with one of Guerrilla's
// non-inventory weapons in his hands (its propane tank) the mod does Guerrilla's: the game's own let-go of a carried
// weapon (its launch, 0x7323C0, as it lets a non-inventory weapon fly: out of his hands and the inventory, his last
// weapon back) at that place, with no speed and not placed, so it drops where he stood.
constexpr DWORD kSetDown = 0x8BA7C0;
constexpr size_t kSetDownPrologue = 6;  // push ebp / mov ebp,esp / sub esp,38h
using SetDownFn = bool(__cdecl*)(BYTE* human);
SetDownFn g_setDown;
constexpr float kSetDownAhead = 0.3f;  // Guerrilla's: 0.3 x his facing

bool __cdecl SetDown(BYTE* human) {
    BYTE* player = LocalPlayer();
    BYTE* record = human && human == player ? RecordEquipped(player, nullptr, 0) : nullptr;
    BYTE* weapon = record ? ObjectFromHandle(kWorld, nullptr, *reinterpret_cast<const DWORD*>(record + kInventoryInfo)) : nullptr;
    BYTE* info = weapon ? *reinterpret_cast<BYTE**>(weapon + kWeaponInfo) : nullptr;
    if (!info || !(info[kWeaponFlagsB] & kNonInventory) || !IsGuerrillaGun(info) || !g_launchThrown) return g_setDown(human);
    const float* at = reinterpret_cast<const float*>(weapon + 0x4);
    const float* facing = reinterpret_cast<const float*>(player + kForward);
    const float place[3] = {at[0] + facing[0] * kSetDownAhead, at[1] + facing[1] * kSetDownAhead, at[2] + facing[2] * kSetDownAhead};
    const float still[3] = {0.0f, 0.0f, 0.0f};
    g_launchThrown(weapon, nullptr, place, reinterpret_cast<const float*>(weapon + 0x10), still, false);
    rfa::LogDetail("Thrown: %s set down as Guerrilla's (no animation; in his hands now: %s)", WeaponName(info), RecordName(RecordEquipped(player, nullptr, 0)));
    return true;
}

// Picking a carried thing up (Armageddon's 0x8BA2F0, cdecl: the player, and whether to take it or only ask; the use
// key's 0x8BA100 sends a non-inventory weapon there): what he aims at (player +0x1540) comes off its parent, its
// physics body is told to ignore him, it goes into his inventory (0x85B3E0 with slot -1: the first free selection
// slot, as Guerrilla's human_find_open_inventory_selection_slot gives its tank) and is equipped (0x85CEE0), as
// Guerrilla's player_pickup_non_inventory (RM 0xAC2720). The user's 12:08 run of 2026-11-19: the first tank he took
// went into his weapon select with something box-like at his hands until he changed weapons and took it again;
// then it was right. The code alone doesn't say why, so each pickup writes what it found and what it left: the thing's
// item flags (+0x108: bit 0 respawn, which makes the inventory take a copy and leave the thing where it lies, 0x85B7CD;
// 4 held; 6 preplaced), its parent (+0x6C) and physics body (+0x4C) before; after, the record that holds the tank,
// its selection slot (+0x10) and whether its weapon is the very thing he took.
constexpr DWORD kPickUpCarried = 0x8BA2F0;
constexpr size_t kPickUpCarriedPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0B0h
using PickUpCarriedFn = bool(__cdecl*)(BYTE* player, bool take);
PickUpCarriedFn g_pickUpCarried;

constexpr DWORD kUseTarget = 0x1540;   // the player -> the handle of what the use key acts on
constexpr DWORD kItemFlags = 0x108;    // an item's flags (obj_item_flags)
constexpr DWORD kParent = 0x6C;        // an object's parent's handle (-1: none)
constexpr DWORD kPhysicsBody = 0x4C;   // an object's physics body (-1: none)
constexpr DWORD kRecordSlot = 0x10;    // an inventory record's selection slot (-1: none)
constexpr int kWeaponKind = 9;         // an object's subtype (+0x7B) for a weapon
auto const CarriedObjectAt = reinterpret_cast<BYTE*(__thiscall*)(void* world, DWORD handle)>(0x6CB9E0);  // any kind
auto const CarriedObjectKind = reinterpret_cast<int(__thiscall*)(const BYTE* object)>(0x423530);
auto const CarriedHandleOf = reinterpret_cast<DWORD(__thiscall*)(const BYTE* object)>(0x424850);

DWORD Field(const BYTE* object, DWORD offset) { return *reinterpret_cast<const DWORD*>(object + offset); }

// After the pickup. Guerrilla's ends with human_equip_inventory_item: the tank is in his hands. In the user's 12:40
// run of 2026-11-19 one pickup of three (12:40:51, the one straight after a throw) never got there: the game had the
// tank on its way to his hands (the line above), his hammer stayed in them, the tank sat in his weapon select (slot 1)
// until he chose it there, and changing weapons didn't set it down (the set-down goes by what is in his hands). The
// others had it in his hands in 0.4 s. Why isn't known yet, so for a few seconds after each pickup the heading step
// looks: the tank in his hands is logged with the time it took; still out of them after kPickupSettleMs with the
// weapon he had before still in his hands, the game's equip is asked again, once, as the pickup asks it (0x85CEE0:
// the human, the record, hand 0, at once, false); with another weapon there (he chose one) it is left as it is.
// Either way the line says what is in his hands, what is on its way, and where the tank is.
constexpr ULONGLONG kPickupSettleMs = 1500;  // the game's own equip takes about 0.4 s
constexpr DWORD kPosition = 0x4;             // an object's position
auto const EquipRecord = reinterpret_cast<void(__cdecl*)(BYTE* human, BYTE* record, int hand, bool now, bool second)>(0x85CEE0);

struct CarriedPickup {
    DWORD tank = 0xFFFFFFFF;        // the tank's handle
    const BYTE* before = nullptr;   // the record in his hands when he took it
    ULONGLONG at = 0, askedAt = 0;  // when he took it (0: not watched); when its equip was asked again
};
CarriedPickup g_carriedPickup;

void AfterCarriedPickup(BYTE* player) {
    CarriedPickup& p = g_carriedPickup;
    if (!p.at) return;
    const ULONGLONG now = GetTickCount64();
    BYTE* tank = CarriedObjectAt(kWorld, p.tank);
    const BYTE* info = tank && CarriedObjectKind(tank) == kWeaponKind ? *reinterpret_cast<const BYTE* const*>(tank + kWeaponInfo) : nullptr;
    BYTE* record = info ? HasItem(player, *reinterpret_cast<const void* const*>(info + kInventoryInfo)) : nullptr;
    if (!record || Field(record, kInventoryInfo) != p.tank) {  // thrown, set down or gone
        p.at = 0;
        return;
    }
    BYTE* held = RecordEquipped(player, nullptr, 0);
    BYTE* wanted = RecordWanted(player, nullptr, 0);
    if (held == record) {
        rfa::LogDetail("Thrown: %s in his hands %llu ms after he took it%s", WeaponName(info), now - p.at, p.askedAt ? " (its equip asked again)" : "");
        p.at = 0;
        return;
    }
    if (now - (p.askedAt ? p.askedAt : p.at) < kPickupSettleMs) return;
    const float* at = reinterpret_cast<const float*>(tank + kPosition);
    const float* him = reinterpret_cast<const float*>(player + kPosition);
    const float dx = at[0] - him[0], dy = at[1] - him[1], dz = at[2] - him[2];
    const bool askAgain = !p.askedAt && held == p.before;
    rfa::Log("Thrown: %s not in his hands %llu ms after he took it: in them %s (%s), on its way %s; the tank's parent %08X, item flags %08X, "
             "%.1f m from him (%.1f up), selection slot %d; %s",
             WeaponName(info), now - p.at, RecordName(held), held == p.before ? "what he had before" : "another weapon", RecordName(wanted),
             Field(tank, kParent), Field(tank, kItemFlags), std::sqrt(dx * dx + dy * dy + dz * dz), dy, static_cast<int>(Field(record, kRecordSlot)),
             askAgain ? "its equip asked again" : p.askedAt ? "the second ask didn't take either; left as it is" : "he chose that weapon; left as it is");
    if (askAgain) {
        EquipRecord(player, record, 0, true, false);
        p.askedAt = now;
        return;
    }
    p.at = 0;
}

bool __cdecl PickUpCarried(BYTE* player, bool take) {
    BYTE* target = take && player && player == LocalPlayer() ? CarriedObjectAt(kWorld, Field(player, kUseTarget)) : nullptr;
    BYTE* weapon = target && CarriedObjectKind(target) == kWeaponKind ? target : nullptr;
    const BYTE* info = weapon ? *reinterpret_cast<const BYTE* const*>(weapon + kWeaponInfo) : nullptr;
    if (!info || !IsGuerrillaGun(info)) return g_pickUpCarried(player, take);
    const DWORD handle = CarriedHandleOf(weapon), flags = Field(weapon, kItemFlags);
    const DWORD parent = Field(weapon, kParent), body = Field(weapon, kPhysicsBody);
    const BYTE* before = RecordEquipped(player, nullptr, 0);
    const bool taken = g_pickUpCarried(player, take);
    const BYTE* record = HasItem(player, *reinterpret_cast<const void* const*>(info + kInventoryInfo));
    if (taken && record && Field(record, kInventoryInfo) == handle) g_carriedPickup = {handle, before, GetTickCount64(), 0};
    const int slot = record ? static_cast<int>(Field(record, kRecordSlot)) : -2;
    const DWORD holds = record ? Field(record, kInventoryInfo) : 0xFFFFFFFF;
    rfa::LogDetail("Thrown: %s %s (handle %08X; item flags %08X, parent %08X, body %08X before; item flags %08X, parent %08X after); "
             "its record: %s, selection slot %d, %s; in his hands: %s (on its way: %s)",
             WeaponName(info), taken ? "picked up" : "not picked up", handle, flags, parent, body, Field(weapon, kItemFlags), Field(weapon, kParent),
             record ? "there" : "none", slot, !record ? "-" : holds == handle ? "the thing he took" : "another object (a copy)",
             RecordName(RecordEquipped(player, nullptr, 0)), RecordName(RecordWanted(player, nullptr, 0)));
    return taken;
}

void ThrownCarried() {
    g_launchThrown = reinterpret_cast<LaunchThrownFn>(
        rfa::HookFunction(kLaunchThrown, kLaunchThrownPrologue, reinterpret_cast<void*>(&LaunchThrown)));
    if (!g_launchThrown) rfa::Log("Thrown: could not wrap the weapons' launch; a thrown tank stays in the hands");
    else rfa::LogDetail("Thrown: a carried weapon (Guerrilla's propane tank) is thrown itself, as Guerrilla's: it bounces, and goes off only when shot or blown up");
    g_setDown = reinterpret_cast<SetDownFn>(rfa::HookFunction(kSetDown, kSetDownPrologue, reinterpret_cast<void*>(&SetDown)));
    if (!g_setDown) rfa::Log("Thrown: could not wrap the set-down; a carried tank is put down Armageddon's way");
    else rfa::LogDetail("Thrown: a carried tank set down (another weapon, a swap) leaves his hands as Guerrilla's: no animation, it drops");
    g_pickUpCarried = reinterpret_cast<PickUpCarriedFn>(rfa::HookFunction(kPickUpCarried, kPickUpCarriedPrologue, reinterpret_cast<void*>(&PickUpCarried)));
    if (!g_pickUpCarried) rfa::Log("Thrown: could not wrap the pickup of a carried thing; what a tank's pickup does isn't written here");
}

// ---- Materials the game draws red (see the top)

constexpr DWORD kBindShader = 0xD4D7D0;
constexpr size_t kBindShaderPrologue = 6;  // push ebp / mov ebp,esp / sub esp,10h
using BindShaderFn = bool(__fastcall*)(void* library, void* unused, DWORD shader, DWORD second, DWORD third);
BindShaderFn g_bindShader;
auto const Renderer = reinterpret_cast<void*(__cdecl*)()>(0x4C1730);
auto const MeshKind = reinterpret_cast<int(__fastcall*)(void* renderer, void* unused)>(0xD4E060);
auto const MeshVariant = reinterpret_cast<int(__fastcall*)(void* renderer, void* unused)>(0xD4E0B0);
auto const ShaderHasVersion = reinterpret_cast<bool(__fastcall*)(void* library, void* unused, DWORD shader, int kind, int variant)>(0xD4D650);
auto const ShaderNamed = reinterpret_cast<DWORD(__fastcall*)(void* library, void* unused, const char* name)>(0xD4D5C0);
constexpr DWORD kNoShader = 0xFFFFFFFF;
constexpr size_t kRedLines = 48;  // no more lines than this a run

std::mutex g_redLock;
std::vector<unsigned long long> g_redSeen;                // the shader, kind and variant of each line logged
std::vector<std::pair<std::string, DWORD>> g_redShaders;  // Guerrilla's shaders by name, and their numbers in the library
bool g_redShadersRead;

// The ending of a shader's file for a kind of mesh and its variant, as the game's own table has them (0x11234D8).
const char* ShaderEnding(int kind, int variant) {
    static const char* const endings[4][4] = {
        {"(no ending)", "?", "?", "?"}, {"_s", "_bs", "_ms", "_bms"}, {"_c", "_bc", "_mc", "_bmc"}, {"_t", "?", "_ts", "?"}};
    return kind >= 0 && kind < 4 && variant >= 0 && variant < 4 ? endings[kind][variant] : "?";
}

// Guerrilla's shaders' names (the gw_ effect files in overrides\build\pc\cache\, without their endings) and numbers.
void ReadRedShaders(void* library) {
    g_redShadersRead = true;
    WIN32_FIND_DATAW found;
    const std::wstring pattern = rfa::RootDir() + L"\\overrides\\build\\pc\\cache\\gw_*.fxo_pc_dx11";
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        std::string name;
        for (const wchar_t* c = found.cFileName; *c && *c != L'.'; ++c) name += static_cast<char>(*c);
        const size_t ending = name.rfind('_');
        if (ending == std::string::npos || ending == 0) continue;
        name.resize(ending);
        bool known = false;
        for (const auto& shader : g_redShaders) known = known || shader.first == name;
        if (known) continue;
        const DWORD number = ShaderNamed(library, nullptr, name.c_str());
        if (number != kNoShader) g_redShaders.emplace_back(name, number);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

void NoteRed(void* library, DWORD shader, int kind, int variant) {
    const unsigned long long key = (static_cast<unsigned long long>(shader) << 8) | (static_cast<unsigned>(kind) << 4) | static_cast<unsigned>(variant);
    std::lock_guard<std::mutex> lock(g_redLock);
    if (g_redSeen.size() >= kRedLines || std::find(g_redSeen.begin(), g_redSeen.end(), key) != g_redSeen.end()) return;
    g_redSeen.push_back(key);
    if (!g_redShadersRead) ReadRedShaders(library);
    if (shader == kNoShader) {
        rfa::Log("Red: a material with no shader at all is drawn (its mesh's kind %s): its shader's name wasn't in the library when it loaded",
                 ShaderEnding(kind, variant));
        return;
    }
    const char* name = nullptr;
    for (const auto& known : g_redShaders)
        if (known.second == shader) name = known.first.c_str();
    if (name) rfa::LogDetail("Red: a material on %s is drawn as a mesh of the kind %s, and there is no %s%s", name, ShaderEnding(kind, variant), name, ShaderEnding(kind, variant));
    else rfa::Log("Red: a material on the library's shader %lu (none of Guerrilla's) is drawn as a mesh of the kind %s, which it has no version for", shader, ShaderEnding(kind, variant));
}

// The game's shader binding, watched: what it is about to draw red is written to the log, once each.
bool __fastcall BindShader(void* library, void* unused, DWORD shader, DWORD second, DWORD third) {
    void* renderer = Renderer();
    const int kind = renderer ? MeshKind(renderer, nullptr) : -1;
    if (kind >= 0) {
        const int variant = MeshVariant(renderer, nullptr);
        if (shader == kNoShader || !ShaderHasVersion(library, nullptr, shader, kind, variant)) NoteRed(library, shader, kind, variant);
    }
    return g_bindShader(library, unused, shader, second, third);
}

void RedMaterials() {
    g_bindShader = reinterpret_cast<BindShaderFn>(
        rfa::HookFunction(kBindShader, kBindShaderPrologue, reinterpret_cast<void*>(&BindShader)));
    if (!g_bindShader) rfa::Log("Red: could not watch the game's shader binding; materials drawn red aren't named in the log");
    else rfa::LogDetail("Red: a material the game draws red (no shader for it, or none for its mesh's kind) is named here, once each");
}

}  // namespace

// ---- What the other parts borrow (guerrilla_gear.h)

std::wstring gear::Folder() { return GearFolder(); }

const BYTE* gear::WeaponEntryNamed(const char* name) { return ::WeaponEntryNamed(name); }

bool gear::PlaysMason(const BYTE* human) { return human && PlaysSet(human, kMasonSet); }

bool gear::GiveWeapon(BYTE* player, const char* name, std::string& why) {
    const BYTE* info = name && *name ? ::WeaponEntryNamed(name) : nullptr;
    const void* inventoryItem = info ? *reinterpret_cast<const void* const*>(info + kInventoryInfo) : nullptr;
    if (!player || !inventoryItem) {
        why = "not built";
        return false;
    }
    if (HasItem(player, inventoryItem)) {
        why = "you carry it already";
        return false;
    }
    // A free slot; Mason's hammer slot last, as it is his hammer's.
    const bool mason = PlaysSet(player, kMasonSet);
    int slot = -1;
    for (int s = 0; slot < 0 && s < kRadialSlots; ++s)
        if (!(mason && s == kHammerSlot) && !RecordInSlot(player, s)) slot = s;
    if (slot < 0 && !mason) {
        why = "no free slot";
        return false;
    }
    if (slot < 0 && !RecordInSlot(player, kHammerSlot)) slot = kHammerSlot;
    if (slot < 0) {
        why = "no free slot (drop a gun)";
        return false;
    }
    if (!GiveByInventoryItem(player, inventoryItem, 0, slot, false, false, false)) {
        why = "the game refused it";
        return false;
    }
    rfa::LogDetail("Upgrades: gave %s, slot %d", name, slot);
    return true;
}

// ---- The player's climb, for the jetpack (backpacks.cpp)
//
// Armageddon moves a human through its character controller (human+0x98, there while human+0xAC is 1;
// Guerrilla's character_controller): the game's own get and set of its velocity (0x850950 / 0x850990,
// cdecl: the human, a vector; Guerrilla's human_controller_get_vel / _set_vel, which its jetpack uses),
// and whether it stands on the ground (0x850830: the controller's +0x38 bit 2). The heading step runs
// before the controller's step each frame, so a velocity set here is the one the step starts from. One
// catch: for the player, the step puts a climb a little above the last one it worked out (controller
// +0x60) back to it, unless he jumps (0x79E77E); Guerrilla skips that while its jetpack is on, Armageddon
// has no such case, so Lift also writes the new climb there (the step writes its own after).

struct Vector3 {
    float x, y, z;
};
auto const ControllerVelocity = reinterpret_cast<void(__cdecl*)(BYTE* human, Vector3* out)>(0x850950);
auto const SetControllerVelocity = reinterpret_cast<void(__cdecl*)(BYTE* human, const Vector3* velocity)>(0x850990);
auto const OnGround = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x850830);
constexpr DWORD kController = 0x98, kHasController = 0xAC, kLastClimb = 0x60;

bool gear::Airborne(BYTE* player) {
    return player && *reinterpret_cast<const int*>(player + kOnFootState) == 0 && player[kHasController] == 1 && !OnGround(player);
}

float gear::Climb(BYTE* player) {
    Vector3 velocity{};
    ControllerVelocity(player, &velocity);
    return velocity.y;
}

void gear::Lift(BYTE* player, float climb) {
    if (!player || player[kHasController] != 1) return;
    Vector3 velocity{};
    ControllerVelocity(player, &velocity);
    velocity.y += climb;
    SetControllerVelocity(player, &velocity);
    if (BYTE* controller = *reinterpret_cast<BYTE**>(player + kController))
        *reinterpret_cast<float*>(controller + kLastClimb) = velocity.y;
}

// Guerrilla's apply_jump_boost (RM 0x957190): his controller's velocity, its sideways part x horizontal, its climb
// max(climb, 2) + vertical going up, climb - vertical going down (Thrust's slam); returns the climb before.
float gear::JumpBoost(BYTE* player, float horizontal, float vertical, bool up) {
    if (!player || player[kHasController] != 1) return 0.0f;
    Vector3 velocity{};
    ControllerVelocity(player, &velocity);
    const float before = velocity.y;
    velocity.x *= horizontal;
    velocity.z *= horizontal;
    velocity.y = up ? (std::max)(velocity.y, 2.0f) + vertical : velocity.y - vertical;
    SetControllerVelocity(player, &velocity);
    if (BYTE* controller = *reinterpret_cast<BYTE**>(player + kController))
        *reinterpret_cast<float*>(controller + kLastClimb) = velocity.y;
    return before;
}

void gear::CameraLook(BYTE* human, float* out) {
    if (g_lookVector) g_lookVector(human, out);
    else reinterpret_cast<LookVectorFn>(kLookVector)(human, out);
}

bool gear::Velocity(BYTE* player, float* out) {
    if (!player || !out || player[kHasController] != 1) return false;
    Vector3 velocity{};
    ControllerVelocity(player, &velocity);
    out[0] = velocity.x;
    out[1] = velocity.y;
    out[2] = velocity.z;
    return true;
}

bool gear::SetVelocity(BYTE* player, const float* velocity) {
    if (!player || !velocity || player[kHasController] != 1) return false;
    const Vector3 v{velocity[0], velocity[1], velocity[2]};
    SetControllerVelocity(player, &v);
    if (BYTE* controller = *reinterpret_cast<BYTE**>(player + kController))
        *reinterpret_cast<float*>(controller + kLastClimb) = v.y;
    return true;
}

namespace {

// Swapping a gun on the ground for one of his (the action button, 0x8BEB00: the gun in the slot asked
// for, or in his hands, dropped): Guerrilla never lets the hammer go (player_can_swap_inv_item, RM
// 0xA111B0, refuses slot 3), so for the player a swap that would drop one of Guerrilla's hammers doesn't
// happen; with a gun in his hands it swaps that.
void __cdecl SwapWeapon(BYTE* human, BYTE* item, int slot) {
    if (human && human == LocalPlayer()) {
        BYTE* out = slot == -1 ? RecordInHands(human) : RecordInSlot(human, slot);
        if (IsHammer(RecordWeapon(out))) {
            const ULONGLONG now = GetTickCount64();
            if (now - g_lastHammerNote > 5000) {
                g_lastHammerNote = now;
                rfa::LogDetail("Hammer: not swapped away (Guerrilla never drops it); swap with a gun in his hands");
            }
            return;
        }
    }
    g_swapWeapon(human, item, slot);
}

// ---- Room for more projectiles (see the top)

constexpr DWORD kProjectilePoolConstructor = 0x82FE60;  // the pool's start-up constructor (thiscall: the pool)
constexpr size_t kProjectilePoolConstructorPrologue = 6;  // push ebp / mov ebp,esp / sub esp,5Ch
constexpr DWORD kProjectileLoader = 0x82E9F0;            // the projectile tables' set-up and first read (cdecl, none)
constexpr size_t kProjectileLoaderPrologue = 10;          // push ebp / mov ebp,esp / cmp dword ptr [2174E70h],0
constexpr DWORD kProjectilePool = 0x2174F90;             // the projectile types (a word at +0xA how many it holds; a byte each at +0x18, 0xFF free)
constexpr DWORD kPoolHolds = 0xA, kPoolFlags = 0x18;
constexpr BYTE kPoolFree = 0xFF;
constexpr DWORD kNamePool = 0x2174ED8, kStagePool = 0x2174F38;  // the "Proj String Pool" and "Projectile Mempool" (size +0x44, used +0x4C)
constexpr DWORD kGameProjectiles = 100, kProjectiles = 256;
constexpr DWORD kProjectileTypeSize = 0xC0;              // and a link (4 bytes) and a flag (1 byte) each
constexpr DWORD kGamePoolMemory = 0x4CF9;                // the constructor's buffer, inside the pool object (+0x30)
constexpr DWORD kProjectilePoolMemory = kProjectiles * (kProjectileTypeSize + 4 + 1) + 64;  // with room for its alignment

// The constructor's own values, checked before anything changes: its buffer's size (push 4CF9h), its
// count (push 64h, a one-byte value: 127 at most) and the buffer's place in the object (add eax,30h).
constexpr DWORD kPoolMemoryValue = 0x82FE7B, kPoolCountValue = 0x82FE8F, kPoolBufferValue = 0x82FE84;

// The four sizes in the loader (each `push imm32`): the names' block and its pool, the stages' block and
// its pool; the blocks come from the Permanent Mempool once, while the game starts.
struct ProjectileRoom { const char* what; DWORD at; DWORD game; DWORD ours; };
const ProjectileRoom kProjectileRooms[] = {
    {"names' block", 0x82EA03, 0x800, 0x2000},
    {"names' pool", 0x82EA4D, 0x800, 0x2000},
    {"stages' block", 0x82EA80, 0x18000, 0x40000},
    {"stages' pool", 0x82EACB, 0x18000, 0x40000},
};

alignas(16) BYTE g_projectilePoolMemory[kProjectilePoolMemory];

auto const PoolBase = reinterpret_cast<void(__fastcall*)(BYTE* pool, void* unused)>(0x71C5D0);
auto const MempoolOver = reinterpret_cast<void(__fastcall*)(void* mempool, void* unused, void* memory, DWORD size, const void* name,
                                                             DWORD first, DWORD second)>(0x428240);
using PoolInitFn = void(__fastcall*)(BYTE* pool, void* unused, void* mempool, DWORD holds);
auto const ProjectilePoolInit = reinterpret_cast<PoolInitFn>(0x82FEB0);
auto const MempoolDone = reinterpret_cast<void(__fastcall*)(void* mempool, void* unused)>(0x427F40);
const void* const kObjectPoolName = reinterpret_cast<const void*>(0xF7101C);  // "object pool", the name the game gives it

using ProjectileLoaderFn = void(__cdecl*)();
ProjectileLoaderFn g_projectileLoader;

// What the game's start-up constructors of its fixed pools do (the projectile types' 0x82FE60, the
// melee moves' 0x881BC0): the pool's base, a memory pool laid over a buffer, the pool's set-up in it
// for so many places, the memory pool dropped. The mod's pass a buffer of their own and more places.
BYTE* BuildPool(BYTE* pool, BYTE* memory, DWORD size, PoolInitFn init, DWORD holds) {
    PoolBase(pool, nullptr);
    alignas(16) BYTE mempool[0x60];  // the game's takes 0x58 bytes of its stack
    MempoolOver(mempool, nullptr, memory, size, kObjectPoolName, 1, 0);
    init(pool, nullptr, mempool, holds);
    MempoolDone(mempool, nullptr);
    return pool;
}

// How many places such a pool holds (a word at +0xA) and how many are taken (a byte each, 0xFF while free).
int PoolHolds(DWORD pool) { return *reinterpret_cast<const WORD*>(pool + kPoolHolds); }

int PoolInUse(DWORD pool) {
    const BYTE* flags = *reinterpret_cast<BYTE* const*>(pool + kPoolFlags);
    int used = 0;
    for (int i = 0, holds = PoolHolds(pool); flags && i < holds; ++i)
        if (flags[i] != kPoolFree) ++used;
    return used;
}

// The projectile pool's constructor, with the mod's buffer and 256 places.
BYTE* __fastcall ProjectilePoolConstructor(BYTE* pool, void* unused) {
    return BuildPool(pool, g_projectilePoolMemory, sizeof(g_projectilePoolMemory), ProjectilePoolInit, kProjectiles);
}

int ProjectilesHeld() { return PoolHolds(kProjectilePool); }
int ProjectilesInUse() { return PoolInUse(kProjectilePool); }

void LogPoolUse(const char* what, DWORD pool) {
    rfa::LogDetail("Projectiles: %s %lu of %lu bytes in use", what, *reinterpret_cast<const DWORD*>(pool + kMempoolUsed),
             *reinterpret_cast<const DWORD*>(pool + kMempoolSize));
}

// After the game has set up its projectile tables and read projectiles.xtbl (Armageddon's with
// Guerrilla's, from the guerrilla_world mod): how full they are. Add-ons' come later.
void __cdecl ProjectileLoader() {
    g_projectileLoader();
    rfa::LogDetail("Projectiles: %d of %d places in use", ProjectilesInUse(), ProjectilesHeld());
    LogPoolUse("names:", kNamePool);
    LogPoolUse("stages:", kStagePool);
}

void MoreProjectiles() {
    if (ProjectilesHeld() != 0) {
        rfa::Log("Projectiles: the game has already built its table; it keeps its %lu", kGameProjectiles);
        return;
    }
    if (rfa::CodeValue(kPoolMemoryValue) != kGamePoolMemory || *reinterpret_cast<const BYTE*>(kPoolCountValue) != kGameProjectiles ||
        *reinterpret_cast<const BYTE*>(kPoolBufferValue) != 0x30) {
        rfa::Log("Projectiles: the game's constructor (0x%08lX) isn't as expected; the game keeps its %lu", kProjectilePoolConstructor, kGameProjectiles);
        return;
    }
    for (const ProjectileRoom& room : kProjectileRooms)
        if (rfa::CodeValue(room.at) != room.game) {
            rfa::Log("Projectiles: the game's %s size at 0x%08lX isn't 0x%lX (it's 0x%lX); the game keeps its %lu", room.what, room.at,
                     room.game, rfa::CodeValue(room.at), kGameProjectiles);
            return;
        }
    // All or nothing: the four sizes, then the constructor; any one failing puts back what changed.
    size_t changed = 0;
    for (; changed < std::size(kProjectileRooms); ++changed)
        if (!rfa::PatchCode(reinterpret_cast<void*>(kProjectileRooms[changed].at), &kProjectileRooms[changed].ours, sizeof(DWORD))) break;
    const bool hooked = changed == std::size(kProjectileRooms) &&
        rfa::HookFunction(kProjectilePoolConstructor, kProjectilePoolConstructorPrologue, reinterpret_cast<void*>(&ProjectilePoolConstructor)) != nullptr;
    if (!hooked) {
        for (size_t i = 0; i < changed; ++i) rfa::PatchCode(reinterpret_cast<void*>(kProjectileRooms[i].at), &kProjectileRooms[i].game, sizeof(DWORD));
        rfa::Log("Projectiles: could not change the game's code; it keeps its %lu", kGameProjectiles);
        return;
    }
    g_projectileLoader = reinterpret_cast<ProjectileLoaderFn>(
        rfa::HookFunction(kProjectileLoader, kProjectileLoaderPrologue, reinterpret_cast<void*>(&ProjectileLoader)));
    rfa::LogDetail("Projectiles: the game holds %lu (its own %lu), their names %lu bytes (0x%lX), their stages %lu bytes (0x%lX)", kProjectiles,
             kGameProjectiles, kProjectileRooms[1].ours, kProjectileRooms[1].game, kProjectileRooms[3].ours, kProjectileRooms[3].game);
}

// ---- Room for more melee moves (see the top)

constexpr DWORD kMeleeConstructor = 0x881BC0;    // the melee moves' pool's start-up constructor (thiscall: the pool)
constexpr size_t kMeleeConstructorPrologue = 6;  // push ebp / mov ebp,esp / sub esp,5Ch
constexpr DWORD kMeleeLoader = 0x876900;         // melee.xtbl's reader (cdecl: two values)
constexpr size_t kMeleeLoaderPrologue = 6;       // push ebp / mov ebp,esp / sub esp,68h
constexpr DWORD kMeleePool = 0x21AFEF0;
constexpr DWORD kGameMeleeMoves = 32, kMeleeMoves = 64;
constexpr DWORD kMeleeMoveSize = 0xE0;           // and a link (4 bytes) and a flag (1 byte) each
constexpr DWORD kGameMeleeMemory = 0x1CA5;       // the constructor's buffer, inside the pool object (+0x30)
constexpr DWORD kMeleePoolMemory = kMeleeMoves * (kMeleeMoveSize + 4 + 1) + 64;
// The constructor's own values, checked first: its buffer's size (push 1CA5h), its count (push 20h, one
// byte) and the buffer's place in the object (add eax,30h).
constexpr DWORD kMeleeMemoryValue = 0x881BDB, kMeleeCountValue = 0x881BEF, kMeleeBufferValue = 0x881BE4;

alignas(16) BYTE g_meleePoolMemory[kMeleePoolMemory];
auto const MeleePoolInit = reinterpret_cast<PoolInitFn>(0x7FD7C0);

using MeleeLoaderFn = int(__cdecl*)(int first, int second);
MeleeLoaderFn g_meleeLoader;

BYTE* __fastcall MeleePoolConstructor(BYTE* pool, void* unused) {
    return BuildPool(pool, g_meleePoolMemory, sizeof(g_meleePoolMemory), MeleePoolInit, kMeleeMoves);
}

// After melee.xtbl is read (Armageddon's with Guerrilla's, from the guerrilla_world mod): how full the table is.
// ---- Room in the tables' name pools (see the top: the moves' names, and the vehicles')
//
// A string pool's set-up takes its memory from a memory pool (`push size`) and makes the string pool over it
// (`push size` again); both values are made ours, checked against the game's first, all or nothing.
struct NamePool {
    const char* what;
    DWORD memoryValue;  // the push of the bytes taken for it
    DWORD poolValue;    // the push of the pool's size
    DWORD pool;         // the string pool (size +0x44, bytes used +0x4C)
    DWORD memory;       // where the set-up keeps its memory (0 until it has run)
    DWORD game, ours;
};

const NamePool kNamePools[] = {
    // "Melee Combat Strings": melee.xtbl's moves, set up by 0x8777E0.
    {"Melee move names", 0x8777F3, 0x87783D, 0x21B1BD8, 0x21AFDE0, 0x200, 0x1000},
    // "Vehicle info strings": vehicles.xtbl's names, models, display names, components, lights and props, set
    // up by 0x6271C0 (its memory from the pool its caller hands it: the Permanent Mempool at start-up).
    {"Vehicle names", 0x6271E0, 0x627228, 0x14A15F8, 0x14A15D0, 0x2000, 0x8000},
    // ("Vehicle interaction info strings", vehicle_interactions.xtbl's, is the guerrilla_vehicles mod's.)
};

DWORD NamePoolValue(const NamePool& p, DWORD field) { return *reinterpret_cast<const DWORD*>(p.pool + field); }

void MoreNameRoom() {
    for (const NamePool& p : kNamePools) {
        if (*reinterpret_cast<const DWORD*>(p.memory) != 0) {
            rfa::Log("%s: the game has already made its pool; it keeps its %lu bytes", p.what, p.game);
            continue;
        }
        if (rfa::CodeValue(p.memoryValue) != p.game || rfa::CodeValue(p.poolValue) != p.game) {
            rfa::Log("%s: the game's code (0x%08lX, 0x%08lX) isn't as expected; it keeps its %lu bytes", p.what, p.memoryValue, p.poolValue,
                     p.game);
            continue;
        }
        if (!rfa::PatchCode(reinterpret_cast<void*>(p.memoryValue), &p.ours, sizeof(p.ours))) {
            rfa::Log("%s: could not change the game's code; it keeps its %lu bytes", p.what, p.game);
            continue;
        }
        if (!rfa::PatchCode(reinterpret_cast<void*>(p.poolValue), &p.ours, sizeof(p.ours))) {
            rfa::PatchCode(reinterpret_cast<void*>(p.memoryValue), &p.game, sizeof(p.game));
            rfa::Log("%s: could not change the game's code; it keeps its %lu bytes", p.what, p.game);
            continue;
        }
        rfa::LogDetail("%s: %lu bytes (the game's %lu)", p.what, p.ours, p.game);
    }
}

// Each pool's use, once its tables are read (logged at the player's first step).
void LogNameRoom() {
    for (const NamePool& p : kNamePools)
        if (NamePoolValue(p, kMempoolSize)) rfa::LogDetail("%s: %lu of %lu bytes in use", p.what, NamePoolValue(p, kMempoolUsed), NamePoolValue(p, kMempoolSize));
}

constexpr DWORD kMeleeNamesPool = 0x21B1BD8;

int __cdecl MeleeLoader(int first, int second) {
    const int result = g_meleeLoader(first, second);
    rfa::LogDetail("Melee moves: %d of %d places in use; names %lu of %lu bytes", PoolInUse(kMeleePool), PoolHolds(kMeleePool),
             *reinterpret_cast<const DWORD*>(kMeleeNamesPool + kMempoolUsed), *reinterpret_cast<const DWORD*>(kMeleeNamesPool + kMempoolSize));
    return result;
}

// The user, 2026-11-06: "Yes, enlarge it". The constructor's count is one byte and its buffer a fixed part
// of the object (as the projectiles'), so the mod runs the constructor itself with a buffer for 64.
void MoreMeleeMoves() {
    if (PoolHolds(kMeleePool) != 0) {
        rfa::Log("Melee moves: the game has already built its table; it keeps its %lu", kGameMeleeMoves);
        return;
    }
    if (rfa::CodeValue(kMeleeMemoryValue) != kGameMeleeMemory || *reinterpret_cast<const BYTE*>(kMeleeCountValue) != kGameMeleeMoves ||
        *reinterpret_cast<const BYTE*>(kMeleeBufferValue) != 0x30) {
        rfa::Log("Melee moves: the game's constructor (0x%08lX) isn't as expected; the game keeps its %lu", kMeleeConstructor, kGameMeleeMoves);
        return;
    }
    if (!rfa::HookFunction(kMeleeConstructor, kMeleeConstructorPrologue, reinterpret_cast<void*>(&MeleePoolConstructor))) {
        rfa::Log("Melee moves: could not change the game's code; it keeps its %lu", kGameMeleeMoves);
        return;
    }
    g_meleeLoader = reinterpret_cast<MeleeLoaderFn>(rfa::HookFunction(kMeleeLoader, kMeleeLoaderPrologue, reinterpret_cast<void*>(&MeleeLoader)));
    rfa::LogDetail("Melee moves: the game holds %lu (its own %lu)", kMeleeMoves, kGameMeleeMoves);
}

// ---- Room in the start-up's memory pools (see the top: the preloaded items, the Permanent Mempool)
//
// The game's start-up (0xA45600) makes each of these pools the same way: `push size` → 0xA44A20 (the block,
// _aligned_malloc from the process heap), then `push size` again → 0x512B30 (the pool made of it). Both values
// are made ours, before the start-up runs: each checked against the game's first, neither changed if one
// differs, the first put back if the second can't be written.
struct StartupPool {
    const char* what;
    DWORD blockValue;  // the push of the block's size
    DWORD poolValue;   // the push of the pool's size
    DWORD pool;        // the pool (size +0x44, bytes used +0x4C)
    DWORD game, ours;
    const char* holds;
};

const StartupPool kStartupPools[] = {
    {"Item preload", 0xA47521, 0xA4754F, 0x26AF1A0, 0x1100000, 32 * 1024 * 1024,
     "the preloaded items' containers, then Armageddon's items' low-detail textures"},
    // The user's 21:19 start stopped on it ("Failed aligned alloc ... in Permanent mempool cpu pool (max
    // 12.00MB, space left 3104)"): Armageddon's own (its permanent pegs 3.6 MB, the effects 3.1 MB, ...) and the
    // mods' raised tables (animation map, name pools, patrols, ...) no longer fit in 12 MB.
    {"Permanent mempool", 0xA47277, 0xA472A5, kPermanentMempool, 0xC00000, 24 * 1024 * 1024,
     "what the game keeps for good from start-up (its tables, pegs, effects; the mods' raised tables)"},
};

void MoreStartupRoom() {
    for (const StartupPool& p : kStartupPools) {
        const DWORD gameMb = p.game / (1024 * 1024), oursMb = p.ours / (1024 * 1024);
        if (*reinterpret_cast<const DWORD*>(p.pool + kMempoolSize) != 0) {
            rfa::Log("%s: the game has already made its pool; it keeps its %lu MB", p.what, gameMb);
            continue;
        }
        if (rfa::CodeValue(p.blockValue) != p.game || rfa::CodeValue(p.poolValue) != p.game) {
            rfa::Log("%s: the game's code (0x%08lX, 0x%08lX) isn't as expected; it keeps its %lu MB", p.what, p.blockValue, p.poolValue, gameMb);
            continue;
        }
        if (!rfa::PatchCode(reinterpret_cast<void*>(p.blockValue), &p.ours, sizeof(p.ours))) {
            rfa::Log("%s: could not change the game's code; it keeps its %lu MB", p.what, gameMb);
            continue;
        }
        if (!rfa::PatchCode(reinterpret_cast<void*>(p.poolValue), &p.ours, sizeof(p.ours))) {
            rfa::PatchCode(reinterpret_cast<void*>(p.blockValue), &p.game, sizeof(p.game));
            rfa::Log("%s: could not change the game's code; it keeps its %lu MB", p.what, gameMb);
            continue;
        }
        rfa::LogDetail("%s: %lu MB (the game's %lu MB): %s", p.what, oursMb, gameMb, p.holds);
    }
}

// At the player's first step (start-up and the level's load done): each pool's use, logged once.
void LogItemPreload() {
    for (const StartupPool& p : kStartupPools) {
        const DWORD size = *reinterpret_cast<const DWORD*>(p.pool + kMempoolSize);
        const DWORD used = *reinterpret_cast<const DWORD*>(p.pool + kMempoolUsed);
        if (size) rfa::LogDetail("%s: %lu of %lu bytes in use (%lu left)", p.what, used, size, size > used ? size - used : 0);
    }
}

bool g_upgradesRunning = false;

void MasonsHammer() {
    g_swapWeapon = reinterpret_cast<SwapWeaponFn>(rfa::HookFunction(kSwapWeapon, kSwapWeaponPrologue, reinterpret_cast<void*>(&SwapWeapon)));
    if (!g_swapWeapon) rfa::Log("Hammer: could not wrap the swap; Guerrilla's hammer can be swapped away");
    else rfa::LogDetail("Hammer: Mason is given Guerrilla's sledgehammer (slot 3) when he has none, and never swaps it away");
}

// ---- The player's melee hits, logged (the user's Round 1, 2026-11-06: the hammer "does no damage")
//
// A diagnostic for that report, to keep or take out once its cause is known: it changes nothing in
// the game. So a test shows whether a swing lands, on what, and for how much. Armageddon picks a melee hit's
// damage in 0x878780 (cdecl: the attacker, the move's data for the attacker's animation set, the object
// hit; the damage returned): by the object's type (0x422AD0: 0 a person, 2 a building or other mover,
// 3 a vehicle) the data's figure for it (people, structures, vehicles), or 10 when there is no data.
// The wrap calls it and returns its figure unchanged; for the player's hits it logs a line, at most
// four a second. No line while swinging at someone means the hit test never got that far. It also
// tells the backpacks of each hit (backpacks::MeleeHit: Guerrilla's melee hit asks the attacker's pack
// for its ripple, attempt_ripple).
constexpr DWORD kMeleeDamage = 0x878780;
constexpr size_t kMeleeDamagePrologue = 6;  // push ebp / mov ebp,esp / sub esp,24h
using MeleeDamageFn = float(__cdecl*)(BYTE* attacker, const BYTE* data, BYTE* target);
MeleeDamageFn g_meleeDamage = nullptr;
auto const ObjectTypeOf = reinterpret_cast<int(__thiscall*)(const BYTE* object)>(0x422AD0);
DWORD g_lastMeleeLog = 0;

const char* ObjectKind(int type) {
    switch (type) {
        case 0: return "person";
        case 2: return "building or other mover";
        case 3: return "vehicle";
        default: return "object";
    }
}

float __cdecl MeleeDamage(BYTE* attacker, const BYTE* data, BYTE* target) {
    const float damage = g_meleeDamage(attacker, data, target);
    if (g_upgradesRunning) backpacks::MeleeHit(attacker, target);  // a pack's ripple (Guerrilla's attempt_ripple)
    if (rfa::DetailLog() && attacker && attacker == LocalPlayer()) {
        const DWORD now = GetTickCount();
        if (now - g_lastMeleeLog >= 250) {
            g_lastMeleeLog = now;
            const char* weapon = WeaponName(HeldWeapon(attacker));
            const int type = target ? ObjectTypeOf(target) : -1;
            rfa::LogDetail("Melee hit: %s hit a %s (type %d) for %.0f%s", weapon ? weapon : "(nothing in hand)", ObjectKind(type), type,
                     damage, data ? "" : " (the move has no data for the player's set: the game's 10)");
        }
    }
    return damage;
}

void LogMeleeHits() {
    g_meleeDamage = reinterpret_cast<MeleeDamageFn>(rfa::HookFunction(kMeleeDamage, kMeleeDamagePrologue, reinterpret_cast<void*>(&MeleeDamage)));
    if (!g_meleeDamage) rfa::Log("Melee hits: could not wrap the damage pick; they aren't logged");
    else if (rfa::DetailLog()) rfa::LogDetail("Melee hits: the player's are logged (\"Melee hit:\" lines)");
}

// ---- The guns' high-detail copies, logged (2026-11-12: does a gun he swaps to stay on its low copy?)
//
// A diagnostic: it changes nothing in the game. Armageddon draws an item's model with the low-detail copies of its
// textures, and streams in its <item>_high container (the big ones, type 25) only for the player's gun as he draws
// it (0x737250 → 0x7CFDC0, thiscall: the item; true when it asked for it), as Guerrilla does for the player's
// equipped gun. But one item at a time: the one whose copy is in is kept at 0x1BB1D48 (its info) until let go
// (holstering marks it, 0x8BB550 → 0x7D7740; the frame update frees it, 0x7D76C0), and another is refused while it
// is held. An item's info is at +0x98: its name +0, its _high container +0x1C (0: none).
constexpr DWORD kStreamHighDetail = 0x7CFDC0;
constexpr BYTE kStreamHighDetailPrologue[] = {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC};  // push ebp / mov ebp,esp / push ecx / mov [ebp-4],ecx
using StreamHighDetailFn = bool(__fastcall*)(BYTE* item, void* unused);  // thiscall
StreamHighDetailFn g_streamHighDetail = nullptr;
BYTE* const* const kHighDetailHolder = reinterpret_cast<BYTE* const*>(0x1BB1D48);
constexpr DWORD kItemInfo = 0x98, kItemInfoHighContainer = 0x1C;
int g_highDetailLogs = 0;

const char* ItemInfoName(const BYTE* info) {
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    return name ? name : "?";
}

bool __fastcall StreamHighDetail(BYTE* item, void* unused) {
    const BYTE* holder = *kHighDetailHolder;  // before the call
    const bool asked = g_streamHighDetail(item, unused);
    if (g_highDetailLogs < 100 && item) {
        ++g_highDetailLogs;
        const BYTE* info = *reinterpret_cast<const BYTE* const*>(item + kItemInfo);
        const char* name = ItemInfoName(info);
        if (!info || !*reinterpret_cast<const DWORD*>(info + kItemInfoHighContainer))
            rfa::LogDetail("Guns: %s has no high-detail copy (no _high container); its low copy is drawn", name);
        else if (asked)
            rfa::LogDetail("Guns: %s's high-detail copy streamed in", name);
        else if (holder && holder != info)
            rfa::LogDetail("Guns: %s's high-detail copy refused, held by %s; it stays on its low copy", name, ItemInfoName(holder));
        else if (holder == info)
            rfa::LogDetail("Guns: %s's high-detail copy already in (kept)", name);
        else
            rfa::LogDetail("Guns: %s's high-detail copy not asked for (the item isn't ready); it stays on its low copy", name);
    }
    return asked;
}

void LogHighDetailGuns() {
    if (memcmp(reinterpret_cast<const void*>(kStreamHighDetail), kStreamHighDetailPrologue, sizeof kStreamHighDetailPrologue) != 0) {
        rfa::Log("Guns: the high-detail stream-in (0x%08lX) isn't the publicbeta's; not logged", kStreamHighDetail);
        return;
    }
    g_streamHighDetail = reinterpret_cast<StreamHighDetailFn>(
        rfa::HookFunction(kStreamHighDetail, sizeof kStreamHighDetailPrologue, reinterpret_cast<void*>(&StreamHighDetail)));
    if (!g_streamHighDetail) rfa::Log("Guns: could not wrap the high-detail stream-in; not logged");
    else rfa::LogDetail("Guns: each gun he draws says whether its high-detail copy streamed in (\"Guns:\" lines)");
}

// ---- The held weapon sounds' starts and stops, logged (the user, 2026-11-10: a turret gun held sounds once, then is silent)
//
// A diagnostic for that report, to keep or take out once its cause is known: it changes nothing in the game. A
// weapon whose sound goes on while it fires has a looping event, which the game starts once and stops when the
// firing ends (the builder's GuerrillaSoundBanks.WeaponSound; its loop events are the "pair" lines of
// mods\guerrilla_world\weapons_sound_banks.txt). The game uses the same event for a dry fire, an overheat and a
// turning turret, saying which with the switch WEAPON_SOUND_TYPE. Its audio functions, all cdecl: 0xB855E0 sets a
// switch on a sound object (group, value, object), 0xB846E0 plays an event on one (object, event, where the
// handle goes, unused, a delay), 0xB85110 stops a sound (where its handle is), 0xB84D20 gives a handle's game
// object (0 once the sound has ended). The wraps call them and write, for the held events: each start with the
// switch last set on that object, and each stop with how long the sound had played and whether it was still playing.
constexpr DWORD kSetSwitch = 0xB855E0, kPlaySound = 0xB846E0, kStopSound = 0xB85110;
constexpr size_t kAudioPrologue = 10;  // push ebp / mov ebp,esp / push -1 / push <its handler>: the same for all three
using SetSwitchFn = void(__cdecl*)(DWORD group, DWORD value, DWORD soundObject);
using PlaySoundFn = int(__cdecl*)(DWORD soundObject, DWORD eventId, DWORD* handle, int unused, int delayMs);
using StopSoundFn = int(__cdecl*)(DWORD* handle);
auto const SoundGameObject = reinterpret_cast<DWORD(__cdecl*)(DWORD handle)>(0xB84D20);
SetSwitchFn g_setSwitch = nullptr;
PlaySoundFn g_playSound = nullptr;
StopSoundFn g_stopSound = nullptr;

// Wwise's id of a name: FNV-1 (32 bits) of its lower-case letters.
constexpr DWORD SoundId(const char* name) {
    DWORD h = 2166136261u;
    for (; *name; ++name) h = (h * 16777619u) ^ static_cast<BYTE>(*name >= 'A' && *name <= 'Z' ? *name + 32 : *name);
    return h;
}
constexpr DWORD kWeaponSoundType = SoundId("WEAPON_SOUND_TYPE");

const char* WeaponSoundTypeName(DWORD value) {
    if (value == SoundId("primary_fire")) return "a shot";
    if (value == SoundId("no_ammo")) return "a dry fire";
    if (value == SoundId("overheat")) return "an overheat";
    if (value == SoundId("turret_turn")) return "a turning turret";
    return value ? "another use" : "no use set";
}

std::vector<DWORD> g_heldEvents;  // the held weapon sounds' events, sorted
struct ObjectSwitch {
    DWORD soundObject, value;
};
std::vector<ObjectSwitch> g_weaponSoundTypes;  // the WEAPON_SOUND_TYPE last set on each sound object (the latest 64)
struct HeldSound {
    DWORD handle, eventId, startedAt;
};
std::vector<HeldSound> g_heldSounds;  // the held sounds started and not yet stopped (the latest 64)

void __cdecl SetSwitchLogged(DWORD group, DWORD value, DWORD soundObject) {
    if (group == kWeaponSoundType) {
        auto found = std::find_if(g_weaponSoundTypes.begin(), g_weaponSoundTypes.end(), [&](const ObjectSwitch& s) { return s.soundObject == soundObject; });
        if (found != g_weaponSoundTypes.end()) found->value = value;
        else {
            if (g_weaponSoundTypes.size() >= 64) g_weaponSoundTypes.erase(g_weaponSoundTypes.begin());
            g_weaponSoundTypes.push_back({soundObject, value});
        }
    }
    g_setSwitch(group, value, soundObject);
}

int __cdecl PlaySoundLogged(DWORD soundObject, DWORD eventId, DWORD* handle, int unused, int delayMs) {
    const int result = g_playSound(soundObject, eventId, handle, unused, delayMs);
    if (std::binary_search(g_heldEvents.begin(), g_heldEvents.end(), eventId)) {
        const DWORD started = handle ? *handle : 0;
        auto use = std::find_if(g_weaponSoundTypes.begin(), g_weaponSoundTypes.end(), [&](const ObjectSwitch& s) { return s.soundObject == soundObject; });
        if (g_heldSounds.size() >= 64) g_heldSounds.erase(g_heldSounds.begin());
        g_heldSounds.push_back({started, eventId, GetTickCount()});
        RFA_LOG_DETAIL_FIRST(400, "Held sounds: event %lu started for %s (sound object 0x%08lX, handle %lu, result %d)", eventId,
                      WeaponSoundTypeName(use != g_weaponSoundTypes.end() ? use->value : 0), soundObject, started, result);
    }
    return result;
}

int __cdecl StopSoundLogged(DWORD* handle) {
    const DWORD stopped = handle ? *handle : 0;
    auto found = std::find_if(g_heldSounds.begin(), g_heldSounds.end(), [&](const HeldSound& s) { return s.handle == stopped; });
    if (stopped && found != g_heldSounds.end()) {
        RFA_LOG_DETAIL_FIRST(400, "Held sounds: event %lu (handle %lu) stopped by the game after %lu ms; %s", found->eventId, stopped,
                      GetTickCount() - found->startedAt, SoundGameObject(stopped) ? "it was still playing" : "it had already ended by itself");
        g_heldSounds.erase(found);
    }
    return g_stopSound(handle);
}

void LogHeldSounds() {
    std::ifstream list(rfa::RootDir() + L"\\mods\\guerrilla_world\\weapons_sound_banks.txt");
    for (std::string word; list >> word;) {
        unsigned long loopEvent = 0, stopEvent = 0;
        if (word == "pair" && list >> loopEvent >> stopEvent) g_heldEvents.push_back(loopEvent);
    }
    std::sort(g_heldEvents.begin(), g_heldEvents.end());
    if (g_heldEvents.empty()) return;  // no weapons built: nothing to log
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68};
    for (DWORD function : {kSetSwitch, kPlaySound, kStopSound})
        if (memcmp(reinterpret_cast<const void*>(function), opening, sizeof(opening)) != 0) {
            rfa::Log("Held sounds: the game's audio code isn't as expected; they aren't logged");
            return;
        }
    g_setSwitch = reinterpret_cast<SetSwitchFn>(rfa::HookFunction(kSetSwitch, kAudioPrologue, reinterpret_cast<void*>(&SetSwitchLogged)));
    g_playSound = reinterpret_cast<PlaySoundFn>(rfa::HookFunction(kPlaySound, kAudioPrologue, reinterpret_cast<void*>(&PlaySoundLogged)));
    g_stopSound = reinterpret_cast<StopSoundFn>(rfa::HookFunction(kStopSound, kAudioPrologue, reinterpret_cast<void*>(&StopSoundLogged)));
    if (!g_setSwitch || !g_playSound || !g_stopSound) rfa::Log("Held sounds: could not wrap the game's audio functions; they aren't all logged");
    else rfa::LogDetail("Held sounds: %u held weapon sounds' starts and stops are logged (\"Held sounds:\" lines)", static_cast<unsigned>(g_heldEvents.size()));
}

}  // namespace

void guerrilla_gear::Init() {
    if (rfa::ReadLogSwitch(L"GuerrillaGear")) rfa::LogDetail("Log=1: routine lines and the diagnoses (\"[guerrilla_gear:log]\") are on");
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    MoreInventoryItems();
    MoreStartupRoom();
    MoreProjectiles();
    MoreMeleeMoves();
    MoreNameRoom();
    MoreTriggerRoom();
    PlayerAnimations();
    DroppedGuns();
    ReadAmmoCapacity();
    ThrownCarried();
    RedMaterials();
    PlayerMovement();
    LookAheadStill();
    HeldInPlace();
    MasonsHammer();
    LogMeleeHits();  // its hook also gives the packs their ripple on a hit
    nano_rifle::Init();
    curved_trail::Init();
    // Diagnoses that change nothing in the game, each wrapping game code the player's every step or sound
    // passes through: only with Log=1.
    if (rfa::DetailLog()) {
        WatchStillTurns();
        LogHighDetailGuns();
        LogHeldSounds();
    }
    // The upgrades' purchases happen in the heading step: without it the window stays shut.
    if (g_headingStep) {
        upgrades::Init();
        backpacks::Init();
        g_upgradesRunning = true;
    } else {
        rfa::Log("Upgrades: no heading step to buy them in; the window is off");
    }
}

void guerrilla_gear::Menu(const RFAMenu* menu) {
    if (g_upgradesRunning) upgrades::Menu(menu);
}

// ---- The placed explosives, for other mods (guerrilla_hud's minimap)
//
// Guerrilla's minimap (cartographer::collect_projectiles RM 0x86D000, render_projectiles 0x85FCC0) shows every
// projectile whose weapon's class is THROWN_CHARGE, THROWN_MINE or SINGULARITY_BOMB inside its zoom radius,
// whoever threw it: remote charges white and pulsing when stuck to something (its parent or a constraint, 0x85FF83),
// else faint grey; mines orange; singularity bombs teal. Ours are Guerrilla's own weapons thrown the same way
// (rfg_charge_placer, mine_placer, singularity_bomb: Shot_Type Thrown, their projectiles made by 0x723300).
// Armageddon's projectiles are the world's list of subtype 10 (0x43ABC0 how many, 0x43AC10 each; thiscall: the
// world); a projectile's weapons.xtbl entry is at +0x188 (null for none), its class at the entry's +0x10
// (Armageddon's numbers: thrown_charge 3, thrown_mine 4, singularity_bomb 12); an object's place +4, its parent's
// handle +0x6C (-1 none, Guerrilla's +0x70), its child constraint +0x40 (as Guerrilla's); an item's owner's
// handle +0x164 (its set_owner, virtual +0xA0, writes it).
// (Its struct, GuerrillaGearExplosive, is in gear_api.h.)

namespace {
auto const ProjectileCount = reinterpret_cast<int(__thiscall*)(void* world)>(0x43ABC0);
auto const ProjectileAt = reinterpret_cast<BYTE*(__thiscall*)(void* world, int index)>(0x43AC10);
auto const ObjectHandle = reinterpret_cast<DWORD(__thiscall*)(const BYTE* object)>(0x424850);
constexpr DWORD kProjectileWeapon = 0x188, kWeaponClass = 0x10, kObjectParent = 0x6C, kObjectChildConstraint = 0x40, kItemOwner = 0x164;
constexpr int kThrownChargeClass = 3, kThrownMineClass = 4, kSingularityBombClass = 12;
}  // namespace

// Fills up to `most` records (none when `out` is null) and returns how many there are, which may be more. Call it on
// the game's thread (as guerrilla_hud's Lua functions run): it reads the world's list as it is.
int GuerrillaGear_PlacedExplosives(GuerrillaGearExplosive* out, int most) {
    if (!guerrilla_gear::Running()) return 0;
    const BYTE* player = LocalPlayer();
    const DWORD playerHandle = player ? ObjectHandle(player) : 0xFFFFFFFF;
    const int count = ProjectileCount(kWorld);
    int found = 0;
    for (int i = 0; i < count; ++i) {
        const BYTE* projectile = ProjectileAt(kWorld, i);
        if (!projectile) continue;
        const BYTE* weapon = *reinterpret_cast<const BYTE* const*>(projectile + kProjectileWeapon);
        if (!weapon) continue;
        const int weaponClass = *reinterpret_cast<const int*>(weapon + kWeaponClass);
        const int kind = weaponClass == kThrownChargeClass ? 0 : weaponClass == kThrownMineClass ? 1 : weaponClass == kSingularityBombClass ? 2 : -1;
        if (kind < 0) continue;
        if (out && found < most) {
            GuerrillaGearExplosive& e = out[found];
            const float* at = reinterpret_cast<const float*>(projectile + 0x4);
            e.x = at[0];
            e.y = at[1];
            e.z = at[2];
            e.kind = kind;
            e.stuck = *reinterpret_cast<const DWORD*>(projectile + kObjectParent) != 0xFFFFFFFF ||
                      *reinterpret_cast<void* const*>(projectile + kObjectChildConstraint) != nullptr ? 1 : 0;
            e.players = playerHandle != 0xFFFFFFFF && *reinterpret_cast<const DWORD*>(projectile + kItemOwner) == playerHandle ? 1 : 0;
        }
        ++found;
    }
    return found;
}
