// Guerrilla's alert orders: at yellow and orange the EDF send squads already near the player at him
// (alert_orders.cpp).
#pragma once
#include "campaign.h"

namespace campaign::alert_orders {

void Init();   // the setting ([GuerrillaCampaign] AlertOrders=)
void Enter();  // a world starts: no orders; finds guerrilla_people's leash call
void Frame();  // each frame of play

}  // namespace campaign::alert_orders
