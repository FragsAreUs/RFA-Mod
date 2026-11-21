// The campaign's state for the other mods: the districts for guerrilla_hud's control and morale
// meters, and the whole state for guerrilla_save, so it goes with each game save.
//
// The state is the campaign's save (mods\guerrilla_campaign\campaign_save.ini: the districts' control,
// morale and liberation, the tech and progression levels, salvage, each demolitions master node's
// progress, the buildings already counted). Every change is written there as it happens, so a game
// save takes its bytes as they are, and loading one puts them back and reads them again.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "activities.h"
#include "activity_base.h"
#include "district_sources.h"
#include "liberation.h"
#include "save.h"
#include <string.h>

// (The district's struct, GuerrillaCampaignDistrict, is in campaign_api.h.)

// The district (-1: the player's) for the HUD's meters: 1 and filled in, or 0 (none there).
int GuerrillaCampaign_District(int which, GuerrillaCampaignDistrict* info) {
    using namespace campaign;
    if (!info || !guerrilla_campaign::Running()) return 0;
    const int index = which < 0 ? liberation::PlayerDistrict() : which;
    const liberation::District* district = liberation::GetDistrict(index);
    if (!district) return 0;
    info->name = district->name.c_str();
    info->index = district->index;
    info->control = district->control;
    info->controlMax = district->controlMax;
    info->controlBefore = district->controlBefore;
    info->morale = district->morale;
    info->moraleMax = district->moraleMax;
    info->moraleBefore = district->moraleBefore;
    info->effectiveMorale = liberation::EffectiveMoralePercent(index);
    info->liberated = district->liberated ? 1 : 0;
    info->controlChangedAt = district->controlChangedAt;
    info->moraleChangedAt = district->moraleChangedAt;
    info->changes = district->changes;
    return 1;
}

// For guerrilla_people's squad levels: a team's progression level, Guerrilla's get_progression_level (RM 0x78CC70):
// 0 guerrillas and 2 civilians the tech level (0-5), 1 the EDF's progression (0-7, at least the player's district's
// min_progression_edf), 3 the Marauders' (0-7). Level n is squad_level_(n+1). -1 while the campaign's systems rest.
int GuerrillaCampaign_ProgressionLevel(int team) {
    using namespace campaign;
    if (!guerrilla_campaign::Running() || !Playing()) return -1;
    switch (team) {
        case 1: return liberation::EdfProgressionLevel();
        case 3: return liberation::MarauderProgressionLevel();
        default: return liberation::TechLevel();
    }
}

// For guerrilla_gear: whether the district of that name (Guerrilla's, as upgrades.xtbl's District_Liberated names
// it: "Parker", "Dust", ...) is liberated: 1 yes, 0 not yet, -1 no such district.
int GuerrillaCampaign_DistrictLiberated(const char* name) {
    using namespace campaign;
    if (!guerrilla_campaign::Running()) return -1;
    const liberation::District* district = name ? liberation::GetDistrict(liberation::DistrictNamed(name)) : nullptr;
    return !district ? -1 : district->liberated ? 1 : 0;
}

// For guerrilla_save, when it makes a save of one of our worlds: the campaign's state into buffer (room
// bytes); the bytes written, or the bytes needed when buffer is null (or too small).
int GuerrillaCampaign_SaveState(unsigned char* buffer, int room) {
    if (!guerrilla_campaign::Running()) return 0;
    const std::string bytes = campaign::save::Bytes();
    const int size = static_cast<int>(bytes.size());
    if (!buffer || room < size) return size;
    memcpy(buffer, bytes.data(), bytes.size());
    rfa::LogDetail("Campaign state: %d bytes given to the game's save", size);
    return size;
}

// For guerrilla_save, when a save of one of our worlds is opened: the state it carried (null, 0: a fresh
// world, back to Guerrilla's starting values), read again by every system.
void GuerrillaCampaign_LoadState(const unsigned char* data, int size) {
    using namespace campaign;
    if (!guerrilla_campaign::Running()) return;
    save::Replace(data, size);
    liberation::ReloadState();
    activities::ReloadProgress();
    district_sources::ReloadState();
    rfa::LogDetail(data && size > 0 ? "Campaign state: %d bytes from the game's save" : "Campaign state: a fresh campaign (%d bytes)", size);
}
