#pragma once
#include <ROCK/Core.h>
#include <ROCK/Hands.h>
#include <ROCK/Grab.h>
#include <ROCK/Weapon.h>
#include <ROCK/WeaponParts.h>
#include <ROCK/Animation.h>

namespace paper {
    // PAPER animates the primary weapon: ROCK weapon slot 0, the only slot
    // whose grip, parts and authored poses ROCK publishes.
    inline constexpr std::uint32_t kRockWeaponSlot = 0;

    // PAPER's frame-local native scene witness. Only the base value record is
    // passed to ROCK; PAPER resolves the node through its own native context.
    struct RockWeaponGripState : rock::api::weapon::Grip {
        std::uintptr_t weaponNode{};
    };

    // The primary weapon's published state and hand assignment, and the
    // handling authority lease another mod may hold over it.
    struct RockWeaponHandling {
        rock::api::weapon::WeaponState state{};
        rock::api::weapon::HandlingAuthority authority{};
    };
}
