#pragma once

#include <PAPER/Provider.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace paper::bolt_hold_policy
{
    // The action's bones on the game's own weapon skeleton: the bolt and the
    // helper bones animators use for what moves with it (a tilting barrel, a
    // slide stop, a bolt catch). A gun that animates its bolt under another name
    // is not covered.
    inline constexpr std::array<std::string_view, 10> kActionBoneNames{
        "WeaponBolt",
        "WeaponExtra1",
        "WeaponExtra2",
        "WeaponExtra3",
        "WeaponExtra4",
        "WeaponExtra5",
        "WeaponExtra6",
        "WeaponExtra7",
        "WeaponExtra8",
        "WeaponExtra9",
    };
    inline constexpr std::size_t kBoltBone = 0;

    // Captured reload dwells: Glock 19x 3.03 gu, SA58 FAL 5.53, No Heroics 5.92.
    // Less than this is a clip's small shake, not an open bolt.
    inline constexpr float kMinimumOpenTravelGameUnits = 1.0f;
    // A bolt flies back in about a third of a Glock's 90 ms fire stroke.
    inline constexpr float kOpenSeconds = 0.05f;
    // The bolt must sit still this long, outside a reload, to count as closed.
    inline constexpr std::uint32_t kRestStillFrames = 10;
    inline constexpr float kRestStillGameUnits = 0.01f;

    // A clip's dwell is the bolt parked open: the animator's locked-back pose.
    // Parts get this long to arrive with the bolt, and those that then keep
    // still until the commit belong to the pose. A fire stroke never lasts this
    // long, and a magazine prop moves through it.
    inline constexpr float kDwellSettleSeconds = 0.1f;
    inline constexpr float kDwellCommitSeconds = 0.5f;
    inline constexpr float kDwellBoltDriftGameUnits = 0.05f;
    inline constexpr float kPartStillGameUnits = 0.05f;
    inline constexpr float kPartStillDegrees = 0.5f;
    // Captured parts: Glock barrel 0.45 gu and 3.1 degrees, slide stop 0.11 gu.
    inline constexpr float kPartMinimumMoveGameUnits = 0.02f;
    inline constexpr float kPartMinimumTurnDegrees = 0.25f;
    // A part further from rest than the bolt, or turned this far, is a prop.
    inline constexpr float kPartMaximumTurnDegrees = 45.0f;

    [[nodiscard]] constexpr bool partBelongsToPose(
        const float moveGameUnits,
        const float turnDegrees,
        const float boltTravelGameUnits) noexcept
    {
        return (moveGameUnits >= kPartMinimumMoveGameUnits || turnDegrees >= kPartMinimumTurnDegrees) &&
               moveGameUnits <= boltTravelGameUnits && turnDegrees <= kPartMaximumTurnDegrees;
    }

    [[nodiscard]] constexpr std::uint64_t familyBits(std::initializer_list<api::WeaponFamilyFlag> flags) noexcept
    {
        std::uint64_t bits = 0;
        for (const auto flag : flags) {
            bits |= static_cast<std::uint64_t>(flag);
        }
        return bits;
    }

    // A manual action stays where the hand left it, and a gun fed from a belt,
    // tube or the shooter's hand has no follower to catch the bolt. Energy and
    // heavy weapons have no bolt to hold.
    inline constexpr std::uint64_t kExcludedFamilies = familyBits({
        api::WeaponFamilyFlag::ManualCycle,
        api::WeaponFamilyFlag::Revolver,
        api::WeaponFamilyFlag::BoltAction,
        api::WeaponFamilyFlag::LeverAction,
        api::WeaponFamilyFlag::PumpAction,
        api::WeaponFamilyFlag::BreakAction,
        api::WeaponFamilyFlag::CylinderFed,
        api::WeaponFamilyFlag::BeltFed,
        api::WeaponFamilyFlag::TubeFed,
        api::WeaponFamilyFlag::InternalFeed,
        api::WeaponFamilyFlag::SingleLoad,
        api::WeaponFamilyFlag::ShellFed,
        api::WeaponFamilyFlag::LooseRoundFed,
        api::WeaponFamilyFlag::EnergyWeapon,
        api::WeaponFamilyFlag::LaserCellFed,
        api::WeaponFamilyFlag::HeavyWeapon,
        api::WeaponFamilyFlag::Launcher,
        api::WeaponFamilyFlag::MeleeWeapon,
        api::WeaponFamilyFlag::ThrownWeapon,
    });

    [[nodiscard]] constexpr bool familyHoldsOpen(const std::uint64_t familyFlags) noexcept
    {
        return (familyFlags & static_cast<std::uint64_t>(api::WeaponFamilyFlag::MagazineFed)) != 0 &&
               (familyFlags & kExcludedFamilies) == 0;
    }

    // Opens at the bolt's own speed; closes over the configured blend, or at
    // once when that is 0.
    [[nodiscard]] constexpr float advanceHoldWeight(
        const float weight,
        const bool hold,
        const float deltaSeconds,
        const float closeSeconds) noexcept
    {
        const float dt = deltaSeconds > 0.0f ? deltaSeconds : 0.0f;
        if (hold) {
            return (std::min)(1.0f, weight + dt / kOpenSeconds);
        }
        return closeSeconds > 0.0f ? (std::max)(0.0f, weight - dt / closeSeconds) : 0.0f;
    }

    [[nodiscard]] constexpr float easeHoldWeight(const float weight) noexcept
    {
        return weight * weight * (3.0f - 2.0f * weight);
    }
}
