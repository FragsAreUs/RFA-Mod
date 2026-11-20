// Saving in the worlds ported from Red Faction: Guerrilla ([GuerrillaSave] Worlds=, the ported world is
// "rfg").
//
// Armageddon saves its whole world into one stream in memory (the world object 0x1870630, +0x248C its
// size, 1 MB at most) and writes that stream into a save slot. Its campaign makes the stream at each
// checkpoint; its pause menu then offers SAVE GAME (the save screen writes the stream into the slot you
// pick) and LOAD CHECKPOINT (the world opened again from the stream). The rfg world is a level opened
// outside the campaign, so nothing ever made a stream there and the pause menu offered no save.
//
// Here, in those worlds:
//   - the pause menu's SAVE GAME (ui_pause_menu_pc.lua) asks the world for a save of the moment you are
//     in (rfg_save_game) and leaves the menu as Armageddon's SAVE GAME does; the menu's script waits
//     while the world makes it (rfg_save_state, a few frames: the gameplay step that makes it runs while
//     the game is paused), then opens Armageddon's own save screen, and the slot you pick gets it. LOAD
//     CHECKPOINT then goes back to the last save made. (The save screen borrows its hint bar from the
//     pause menu's document, so it is opened from there, the game's own way: opened with the menu
//     closed, its script failed on Vdo_hint_bar and the game crashed.)
//   - a save is never written into a slot on its own. Armageddon writes every checkpoint into its
//     autosave slot as soon as it is made (the save job's last step, 0x6D7000, when the world's
//     +0x2515 bit 1 says the profile owns its saves); that slot is the campaign's, so in these worlds
//     the bit is cleared and the stream stays in memory until you save it.
//   - a save of one of those worlds opens by its level's name (see OpenWorld), so that its zones get the
//     Guerrilla world's size as when the world is started; with the empty name the game gives a save,
//     the load hung.
//   - each save carries guerrilla_campaign's state (districts, morale, ...): its block, from
//     GuerrillaCampaign_SaveState when the world is saved, is added after the game's own data in the
//     save's stream, so it goes wherever the save goes (the slot you pick, LOAD CHECKPOINT). When one
//     of these worlds opens, GuerrillaCampaign_LoadState gets the save's block, or nothing (null, 0) for
//     a world started fresh; a save made before this has no block, and the campaign is left as it is.
//   - a death ends as Guerrilla's: the game-over screen (ui_failure.lua) offers CONTINUE, LOAD GAME and
//     QUIT GAME, and CONTINUE wakes Mason at a safehouse with the world as he left it. Guerrilla saved the
//     world at the death (world::save_and_continue) and loaded that save back, and loading a save out of
//     a mission puts him at the nearest safehouse of his district (save_state_finish); here the save is
//     made with him put at that safehouse (0x6DA3D0, the save's warp), then the screen's own LOAD
//     CHECKPOINT opens it. See ChooseSpot and StepRespawn below. That save is made after the death,
//     when a dead human's weapons are gone, so the weapons he had alive (noted every gameplay step) go
//     with it as a block of ours and are given back when it opens (TakeKit, GiveKit).
//   - guerrilla_campaign can ask for a reload at a spot (GuerrillaSave_ReloadAt, save_api.h: a Transporter's
//     RETRY, Guerrilla's world::save_and_reload with the player saved at the activity's start): the save with
//     him put there (0x6DA3D0; a respawn's after a death), then the game-over screen's fade and LOAD
//     CHECKPOINT's reload, called here (see StepReload).
//   - a death the campaign's running activity takes (Guerrilla's: its results screen in place of the death
//     screen, as the death timer runs out) raises no game-over screen (see GameOverStart); after that
//     screen's EXIT a dead player respawns as CONTINUE would (GuerrillaSave_Respawn).
//
// The game's save, found in its exe:
//   world 0x6DA1B0(message, b) asks for one: +0x2514 bit 2, +0x2515 bit 0 the "Checkpoint" message,
//     bit 1 "write it into the autosave slot when done";
//   the gameplay step 0x542110 (through 0x542030 and 0x6DA0E0) starts it: +0x2514 bit 4;
//   the world's streaming (0x6D70D0) then makes it: 0x6D7E20 writes the header and the game's systems
//     (+0x2514 bit 5 while it is being made), the zone stream-out jobs the rest, and the last job's end
//     sets the size (+0x248C), the streaming state (+0x23F4) back to 0 and clears bits 4 and 5.
#define RFA_MOD_NAME "guerrilla_save"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "save_api.h"
#include "campaign/campaign_api.h"
#include "rfa_game.h"

#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr DWORD kWorld = 0x1870630;
constexpr DWORD kWorldName = kWorld + 0x29EA;     // "rfg" in the ported world, "world" in the campaign's
constexpr DWORD kStreamingState = kWorld + 0x23F4; // 0 when the world isn't streaming zones out
constexpr DWORD kSavedSize = kWorld + 0x248C;      // the save in memory: its size, 0 for none
constexpr DWORD kSavedRoom = kWorld + 0x2490;      // and the room for it (0x100000)
constexpr DWORD kSaveFlags = kWorld + 0x2514;
constexpr DWORD kSaveFlags2 = kWorld + 0x2515;
constexpr BYTE kSaveAsked = 0x04;     // +0x2514 bit 2
constexpr BYTE kSaveStarted = 0x10;   // +0x2514 bit 4
constexpr BYTE kSaveBeingMade = 0x20; // +0x2514 bit 5
constexpr BYTE kWriteToAutosave = 0x02; // +0x2515 bit 1

constexpr DWORD kAskForSave = 0x6DA1B0;      // thiscall (world, bool message, bool b)
constexpr size_t kAskForSavePrologue = 6;    // push ebp / mov ebp,esp / sub esp,8
constexpr DWORD kMakeSave = 0x6D7E20;        // thiscall bool (world): the save's header and systems
constexpr size_t kMakeSavePrologue = 9;      // push ebp / mov ebp,esp / sub esp,0E8h
constexpr DWORD kOpenWorld = 0x6C91F0;       // thiscall int (world, name, bool fresh, bool b): opens a world
constexpr size_t kOpenWorldPrologue = 9;     // push ebp / mov ebp,esp / sub esp,1BCh
constexpr DWORD kSaveBuffer = 0x6CCF20;      // thiscall BYTE* (world): the save in memory's bytes
constexpr DWORD kSaveBytes = kWorld + 0x2494; // where that pointer is kept (+0x248C size, +0x2490 room)
constexpr DWORD kSaveMagic = 0x53474652;     // "RFGS"
constexpr int kSaveVersion = 0x5E;
constexpr DWORD kSaveAndWarp = 0x6DA3D0;     // thiscall (world, position, orientation): a save with the player put there
constexpr DWORD kFailureLoadCheckpoint = 0x578E50;  // void(): the game-over screen's LOAD CHECKPOINT (rf4_failure_load_checkpoint)
constexpr DWORD kMissionActive = 0x504A50;   // bool()
constexpr DWORD kRespawnWaitLimitMs = 10000;
// A reload at a spot (GuerrillaSave_ReloadAt): after the save, the game-over screen's fade before LOAD CHECKPOINT (its
// process 0x578BF0: 0x58D820(false, true, 1 s), asked each step: true once the screen is dark), then the world asked to
// open again from the save in memory as Armageddon's own save and reload does it (0x6DA2B0: world 0x6C88B0, +0x2515
// bit 2; the next gameplay step's 0x542030 opens the level again; the level load's exit 0x55E210 fades back in).
// LOAD CHECKPOINT's own 0x58D940 (the sounds paused, 0xB8AB30 / 0xB8ABF0, then 0x6C88B0) and 0x55F7B0 (0x1331855, the
// load screen's flag) were used at first: after that reload the sound stayed off until the pause menu was opened and
// closed (2026-11-19 20:54). I: 0xB8ABF0(1, 1) mutes (the gameplay state's exit 0x540C50 calls it too), and on LOAD
// CHECKPOINT's way the game-over screen's close posts its Failure_Screen_UnMute; with no game-over screen nothing
// unmuted it, until the pause menu's close (Interface_UnMute_State).
constexpr DWORD kFadeOut = 0x58D820;         // bool(bool at once, bool b, float seconds)
constexpr DWORD kAskReload = 0x6C88B0;       // thiscall bool (world): +0x2515 bit 2
constexpr DWORD kReloadWaitLimitMs = 10000;
constexpr DWORD kFadeLimitMs = 3000;
// The sound's pause (0xB8AB30(a, b): +a, b at 0x274552C / 0x274552D, nothing when already so) and resume (0xB8ABA0(a, b)):
// Armageddon's pause (0x520280, at its first count) and a level load (0x55DBD1) pause it, its unpause (0x520310, at its
// last count) and a load's end (0x55F44A) resume it, each with (1, 1).
constexpr DWORD kSoundPausedA = 0x274552C, kSoundPausedB = 0x274552D;
constexpr DWORD kGamePaused = 0x520260;      // bool(): the game's pause count above 0
using AskReloadFn = bool(__thiscall*)(void* world);
using ResumeSoundFn = int(__cdecl*)(bool a, bool b);
auto const ResumeSound = reinterpret_cast<ResumeSoundFn>(0xB8ABA0);
// Armageddon's game-over screen's start (0x578DA0, cdecl, no arguments): called only by the gameplay process (0x5410D0)
// once the local player's death timer (+0x1844) ran out, outside a mission (0x504B70 takes a mission's). With him
// dead: its title MENU_GAME_OVER (0xF9395C) into 0x135AE60 (64 bytes) and its reason MENU_YOU_DIED (0xF9394C) into
// 0x135ADE0 (128) (0xC9B390: a text copied, cut to its room), then the screen system 0x135CE10's 0x58C040(0x15) and
// (0x18). Replaced whole here (see GameOverStart).
constexpr DWORD kGameOverStart = 0x578DA0;
constexpr DWORD kGameOverTitle = 0x135AE60, kGameOverTitleRoom = 0x40, kGameOverTitleText = 0xF9395C;
constexpr DWORD kGameOverReason = 0x135ADE0, kGameOverReasonRoom = 0x80, kGameOverReasonText = 0xF9394C;
constexpr DWORD kScreenSystem = 0x135CE10;
constexpr int kGameOverScreen = 0x15, kGameOverScreenToo = 0x18;
using CopyTextFn = char*(__cdecl*)(char* to, const char* from, int room);
using RaiseScreenFn = void(__fastcall*)(void* screens, void* unused, int screen);
auto const CopyText = reinterpret_cast<CopyTextFn>(0xC9B390);
auto const RaiseScreen = reinterpret_cast<RaiseScreenFn>(0x58C040);
constexpr DWORD kGameplayStep = 0x542110;    // void(): the gameplay state's step, every frame (on the main thread)
constexpr size_t kGameplayStepPrologue = 6;  // push ebp / mov ebp,esp / sub esp,30h
constexpr DWORD kLocalPlayer = 0x8B92D0;     // object*(): the local player, or null
constexpr DWORD kSavegameOwned = 0x10FA278;  // byte: what savegame_is_owned() returns
constexpr DWORD kSaveWaitLimitMs = 10000;   // the first save in rfg took 15 ms

using AskForSaveFn = void(__thiscall*)(void* world, bool message, bool b);
using MakeSaveFn = bool(__thiscall*)(void* world);
using OpenWorldFn = int(__thiscall*)(void* world, const char* name, bool fresh, bool b);
using SaveBufferFn = const BYTE*(__thiscall*)(void* world);
using LocalPlayerFn = void*(__cdecl*)();
using IsDeadFn = bool(__thiscall*)(void* object);

using GameplayStepFn = void(__cdecl*)();
using SaveAndWarpFn = void(__thiscall*)(void* world, const float* position, const float* orientation);
using MissionActiveFn = bool(__cdecl*)();
using FailureChoiceFn = void(__cdecl*)();
using FadeOutFn = bool(__cdecl*)(bool atOnce, bool b, float seconds);

AskForSaveFn g_askForSave = nullptr;
MakeSaveFn g_makeSave = nullptr;
OpenWorldFn g_openWorld = nullptr;
GameplayStepFn g_gameplayStep = nullptr;
char g_savedLevel[64] = {};  // the name a save of a Guerrilla world opens with

// Our blocks in a save's stream, after the game's own data, one after another: each its start mark, the
// version, its length, the bytes, its length again, its end mark. A slot puts zeros after a save (to 512
// bytes) and they come back with it, so they are found from the end, past any zeros, the last first.
// The campaign's ("RFAB" .. "RFAE"), and in a respawn's save the weapons Mason had ("RFKB" .. "RFKE").
constexpr DWORD kBlockStart = 0x42414652;  // "RFAB"
constexpr DWORD kBlockEnd = 0x45414652;    // "RFAE"
constexpr DWORD kKitStart = 0x424B4652;    // "RFKB"
constexpr DWORD kKitEnd = 0x454B4652;      // "RFKE"
constexpr DWORD kBlockVersion = 1;
constexpr int kBlockFrame = 20;            // the bytes around a block's own: 12 before, 8 after
constexpr int kMostPaddingLooked = 4096;
constexpr int kMostBlocks = 4;

// The player's weapons, as guerrilla_gear reads them (its guerrilla_gear.cpp; read here, not shared):
// the radial's slots 0-3 (cdecl 0x7A8290: the record in a slot; a record's +0x18 its weapon's handle),
// a weapon's +0x188 its weapons.xtbl entry (+0 the name; the entries at [0x18F8048], [0x18F804C] of
// them, 0xE0 bytes each, +0x18 the inventory item), its magazine at +0x2E0 (set by 0x7310F0, rounds and
// firing mode), the reserve through the human's ammo store (0x84E8F0; 0x70EF40 reads, 0x70F4F0 adds,
// 0x70F650 takes, each by a weapon and its firing mode), the one in his hands (0x8BE980). A gun is
// given by its inventory item into a slot (0x85B260: the human, the item, rounds 0 for its own, the
// slot, three flags false, as guerrilla_gear's GiveWeapon does), and equipped as the console's
// equip_player_item does (0x85CEE0: the human, the record, 0, 1, 0).
constexpr int kWeaponSlots = 4;
constexpr DWORD kWeaponTable = 0x18F8048, kWeaponTableCount = 0x18F804C, kWeaponEntrySize = 0xE0;
constexpr DWORD kWeaponInfo = 0x188, kInventoryItem = 0x18, kRoundsInMagazine = 0x2E0;
using RecordInSlotFn = BYTE*(__cdecl*)(BYTE* human, int slot);
using RecordInHandsFn = BYTE*(__cdecl*)(BYTE* human);
using ObjectFromHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
using HumansAmmoFn = void*(__fastcall*)(BYTE* human, void* unused);
using RoundsFn = int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int mode);
using ChangeRoundsFn = int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int mode, int rounds);
using SetMagazineFn = void(__fastcall*)(BYTE* weapon, void* unused, int rounds, int mode);
using GiveByItemFn = BYTE*(__cdecl*)(BYTE* human, const void* item, int rounds, int slot, bool, bool, bool);
using EquipFn = void(__cdecl*)(BYTE* human, BYTE* record, int, int, int);
auto const RecordInSlot = reinterpret_cast<RecordInSlotFn>(0x7A8290);
auto const RecordInHands = reinterpret_cast<RecordInHandsFn>(0x8BE980);
auto const ObjectFromHandle = reinterpret_cast<ObjectFromHandleFn>(0x436250);
auto const HumansAmmo = reinterpret_cast<HumansAmmoFn>(0x84E8F0);
auto const ReserveRounds = reinterpret_cast<RoundsFn>(0x70EF40);
auto const AddRounds = reinterpret_cast<ChangeRoundsFn>(0x70F4F0);
auto const TakeRounds = reinterpret_cast<ChangeRoundsFn>(0x70F650);
auto const SetMagazine = reinterpret_cast<SetMagazineFn>(0x7310F0);
auto const GiveByItem = reinterpret_cast<GiveByItemFn>(0x85B260);
auto const Equip = reinterpret_cast<EquipFn>(0x85CEE0);

// What Mason carries: each gun's name, slot, magazine and reserve, and the slot in his hands.
struct KitWeapon {
    char name[48];
    int slot, magazine, reserve;
};
struct Kit {
    int count = 0;
    int inHands = -1;
    KitWeapon weapons[kWeaponSlots] = {};
};
Kit g_aliveKit;        // as of the last gameplay step he was alive in one of these worlds
bool g_aliveKitTaken = false;
Kit g_respawnKit;      // what the respawn's save keeps
bool g_kitPending = false;  // a save just opened carries one: given back at the next gameplay step
Kit g_kitToGive;
// The gun he held, taken in hand again once the game can: the equip (0x85CEE0) does nothing while the
// record's +8 is still empty, as it is for a gun just given (the first try, at once, left the gun the
// game picked in his hands). Tried again every few steps until it is in his hands.
int g_equipSlot = -1;
DWORD g_equipSince = 0, g_equipTried = 0;
constexpr DWORD kEquipRetryMs = 250, kEquipLimitMs = 6000;


// Where Mason can wake after a death (see Respawn): Guerrilla's safehouses, each at its spot, and its
// player starts. From mods\guerrilla_save\respawns.txt (GuerrillaBuilder.exe), else the safehouses'
// own places from guerrilla_hud's safehouses.txt (no district, facing the world's z).
struct Spot {
    bool safehouse = false;
    std::string name;      // a safehouse's name tag
    std::string district;  // a safehouse's district ("" if not known)
    bool usable = true;    // a safehouse: visible and not disabled
    float position[3] = {};
    float orientation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};
std::vector<Spot> g_spots;
bool g_spotsRead = false;

// CONTINUE on the game-over screen asked for a respawn: done at the next gameplay step the world can save.
bool g_respawnPending = false;
bool g_respawnSaving = false;  // its save is the next one made (the campaign is told it follows a death)
DWORD g_respawnAskedAt = 0;
Spot g_respawnSpot;

// GuerrillaSave_ReloadAt asked for a reload at a spot: its save (once the world can take one), the save made with
// our blocks in it, the fade, the world asked to open again (until OpenWorld runs).
enum class ReloadStage { None, Asked, Saving, Fading, Opening };
ReloadStage g_reloadStage = ReloadStage::None;  // set to None by OpenWorld too (the loading thread): under g_blockLock
bool g_reloadOpened = false;  // OpenWorld opened a reload at a spot's save: its first gameplay step looks at the sound
DWORD g_reloadSince = 0;
float g_reloadPosition[3] = {};
float g_reloadOrientation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

std::mutex g_blockLock;  // the world opens on the loading thread; the rest is on the main thread
std::vector<unsigned char> g_blockToAdd;   // taken when the world was saved, added once the save is done
bool g_blockPending = false;
bool g_loadPending = false;                 // the next gameplay step gives the campaign its state:
bool g_loadFresh = false;                   //   (null, 0), or
std::vector<unsigned char> g_blockToLoad;   //   this save's block

std::vector<std::string> g_worlds;

// The save SAVE GAME asked for: being made, or how it came out ("saved" or "failed").
bool g_waitingToSave = false;
bool g_saveStarted = false;  // the game's 0x6D7E20 ran for it and wrote the header and systems
DWORD g_askedAt = 0;
const char* g_saveOutcome = "failed";
unsigned g_savesMade = 0;    // saves SAVE GAME has made since the game started (for guerrilla_hud's saving icon)

BYTE& Byte(DWORD address) { return *reinterpret_cast<BYTE*>(address); }
int Int(DWORD address) { return *reinterpret_cast<const int*>(address); }
void* World() { return reinterpret_cast<void*>(kWorld); }

std::vector<std::string> SplitList(const std::wstring& text) {
    std::vector<std::string> names;
    std::string current;
    for (wchar_t c : text) {
        if (c == L',' || c == L' ' || c == L'\t') {
            if (!current.empty()) names.push_back(current);
            current.clear();
        } else {
            current += static_cast<char>(c);
        }
    }
    if (!current.empty()) names.push_back(current);
    return names;
}

bool InGuerrillaWorld() {
    const char* name = reinterpret_cast<const char*>(kWorldName);
    for (const std::string& world : g_worlds)
        if (_stricmp(world.c_str(), name) == 0) return true;
    return false;
}

bool SaveRunning() {
    return (Byte(kSaveFlags) & (kSaveAsked | kSaveStarted | kSaveBeingMade)) != 0 || Int(kStreamingState) != 0;
}

// Why SAVE GAME can't be offered now, or nullptr when it can. The player must be there and alive, as
// for the campaign's checkpoints ("Skipping checkpoint saving due to the player being dead").
const char* WhyNoSave() {
    if (!InGuerrillaWorld()) return "not in a Guerrilla world";
    if (!Byte(kSavegameOwned)) return "the profile doesn't own its saves (savegame_is_owned)";
    void* player = reinterpret_cast<LocalPlayerFn>(kLocalPlayer)();
    if (!player) return "no player";
    void** vtable = *reinterpret_cast<void***>(player);
    if (reinterpret_cast<IsDeadFn>(vtable[4])(player)) return "the player is dead";
    if (g_waitingToSave || SaveRunning()) return "a save is being made";
    return nullptr;
}

// In a Guerrilla world, a save asked for is kept in memory: never written into the autosave slot.
void KeepSaveInMemory() {
    if (InGuerrillaWorld()) Byte(kSaveFlags2) &= static_cast<BYTE>(~kWriteToAutosave);
}

void __fastcall AskForSave(void* world, void* /*unused*/, bool message, bool b) {
    g_askForSave(world, message, b);
    KeepSaveInMemory();
}

// Whether guerrilla_campaign runs, which keeps the campaign's state (campaign_api.h: its state into a buffer, and back
// from a save's block); without it, saves carry none (said once).
bool CampaignThere() {
    const bool running = guerrilla_campaign::Running();
    if (!running) RFA_LOG_FIRST(1, "guerrilla_campaign is off: saves carry no campaign state");
    return running;
}

// Bytes framed as one of our blocks (see kBlockStart).
void AddFramed(std::vector<unsigned char>& to, DWORD start, DWORD end, const unsigned char* bytes, int length) {
    const DWORD head[3] = {start, kBlockVersion, static_cast<DWORD>(length)};
    const DWORD tail[2] = {static_cast<DWORD>(length), end};
    const size_t at = to.size();
    to.resize(at + kBlockFrame + length);
    memcpy(to.data() + at, head, sizeof(head));
    memcpy(to.data() + at + 12, bytes, length);
    memcpy(to.data() + at + 12 + length, tail, sizeof(tail));
}

// The campaign's state, as it goes into a save; empty if there is none.
std::vector<unsigned char> CampaignState() {
    if (!CampaignThere()) return {};
    const int needed = GuerrillaCampaign_SaveState(nullptr, 0);
    if (needed <= 0) return {};
    std::vector<unsigned char> state(needed);
    const int written = GuerrillaCampaign_SaveState(state.data(), needed);
    if (written <= 0 || written > needed) {
        rfa::Log("GuerrillaCampaign_SaveState wrote %d bytes of the %d it asked for; no campaign state in this save", written,
                 needed);
        return {};
    }
    state.resize(written);
    return state;
}

std::string KitText(const Kit& kit) {
    std::string text;
    for (int i = 0; i < kit.count; ++i) {
        const KitWeapon& w = kit.weapons[i];
        char line[96];
        sprintf_s(line, "%sslot %d %s %d | %d%s", text.empty() ? "" : ", ", w.slot, w.name, w.magazine, w.reserve,
                  w.slot == kit.inHands ? " (in his hands)" : "");
        text += line;
    }
    return text.empty() ? "nothing" : text;
}

// His weapons now (see kWeaponSlots).
Kit TakeKit(BYTE* player) {
    Kit kit;
    void* ammo = HumansAmmo(player, nullptr);
    BYTE* inHands = RecordInHands(player);
    for (int slot = 0; slot < kWeaponSlots; ++slot) {
        BYTE* record = RecordInSlot(player, slot);
        BYTE* weapon = record ? ObjectFromHandle(World(), nullptr, *reinterpret_cast<const DWORD*>(record + kInventoryItem)) : nullptr;
        const BYTE* info = weapon ? *reinterpret_cast<BYTE* const*>(weapon + kWeaponInfo) : nullptr;
        const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
        if (!name || !*name) continue;
        KitWeapon& w = kit.weapons[kit.count++];
        strncpy_s(w.name, name, _TRUNCATE);
        w.slot = slot;
        w.magazine = *reinterpret_cast<const int*>(weapon + kRoundsInMagazine);
        w.reserve = ammo ? ReserveRounds(ammo, nullptr, weapon, 0) : 0;
        if (record == inHands) kit.inHands = slot;
    }
    return kit;
}

// The world is saved here, on the main thread, as it is at this moment: the campaign's state with it.
bool __fastcall MakeSave(void* world, void* /*unused*/) {
    KeepSaveInMemory();
    const bool made = g_makeSave(world);
    if (g_waitingToSave) {
        g_saveStarted = made;
        if (!made) rfa::Log("The world couldn't start the save (0x6D7E20 said no; see the lines above)");
    }
    if (made && InGuerrillaWorld()) {
        std::vector<unsigned char> blocks;
        if (g_respawnSaving) {
            // A respawn's save comes after the death: the death's cost is in this state already, and his
            // weapons are gone from him (a dead human's are dropped): the ones he had alive go with it.
            g_respawnSaving = false;
            if (CampaignThere()) GuerrillaCampaign_RespawnSaved();
            if (g_respawnKit.count > 0)
                AddFramed(blocks, kKitStart, kKitEnd, reinterpret_cast<const unsigned char*>(&g_respawnKit), sizeof(Kit));
        }
        const std::vector<unsigned char> state = CampaignState();
        if (!state.empty()) AddFramed(blocks, kBlockStart, kBlockEnd, state.data(), static_cast<int>(state.size()));
        std::lock_guard<std::mutex> hold(g_blockLock);
        g_blockToAdd = std::move(blocks);
        g_blockPending = !g_blockToAdd.empty();
    }
    return made;
}

// The save in memory's bytes (the world's save buffer makes them if they are missing, as its users do).
BYTE* SaveData(void* world) {
    BYTE* data = *reinterpret_cast<BYTE**>(kSaveBytes);
    if (!data) data = const_cast<BYTE*>(reinterpret_cast<SaveBufferFn>(kSaveBuffer)(world));
    return data;
}

// Once the save is done (the last stream-out job set its size): our blocks after its data. Armageddon
// reads a save's sections by its own list and leaves anything after them, as it does the zeros a slot
// adds; the slot's check sum is taken over all of it.
void AddCampaignBlock() {
    std::lock_guard<std::mutex> hold(g_blockLock);
    if (!g_blockPending || SaveRunning()) return;
    g_blockPending = false;
    const int size = Int(kSavedSize);
    const int room = Int(kSavedRoom);
    const int length = static_cast<int>(g_blockToAdd.size());
    BYTE* data = size > 0 ? SaveData(World()) : nullptr;
    if (!data) {
        rfa::Log("The save came out empty; no campaign state added");
    } else if (size + length > room) {
        rfa::Log("No room for the campaign's state in the save (%d + %d bytes of %d)", size, length, room);
    } else {
        memcpy(data + size, g_blockToAdd.data(), length);
        *reinterpret_cast<int*>(kSavedSize) = size + length;
        rfa::Log("Campaign state added to the save: %d bytes after its %d", length, size);
    }
    g_blockToAdd.clear();
}

// Our blocks in the save in memory (past any zeros a slot added), from the last back: the campaign's
// state and a respawn's weapons, each if it has one. True if it has the campaign's.
bool ReadSaveBlocks(void* world, std::vector<unsigned char>& campaign, bool& hasKit, Kit& kit) {
    hasKit = false;
    bool hasCampaign = false;
    const int size = Int(kSavedSize);
    const BYTE* data = size > 0 ? SaveData(world) : nullptr;
    if (!data) return false;
    int end = size;
    while (end > 0 && data[end - 1] == 0 && size - end < kMostPaddingLooked) --end;
    for (int blocks = 0; blocks < kMostBlocks && end >= kBlockFrame; ++blocks) {
        DWORD tail[2];
        memcpy(tail, data + end - 8, sizeof(tail));
        if ((tail[1] != kBlockEnd && tail[1] != kKitEnd) || tail[0] > static_cast<DWORD>(end - kBlockFrame)) break;
        const int length = static_cast<int>(tail[0]);
        const BYTE* start = data + end - kBlockFrame - length;
        DWORD head[3];
        memcpy(head, start, sizeof(head));
        if (head[1] != kBlockVersion || head[2] != tail[0]) break;
        if (head[0] == kBlockStart && tail[1] == kBlockEnd) {
            campaign.assign(start + 12, start + 12 + length);
            hasCampaign = true;
        } else if (head[0] == kKitStart && tail[1] == kKitEnd && length == sizeof(Kit)) {
            memcpy(&kit, start + 12, sizeof(Kit));
            hasKit = kit.count >= 0 && kit.count <= kWeaponSlots;
        } else {
            break;
        }
        end = static_cast<int>(start - data);
    }
    return hasCampaign;
}

// His weapons given back after a respawn: each into its slot (one already holding the same gun keeps
// it, as the hammer guerrilla_gear puts back in slot 3), its magazine, then its reserve (what's loaded
// counts against the reserve, as guerrilla_gear does it), then the one he held taken in hand.
void GiveKit(BYTE* player, const Kit& kit) {
    void* ammo = HumansAmmo(player, nullptr);
    BYTE* entries = *reinterpret_cast<BYTE* const*>(kWeaponTable);
    const int entryCount = *reinterpret_cast<const int*>(kWeaponTableCount);
    std::string given, missed;
    for (int i = 0; i < kit.count; ++i) {
        const KitWeapon& w = kit.weapons[i];
        const void* item = nullptr;
        for (int e = 0; entries && e < entryCount && !item; ++e) {
            const char* name = *reinterpret_cast<const char* const*>(entries + e * kWeaponEntrySize);
            if (name && _stricmp(name, w.name) == 0) item = *reinterpret_cast<const void* const*>(entries + e * kWeaponEntrySize + kInventoryItem);
        }
        BYTE* record = RecordInSlot(player, w.slot);
        BYTE* weapon = record ? ObjectFromHandle(World(), nullptr, *reinterpret_cast<const DWORD*>(record + kInventoryItem)) : nullptr;
        const BYTE* info = weapon ? *reinterpret_cast<BYTE* const*>(weapon + kWeaponInfo) : nullptr;
        const char* there = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
        if (record && !(there && _stricmp(there, w.name) == 0)) {
            missed += std::string(missed.empty() ? "" : ", ") + w.name + " (slot " + std::to_string(w.slot) + " taken by " + (there ? there : "something") + ")";
            continue;
        }
        if (!record) {
            if (!item || !(record = GiveByItem(player, item, 0, w.slot, false, false, false))) record = RecordInSlot(player, w.slot);
            weapon = record ? ObjectFromHandle(World(), nullptr, *reinterpret_cast<const DWORD*>(record + kInventoryItem)) : nullptr;
            if (!weapon) {
                missed += std::string(missed.empty() ? "" : ", ") + w.name + (item ? " (the game refused it)" : " (not in the game's weapons)");
                continue;
            }
        }
        SetMagazine(weapon, nullptr, w.magazine, 0);
        if (ammo) {
            const int now = ReserveRounds(ammo, nullptr, weapon, 0);
            if (now > w.reserve) TakeRounds(ammo, nullptr, weapon, 0, now - w.reserve);
            else if (now < w.reserve) AddRounds(ammo, nullptr, weapon, 0, w.reserve - now);
        }
        given += std::string(given.empty() ? "" : ", ") + w.name;
    }
    rfa::Log("Respawn: weapons given back: %s; now %s%s%s", given.empty() ? "none" : given.c_str(), KitText(TakeKit(player)).c_str(),
             missed.empty() ? "" : "; not given: ", missed.c_str());
    g_equipSlot = kit.inHands;  // taken in hand by EquipHeldGun
    g_equipSince = GetTickCount();
    g_equipTried = 0;
}

// Each gameplay step after a respawn's weapons were given back: the gun he held put in his hands (see
// g_equipSlot).
void EquipHeldGun() {
    if (g_equipSlot < 0) return;
    BYTE* player = static_cast<BYTE*>(reinterpret_cast<LocalPlayerFn>(kLocalPlayer)());
    BYTE* record = player ? RecordInSlot(player, g_equipSlot) : nullptr;
    const DWORD now = GetTickCount();
    if (record && RecordInHands(player) == record) {
        rfa::Log("Respawn: slot %d's gun is in his hands again (%lu ms after its weapons were given back)", g_equipSlot, now - g_equipSince);
        g_equipSlot = -1;
        return;
    }
    if (!record || now - g_equipSince > kEquipLimitMs) {
        rfa::Log("Respawn: couldn't put slot %d's gun back in his hands (%s)", g_equipSlot, record ? "the game didn't take it in time" : "the slot is empty");
        g_equipSlot = -1;
        return;
    }
    if (now - g_equipTried < kEquipRetryMs) return;
    g_equipTried = now;
    Equip(player, record, 0, 1, 0);
}

std::vector<std::string> Words(const std::string& line, char separator) {
    std::vector<std::string> words;
    std::string word;
    for (char c : line) {
        if (c == separator || (separator == ' ' && c == '\t')) {
            if (!word.empty() || separator != ' ') words.push_back(word);
            word.clear();
        } else if (c != '\r') {
            word += c;
        }
    }
    if (!word.empty() || separator != ' ') words.push_back(word);
    return words;
}

// respawns.txt (GuerrillaBuilder.exe), one line each, separated by spaces:
//   safehouse <name tag> <district, or -; spaces as _> <visible 0/1> <enabled 0/1> <x y z> <9 numbers: its facing, right up forward>
//   start <x y z> <9 numbers>
// Every safehouse starts hidden in Guerrilla's zones; its missions show them (safehouse_set_visible), and
// those aren't ported, so a safehouse counts when it is enabled (Guerrilla too skips the visible test in
// one of its world's modes, World +0x2694 == 1).
// Without it, guerrilla_hud's safehouses.txt: each "shop|x|y|z" (a safehouse's upgrade shop, at its spot).
void ReadSpots() {
    if (g_spotsRead) return;
    g_spotsRead = true;
    char line[512];
    if (FILE* file = _wfopen((rfa::RootDir() + L"\\mods\\guerrilla_save\\respawns.txt").c_str(), L"r")) {
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == '#' || line[0] == ';') continue;
            std::vector<std::string> w = Words(std::string(line, strcspn(line, "\n")), ' ');
            Spot spot;
            size_t at = 0;
            if (w.size() == 17 && w[0] == "safehouse") {
                spot.safehouse = true;
                spot.name = w[1];
                spot.district = w[2] == "-" ? "" : w[2];
                for (char& c : spot.district)
                    if (c == '_') c = ' ';  // "End_Game": territories.xtbl's "End Game"
                spot.usable = w[4] == "1";  // enabled (w[3], visible, isn't tracked yet: see above)
                at = 5;
            } else if (w.size() == 13 && w[0] == "start") {
                at = 1;
            } else {
                continue;
            }
            for (int i = 0; i < 3; ++i) spot.position[i] = static_cast<float>(atof(w[at + i].c_str()));
            for (int i = 0; i < 9; ++i) spot.orientation[i] = static_cast<float>(atof(w[at + 3 + i].c_str()));
            g_spots.push_back(spot);
        }
        fclose(file);
        rfa::Log("Respawns: %u places from mods\\guerrilla_save\\respawns.txt", static_cast<unsigned>(g_spots.size()));
        return;
    }
    if (FILE* file = _wfopen((rfa::RootDir() + L"\\mods\\guerrilla_hud\\safehouses.txt").c_str(), L"r")) {
        while (fgets(line, sizeof(line), file)) {
            std::vector<std::string> w = Words(std::string(line, strcspn(line, "\n")), '|');
            if (w.size() < 4 || w[0] != "shop") continue;
            Spot spot;
            spot.safehouse = true;
            spot.name = "safehouse";
            for (int i = 0; i < 3; ++i) spot.position[i] = static_cast<float>(atof(w[1 + i].c_str()));
            g_spots.push_back(spot);
        }
        fclose(file);
    }
    rfa::Log("Respawns: no mods\\guerrilla_save\\respawns.txt (GuerrillaBuilder.exe); %u safehouses' own places from "
             "guerrilla_hud's safehouses.txt instead", static_cast<unsigned>(g_spots.size()));
}

// Where Mason wakes after dying at `at`, as Guerrilla's save_state_finish puts him (out of a mission):
// safehouse_find_spawn_safehouse, the nearest safehouse (flat, x and z) that is visible and not disabled
// in the district he died in (district_get_from_pos), at its spot (safehouse_get_start_pos_orient: its
// player start, else the nearest one within 50 m, else its own place; the builder works that out); with
// none, the nearest player start. When the districts aren't known (no respawns.txt), the nearest
// safehouse; and if Guerrilla's rule finds nothing here at all, the nearest safehouse anywhere.
const Spot* ChooseSpot(const float* at, std::string& why) {
    std::string district;
    GuerrillaCampaignDistrict info{};
    if (GuerrillaCampaign_District(-1, &info) && info.name) district = info.name;
    auto distance = [&](const Spot& s) {
        const float dx = s.position[0] - at[0], dz = s.position[2] - at[2];
        return dx * dx + dz * dz;
    };
    const Spot *inDistrict = nullptr, *anySafehouse = nullptr, *start = nullptr;
    for (const Spot& s : g_spots) {
        if (!s.safehouse) {
            if (!start || distance(s) < distance(*start)) start = &s;
            continue;
        }
        if (!s.usable) continue;
        if (!anySafehouse || distance(s) < distance(*anySafehouse)) anySafehouse = &s;
        const bool sameDistrict = s.district.empty() || district.empty() || _stricmp(s.district.c_str(), district.c_str()) == 0;
        if (sameDistrict && (!inDistrict || distance(s) < distance(*inDistrict))) inDistrict = &s;
    }
    if (inDistrict) {
        why = "the nearest safehouse in " + (district.empty() ? std::string("the district") : district);
        return inDistrict;
    }
    if (start) {
        why = "no safehouse in " + district + ": the nearest player start";
        return start;
    }
    why = "no safehouse in " + district + " and no player starts: the nearest safehouse";
    return anySafehouse;
}

// Every gameplay step while a respawn waits: once the world can take a save (not streaming zones out, no
// save asked or being made), the save with Mason put at the spot (0x6DA3D0, Guerrilla's
// world::save_and_warp but for the reload), then the game-over screen's own LOAD CHECKPOINT (its fade,
// then the world opened again from that save, 0x6C88B0).
void StepRespawn() {
    if (!g_respawnPending) return;
    const bool busy = Int(kStreamingState) != 0 || (Byte(kSaveFlags) & (kSaveAsked | kSaveBeingMade)) != 0 ||
                      reinterpret_cast<MissionActiveFn>(kMissionActive)();
    if (busy) {
        if (GetTickCount() - g_respawnAskedAt <= kRespawnWaitLimitMs) return;
        rfa::Log("Respawn: the world couldn't take a save for %lu s (flags 0x%02X, streaming %d); the last save is loaded "
                 "instead", kRespawnWaitLimitMs / 1000, Byte(kSaveFlags), Int(kStreamingState));
    } else {
        g_respawnSaving = true;
        reinterpret_cast<SaveAndWarpFn>(kSaveAndWarp)(World(), g_respawnSpot.position, g_respawnSpot.orientation);
        if (!(Byte(kSaveFlags) & kSaveAsked)) {
            g_respawnSaving = false;
            rfa::Log("Respawn: the world didn't take the save (flags 0x%02X); the last save is loaded instead", Byte(kSaveFlags));
        }
    }
    g_respawnPending = false;
    reinterpret_cast<FailureChoiceFn>(kFailureLoadCheckpoint)();
}

// The weapons a respawn's save keeps: those he had alive, as of the last gameplay step he was (a dead human's are
// gone). For CONTINUE (LuaRespawn) and a reload at a spot after a death (StepReload), so that the two can't drift apart.
void TakeRespawnKit() { g_respawnKit = g_aliveKitTaken ? g_aliveKit : Kit{}; }

ReloadStage CurrentReloadStage() {
    std::lock_guard<std::mutex> hold(g_blockLock);
    return g_reloadStage;
}

void SetReloadStage(ReloadStage stage) {
    std::lock_guard<std::mutex> hold(g_blockLock);
    g_reloadStage = stage;
    g_reloadSince = GetTickCount();
}

// Every gameplay step while a reload at a spot waits (GuerrillaSave_ReloadAt, guerrilla_campaign's RETRY): once the
// world can take a save, the save with the player put at the spot (0x6DA3D0, as a respawn's: a respawn's after a
// death); once it is made and our blocks are in it, the game-over screen's fade, then the world asked to open again as
// Armageddon's own save and reload asks it (kAskReload above). Its save and reload at a spot (0x6DA320) asks for both at once, and the
// gameplay step then makes the save and opens the world in the same call (0x542030), maybe before the save's last
// stream-out job and our blocks; hence the steps.
void StepReload() {
    const ReloadStage stage = CurrentReloadStage();
    if (stage == ReloadStage::None) return;
    const DWORD waited = GetTickCount() - g_reloadSince;
    if (!InGuerrillaWorld()) {
        rfa::Log("Reload at a spot: not in a Guerrilla world any more; given up");
        SetReloadStage(ReloadStage::None);
        return;
    }
    if (stage == ReloadStage::Asked) {
        const bool busy = Int(kStreamingState) != 0 || (Byte(kSaveFlags) & (kSaveAsked | kSaveBeingMade)) != 0 ||
                          reinterpret_cast<MissionActiveFn>(kMissionActive)() || g_respawnPending || g_waitingToSave;
        if (busy) {
            if (waited <= kReloadWaitLimitMs) return;
            rfa::Log("Reload at a spot: the world couldn't take a save for %lu s (flags 0x%02X, streaming %d); no reload",
                     kReloadWaitLimitMs / 1000, Byte(kSaveFlags), Int(kStreamingState));
            SetReloadStage(ReloadStage::None);
            return;
        }
        void* player = reinterpret_cast<LocalPlayerFn>(kLocalPlayer)();
        const bool dead = player && reinterpret_cast<IsDeadFn>((*reinterpret_cast<void***>(player))[4])(player);
        if (dead) {  // as a respawn's save: the death's cost is in this state, his weapons are gone from him
            g_respawnSaving = true;
            TakeRespawnKit();
        }
        reinterpret_cast<SaveAndWarpFn>(kSaveAndWarp)(World(), g_reloadPosition, g_reloadOrientation);
        if (!(Byte(kSaveFlags) & kSaveAsked)) {
            g_respawnSaving = false;
            rfa::Log("Reload at a spot: the world didn't take the save (flags 0x%02X); no reload", Byte(kSaveFlags));
            SetReloadStage(ReloadStage::None);
            return;
        }
        const std::string afterDeath = dead ? " (after his death, a respawn's save: keeps his weapons as he had them alive: " + KitText(g_respawnKit) + ")" : "";
        rfa::Log("Reload at a spot: the save asked with the player put at %.1f %.1f %.1f%s", g_reloadPosition[0], g_reloadPosition[1],
                 g_reloadPosition[2], afterDeath.c_str());
        SetReloadStage(ReloadStage::Saving);
        return;
    }
    if (stage == ReloadStage::Saving) {
        bool blockPending;
        {
            std::lock_guard<std::mutex> hold(g_blockLock);
            blockPending = g_blockPending;
        }
        if (SaveRunning() || blockPending) {
            if (waited <= kReloadWaitLimitMs) return;
            rfa::Log("Reload at a spot: the save wasn't done after %lu s (flags 0x%02X 0x%02X, streaming %d); no reload",
                     kReloadWaitLimitMs / 1000, Byte(kSaveFlags), Byte(kSaveFlags2), Int(kStreamingState));
            SetReloadStage(ReloadStage::None);
            return;
        }
        if (Int(kSavedSize) <= 0) {
            rfa::Log("Reload at a spot: the save came out empty (it may not fit in its %d KB); no reload", Int(kSavedRoom) / 1024);
            SetReloadStage(ReloadStage::None);
            return;
        }
        rfa::Log("Reload at a spot: saved in memory (%d KB, %lu ms); the screen fades", Int(kSavedSize) / 1024, waited);
        SetReloadStage(ReloadStage::Fading);
        return;
    }
    if (stage == ReloadStage::Fading) {
        const bool dark = reinterpret_cast<FadeOutFn>(kFadeOut)(false, true, 1.0f);
        if (!dark && waited <= kFadeLimitMs) return;
        SetReloadStage(ReloadStage::Opening);
        reinterpret_cast<AskReloadFn>(kAskReload)(World());
        rfa::Log("Reload at a spot: the world opens again from that save%s", dark ? "" : " (the fade wasn't done after 3 s)");
        return;
    }
    if (stage == ReloadStage::Opening && waited > kReloadWaitLimitMs) {  // OpenWorld ends it; this only if the open never came
        rfa::Log("Reload at a spot: the world didn't open again within %lu s of asking (flags 0x%02X 0x%02X); given up",
                 kReloadWaitLimitMs / 1000, Byte(kSaveFlags), Byte(kSaveFlags2));
        SetReloadStage(ReloadStage::None);
    }
}

// Armageddon's game-over screen raised, as its own 0x578DA0 does (see kGameOverStart).
void RaiseGameOver() {
    void* player = reinterpret_cast<LocalPlayerFn>(kLocalPlayer)();
    if (!player || !reinterpret_cast<IsDeadFn>((*reinterpret_cast<void***>(player))[4])(player)) return;
    CopyText(reinterpret_cast<char*>(kGameOverTitle), reinterpret_cast<const char*>(kGameOverTitleText), kGameOverTitleRoom);
    CopyText(reinterpret_cast<char*>(kGameOverReason), reinterpret_cast<const char*>(kGameOverReasonText), kGameOverReasonRoom);
    RaiseScreen(reinterpret_cast<void*>(kScreenSystem), nullptr, kGameOverScreen);
    RaiseScreen(reinterpret_cast<void*>(kScreenSystem), nullptr, kGameOverScreenToo);
}

// The game-over screen's start (0x578DA0, replaced whole): the moment Guerrilla's gamestate_gp_simulate_frame (RM
// 0x7EDB49) gave a death to the running activity, in place of its death screen (save_and_continue and game state
// 0x10). In one of these worlds guerrilla_campaign is asked first (GuerrillaCampaign_TakePlayerDeath: a Transporter's
// run shows its results, FAILED, with RETRY and EXIT); only when it doesn't take the death does the game-over screen
// come, as before.
void __cdecl GameOverStart() {
    if (InGuerrillaWorld() && CampaignThere() && GuerrillaCampaign_TakePlayerDeath()) {
        rfa::Log("Death: the campaign's running activity takes it (its results screen, Guerrilla's); no game-over screen");
        return;
    }
    RaiseGameOver();
}

// The first gameplay step in a world a reload at a spot opened (its campaign state given back before it, when it had
// some): the reload is over (GuerrillaSave_Reloading answers 0 from here), the campaign is told (GuerrillaCampaign_
// ReloadedAtSpot), and the sound looked at as the load left it. With the game running and the
// sound still paused (0x274552C / 0x274552D set), it is resumed as the game's unpause does (0xB8ABA0(1, 1)): 2026-11-19
// 20:54, after the retry's reload it stayed off until the pause menu was opened and closed.
void FollowSoundAfterReload() {
    {
        std::lock_guard<std::mutex> hold(g_blockLock);
        if (!g_reloadOpened) return;
        g_reloadOpened = false;
    }
    // The campaign told at once (a retry's start set up again): a save carries its state only when it has some, so the
    // state's being handed back can't be the sign (2026-11-19 21:26, a fresh campaign's first retry: no state, no sign).
    if (CampaignThere()) GuerrillaCampaign_ReloadedAtSpot();
    const int a = Byte(kSoundPausedA), b = Byte(kSoundPausedB);
    const bool gamePaused = reinterpret_cast<MissionActiveFn>(kGamePaused)();
    if ((a || b) && !gamePaused) {
        ResumeSound(true, true);
        rfa::Log("Reload at a spot: the world runs again, but its sound was left paused (%d %d); resumed as the game's unpause does "
                 "(now %d %d)", a, b, Byte(kSoundPausedA), Byte(kSoundPausedB));
    } else {
        rfa::Log("Reload at a spot: the world runs again; its sound %s (%d %d), the game %s", a || b ? "paused" : "playing", a, b,
                 gamePaused ? "paused" : "running");
    }
}

// Each gameplay step in one of these worlds while Mason is alive: his weapons as they are, for a respawn
// (a dead human's are gone by the time the game-over screen comes).
void NoteKit() {
    if (!InGuerrillaWorld()) return;
    BYTE* player = static_cast<BYTE*>(reinterpret_cast<LocalPlayerFn>(kLocalPlayer)());
    if (!player) return;
    void** vtable = *reinterpret_cast<void***>(player);
    if (reinterpret_cast<IsDeadFn>(vtable[4])(player)) return;
    g_aliveKit = TakeKit(player);
    g_aliveKitTaken = true;
}

// The first gameplay step after one of these worlds opened gives the campaign its state, and a
// respawn's save his weapons, before the step runs; every step, a save's blocks are added once the save
// is done, and his weapons noted while he is alive.
void __cdecl GameplayStep() {
    StepRespawn();
    StepReload();
    bool load = false, fresh = false, giveKit = false;
    std::vector<unsigned char> block;
    Kit kit;
    {
        std::lock_guard<std::mutex> hold(g_blockLock);
        if (g_loadPending) {
            g_loadPending = false;
            load = true;
            fresh = g_loadFresh;
            block.swap(g_blockToLoad);
        }
        if (g_kitPending) {
            g_kitPending = false;
            giveKit = true;
            kit = g_kitToGive;
        }
    }
    if (giveKit && InGuerrillaWorld())
        if (BYTE* player = static_cast<BYTE*>(reinterpret_cast<LocalPlayerFn>(kLocalPlayer)())) GiveKit(player, kit);
    if (load && InGuerrillaWorld()) {
        if (CampaignThere()) {
            if (fresh) {
                GuerrillaCampaign_LoadState(nullptr, 0);
                rfa::Log("Campaign state: a fresh start (GuerrillaCampaign_LoadState(null, 0))");
            } else {
                GuerrillaCampaign_LoadState(block.data(), static_cast<int>(block.size()));
                rfa::Log("Campaign state: the save's %u bytes given back (GuerrillaCampaign_LoadState)",
                         static_cast<unsigned>(block.size()));
            }
        }
    }
    g_gameplayStep();
    AddCampaignBlock();
    EquipHeldGun();
    NoteKit();
    FollowSoundAfterReload();
}

// Whether a level's name ("rfg_07_11") is one of the Guerrilla worlds'.
bool LevelIsOurs(const char* level) {
    for (const std::string& world : g_worlds) {
        const size_t n = world.size();
        if (_strnicmp(level, world.c_str(), n) == 0 && (level[n] == 0 || level[n] == '_')) return true;
    }
    return false;
}

// The level the save in memory was made in, from its header: 0x6D7E20 writes "RFGS", the version, the
// mode, then the name of the zone the player stood in as a string (2 bytes its length, the letters, a 0;
// a save made in rfg: 09 00 "rfg_07_09" 00). "" when there is no such save.
std::string SavedLevel(void* world) {
    const int size = Int(kSavedSize);
    if (size < 16) return "";
    const BYTE* data = *reinterpret_cast<const BYTE* const*>(kSaveBytes);
    if (!data) data = reinterpret_cast<SaveBufferFn>(kSaveBuffer)(world);  // as the world's open does next
    if (!data) return "";
    if (*reinterpret_cast<const DWORD*>(data) != kSaveMagic || *reinterpret_cast<const int*>(data + 4) != kSaveVersion)
        return "";
    const int length = *reinterpret_cast<const unsigned short*>(data + 12);
    if (length == 0 || length >= 64 || 14 + length > size) return "";
    return std::string(reinterpret_cast<const char*>(data + 14), length);
}

// A world opened from a save in memory (LOAD CHECKPOINT, or a slot, which the game reads into memory
// first) comes with an empty name: the save names its level, which the open reads only after it has
// asked for the zones' size by that empty name (0x6C95DD: 0x6C0D60(name, &size)). guerrilla_world gives
// the Guerrilla worlds Guerrilla's zone size by their names, so a save of one got Armageddon's 255 m:
// the streaming grids too small for its zones, and the open stopped in init_territory_streaming_resources
// (0x6DB0C0 reading a grid cell its zone wasn't put in; the load hung). Here such a save opens with its
// level's name, which the open uses for nothing else (it would read a save file only for a name ending
// in rfgs_pc).
//
// It also sets what the campaign is given at the first gameplay step: a world started fresh, nothing
// (null, 0); a save with a campaign block, that block; a save without one (made before saves had it),
// no call at all.
int __fastcall OpenWorld(void* world, void* /*unused*/, const char* name, bool fresh, bool b) {
    {
        std::lock_guard<std::mutex> hold(g_blockLock);
        if (g_reloadStage == ReloadStage::Opening) {  // a reload at a spot: done once its first gameplay step has run
            g_reloadStage = ReloadStage::None;
            g_reloadOpened = true;
        }
        g_loadPending = false;
        g_blockToLoad.clear();
        g_kitPending = false;
        g_equipSlot = -1;
        g_aliveKitTaken = false;  // noted again once he is alive in the world opened
        if (fresh && name && LevelIsOurs(name)) {
            g_loadPending = true;
            g_loadFresh = true;
        }
    }
    if (!fresh && name && name[0] == 0) {
        const std::string level = SavedLevel(world);
        if (!level.empty() && LevelIsOurs(level.c_str())) {
            strncpy_s(g_savedLevel, level.c_str(), _TRUNCATE);
            // A save the world is opened from at once (a respawn's, or Armageddon's save and reload) may be
            // done without a gameplay step since: its campaign block goes in now.
            AddCampaignBlock();
            std::vector<unsigned char> block;
            bool hasKit = false;
            Kit kit;
            const bool hasBlock = ReadSaveBlocks(world, block, hasKit, kit);
            rfa::Log("Opening a save made in %s: opened by that name, so its zones get the Guerrilla world's size; %s%s",
                     g_savedLevel, hasBlock ? "it has the campaign's state" : "it has no campaign state",
                     hasKit ? (", and a respawn's weapons: " + KitText(kit)).c_str() : "");
            {
                std::lock_guard<std::mutex> hold(g_blockLock);
                if (hasBlock) {
                    g_loadPending = true;
                    g_loadFresh = false;
                    g_blockToLoad = std::move(block);
                }
                g_kitPending = hasKit;
                if (hasKit) g_kitToGive = kit;
            }
            return g_openWorld(world, g_savedLevel, fresh, b);
        }
    }
    return g_openWorld(world, name, fresh, b);
}

// How the save SAVE GAME asked for stands: "saving" while the world makes it, then "saved" or "failed".
const char* SaveState() {
    if (!g_waitingToSave) return g_saveOutcome;
    const DWORD waited = GetTickCount() - g_askedAt;
    bool blockPending;
    {
        std::lock_guard<std::mutex> hold(g_blockLock);
        blockPending = g_blockPending;
    }
    if (SaveRunning() || blockPending) {  // and until the campaign's state is in it
        if (waited <= kSaveWaitLimitMs) return "saving";
        rfa::Log("The save wasn't done after %lu s (flags 0x%02X 0x%02X, streaming %d); stopped waiting", waited / 1000,
                 Byte(kSaveFlags), Byte(kSaveFlags2), Int(kStreamingState));
    } else if (!g_saveStarted) {
        rfa::Log("The world didn't make the save (flags 0x%02X 0x%02X)", Byte(kSaveFlags), Byte(kSaveFlags2));
    } else if (Int(kSavedSize) <= 0) {
        rfa::Log("The world's save came out empty (it may not fit in its %d KB)", Int(kSavedRoom) / 1024);
    } else {
        rfa::Log("Saved in memory: %d KB of %d KB, in %lu ms; the save screen opens", Int(kSavedSize) / 1024,
                 Int(kSavedRoom) / 1024, waited);
        g_saveOutcome = "saved";
        g_waitingToSave = false;
        ++g_savesMade;
        return g_saveOutcome;
    }
    g_saveOutcome = "failed";
    g_waitingToSave = false;
    return g_saveOutcome;
}

// Lua: rfg_save_can_save(): in a Guerrilla world, true when SAVE GAME can save the moment you are in,
// false when not now; elsewhere nothing (nil), and the pause menu offers Armageddon's own SAVE GAME.
int __cdecl LuaCanSave(rfa::game::lua_State* L) {
    if (!InGuerrillaWorld()) return 0;
    const char* why = WhyNoSave();
    if (why) RFA_LOG_FIRST(20, "SAVE GAME not offered: %s", why);
    rfa::game::lua_pushboolean(L, why == nullptr);
    return 1;
}

// Lua: rfg_save_game() asks the world for a save of the moment you are in. True when asked.
int __cdecl LuaSaveGame(rfa::game::lua_State* L) {
    const char* why = WhyNoSave();
    if (why) {
        rfa::Log("SAVE GAME: no save (%s)", why);
        rfa::game::lua_pushboolean(L, 0);
        return 1;
    }
    g_waitingToSave = true;
    g_saveStarted = false;
    g_askedAt = GetTickCount();
    reinterpret_cast<AskForSaveFn>(kAskForSave)(World(), false, false);
    rfa::Log("SAVE GAME: asked the world for a save (flags 0x%02X 0x%02X)", Byte(kSaveFlags), Byte(kSaveFlags2));
    rfa::game::lua_pushboolean(L, 1);
    return 1;
}

// Lua: rfg_respawn_available(): in a Guerrilla world, whether the game-over screen's CONTINUE can wake
// Mason at a safehouse (true), or not (false: it offers LOAD CHECKPOINT as before); elsewhere nil.
int __cdecl LuaRespawnAvailable(rfa::game::lua_State* L) {
    if (!InGuerrillaWorld()) return 0;
    ReadSpots();
    rfa::game::lua_pushboolean(L, !g_spots.empty());
    return 1;
}

// Lua: rfg_respawn(): CONTINUE on the game-over screen, Guerrilla's: the world saved as it is with Mason
// at the safehouse Guerrilla would wake him at, then opened again from that save (at the next gameplay
// step the world can save). False when there is nowhere to put him.
int __cdecl LuaRespawn(rfa::game::lua_State* L) {
    ReadSpots();
    void* player = reinterpret_cast<LocalPlayerFn>(kLocalPlayer)();
    float at[3] = {};
    if (player) memcpy(at, static_cast<const BYTE*>(player) + 4, sizeof(at));  // where he is (the object's +4)
    std::string why;
    const Spot* spot = InGuerrillaWorld() ? ChooseSpot(at, why) : nullptr;
    if (!spot) {
        rfa::Log("Respawn: nowhere to wake the player; the last save is loaded instead");
        rfa::game::lua_pushboolean(L, 0);
        return 1;
    }
    g_respawnSpot = *spot;
    g_respawnPending = true;
    g_respawnAskedAt = GetTickCount();
    TakeRespawnKit();
    rfa::Log("Respawn: died at %.0f %.0f %.0f; wakes at %s (%s) %.0f %.0f %.0f; keeps his weapons as he had them alive: %s",
             at[0], at[1], at[2], spot->name.c_str(), why.c_str(), spot->position[0], spot->position[1], spot->position[2],
             KitText(g_respawnKit).c_str());
    rfa::game::lua_pushboolean(L, 1);
    return 1;
}

// Lua: rfg_save_state() gives "saving", "saved" or "failed" for the save rfg_save_game asked for.
int __cdecl LuaSaveState(rfa::game::lua_State* L) {
    rfa::game::lua_pushstring(L, SaveState());
    return 1;
}

// The first bytes of each hooked function, as in the supported build.
bool Starts(DWORD address, std::initializer_list<BYTE> bytes) {
    const BYTE* code = reinterpret_cast<const BYTE*>(address);
    for (BYTE b : bytes)
        if (*code++ != b) return false;
    return true;
}

}  // namespace

// For guerrilla_hud, which shows Guerrilla's saving icon while a save is made and a moment after: 1 while
// SAVE GAME's save is being made in a Guerrilla world, else 0; and through savesMade (if given) how many
// saves it has made since the game started.
int GuerrillaSave_Saving(unsigned* savesMade) {
    if (!guerrilla_save::Running()) {
        if (savesMade) *savesMade = 0;
        return 0;
    }
    if (savesMade) *savesMade = g_savesMade;
    return InGuerrillaWorld() && (g_waitingToSave || SaveRunning()) ? 1 : 0;
}

// For guerrilla_campaign (a Transporter's RETRY): the world saved with the player at a spot, then opened again from
// that save; the steps are StepReload's. False when it can't be asked now.
bool GuerrillaSave_ReloadAt(const float* position, const float* orientation) {
    if (!guerrilla_save::Running() || !g_gameplayStep || !position || !orientation) return false;
    if (!InGuerrillaWorld()) {
        rfa::Log("Reload at a spot: not in a Guerrilla world; not asked");
        return false;
    }
    if (CurrentReloadStage() != ReloadStage::None || g_respawnPending) {
        rfa::Log("Reload at a spot: a %s already waits; not asked", g_respawnPending ? "respawn" : "reload");
        return false;
    }
    memcpy(g_reloadPosition, position, sizeof(g_reloadPosition));
    memcpy(g_reloadOrientation, orientation, sizeof(g_reloadOrientation));
    SetReloadStage(ReloadStage::Asked);
    rfa::Log("Reload at a spot: asked (%.1f %.1f %.1f); saved at the next gameplay step the world can take a save", position[0], position[1],
             position[2]);
    return true;
}

int GuerrillaSave_Reloading() {
    if (!guerrilla_save::Running()) return 0;
    bool opened;
    {
        std::lock_guard<std::mutex> hold(g_blockLock);
        opened = g_reloadOpened;
    }
    return CurrentReloadStage() != ReloadStage::None || opened ? 1 : 0;
}

// For guerrilla_campaign, after an activity's EXIT with the player dead (Guerrilla's activity-complete screen then called
// world::save_and_reload, which wakes him at the nearest safehouse): the spot CONTINUE would pick (ChooseSpot), through
// the reload at a spot (a respawn's save: his weapons kept). With nowhere to put him, or no reload to be had, the
// game-over screen instead. True when the reload was asked.
bool GuerrillaSave_Respawn() {
    if (!guerrilla_save::Running() || !g_gameplayStep || !InGuerrillaWorld()) return false;
    ReadSpots();
    void* player = reinterpret_cast<LocalPlayerFn>(kLocalPlayer)();
    float at[3] = {};
    if (player) memcpy(at, static_cast<const BYTE*>(player) + 4, sizeof(at));  // where he is (the object's +4)
    std::string why;
    const Spot* spot = ChooseSpot(at, why);
    if (spot && GuerrillaSave_ReloadAt(spot->position, spot->orientation)) {
        rfa::Log("Respawn after an activity: died at %.0f %.0f %.0f; wakes at %s (%s) %.0f %.0f %.0f", at[0], at[1], at[2], spot->name.c_str(),
                 why.c_str(), spot->position[0], spot->position[1], spot->position[2]);
        return true;
    }
    rfa::Log("Respawn after an activity: %s; the game-over screen instead", spot ? "the reload couldn't be asked" : "nowhere to wake him");
    RaiseGameOver();
    return false;
}

// For guerrilla_campaign, when its results screen over a dead player can't come up: Armageddon's game-over screen now.
void GuerrillaSave_GameOver() {
    if (!guerrilla_save::Running()) return;
    rfa::Log("Death: the campaign gives it back; the game-over screen");
    RaiseGameOver();
}

void guerrilla_save::RegisterLua(rfa::game::lua_State* L) {
    rfa::game::lua_register(L, "rfg_save_can_save", LuaCanSave);
    rfa::game::lua_register(L, "rfg_save_game", LuaSaveGame);
    rfa::game::lua_register(L, "rfg_save_state", LuaSaveState);
    rfa::game::lua_register(L, "rfg_respawn_available", LuaRespawnAvailable);
    rfa::game::lua_register(L, "rfg_respawn", LuaRespawn);
}

void guerrilla_save::Init() {
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    g_worlds = SplitList(rfa::IniString(L"GuerrillaSave", L"Worlds", L"rfg"));
    if (!Starts(kAskForSave, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08}) ||
        !Starts(kMakeSave, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00}) ||
        !Starts(kOpenWorld, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xBC, 0x01, 0x00, 0x00}) ||
        !Starts(kGameplayStep, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30})) {
        rfa::Log("The game's save functions aren't as expected; saving is left as the game has it");
        return;
    }
    g_askForSave = reinterpret_cast<AskForSaveFn>(
        rfa::HookFunction(kAskForSave, kAskForSavePrologue, reinterpret_cast<const void*>(&AskForSave)));
    g_makeSave = reinterpret_cast<MakeSaveFn>(
        rfa::HookFunction(kMakeSave, kMakeSavePrologue, reinterpret_cast<const void*>(&MakeSave)));
    g_openWorld = reinterpret_cast<OpenWorldFn>(
        rfa::HookFunction(kOpenWorld, kOpenWorldPrologue, reinterpret_cast<const void*>(&OpenWorld)));
    g_gameplayStep = reinterpret_cast<GameplayStepFn>(
        rfa::HookFunction(kGameplayStep, kGameplayStepPrologue, reinterpret_cast<const void*>(&GameplayStep)));
    if (!g_askForSave || !g_makeSave || !g_openWorld || !g_gameplayStep) {
        rfa::Log("Could not hook the game's save functions; saving in the Guerrilla worlds is off");
        return;
    }
    // The game-over screen's start, replaced whole (GameOverStart): its code as the game's, the campaign asked first.
    if (!Starts(kGameOverStart, {0x55, 0x8B, 0xEC, 0x51, 0xE8}) || !rfa::ReplaceFunction(kGameOverStart, reinterpret_cast<const void*>(&GameOverStart)))
        rfa::Log("The game-over screen's start isn't as expected; a death in an activity raises it as the game does");
    rfa::Log("Saving in the Guerrilla worlds: SAVE GAME in the pause menu saves the moment you are in; saves stay in "
             "memory until you pick a slot (never written into the campaign's autosave slot)");
}
