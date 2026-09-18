#pragma once
#include <ROCK/Core.h>
#include <ROCK/Hands.h>
#include <ROCK/Grab.h>
#include <ROCK/Weapon.h>
#include <ROCK/WeaponParts.h>
#include <ROCK/Animation.h>
#include <ROCK/Diagnostics.h>

namespace paper {
    // PAPER's frame-local native scene witness. Only the base value record is
    // passed to ROCK; PAPER resolves the node through its own native context.
    struct RockWeaponGripState : rock::api::weapon::EquippedWeaponGripStateV1 {
        std::uintptr_t weaponNode{};
    };
}
