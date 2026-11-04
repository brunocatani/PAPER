#pragma once

#include <PAPER/Provider.h>

#include "animation_evidence/AnimationPreharvestPolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace paper::bolt_hold_policy
{
    // The action's bones on the game's own weapon skeleton: the bolt and the
    // helper bones animators use for what moves with it (a tilting barrel, a
    // slide stop, a bolt catch). A gun that animates its bolt under another name
    // is not covered. The bolt comes first: a clip without it is not read.
    inline constexpr std::array<const char*, 10> kActionBoneNames{
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

    // Samples land 17 ms apart, and a fire stroke lasts about 90 ms.
    inline constexpr float kClipSamplesPerSecond = 60.0f;
    // A clip must also show the bolt where the idle clip keeps it. An additive
    // layer's track is a delta, and a paired kill's bolt sits where another gun
    // has it: both look parked open at a position this gun's bolt never takes.
    inline constexpr float kClipClosedGameUnits = 0.1f;

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

    // The read order. Idle shows the action closed, a reload may park the bolt and
    // a shot shows its stroke. Any other clip is read only when none of these moves
    // the bolt, because a gun's roughly 90 clips take about 8 s at the sampling budget.
    enum class ClipKind : std::uint32_t
    {
        Idle,
        Reload,
        Fire,
        Other,
    };

    // `pattern` is lower case.
    [[nodiscard]] constexpr bool matchesAt(
        const std::string_view text,
        const std::string_view pattern,
        const std::size_t start) noexcept
    {
        if (start + pattern.size() > text.size()) {
            return false;
        }
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            if (animation_preharvest_policy::asciiLower(text[start + index]) != pattern[index]) {
                return false;
            }
        }
        return true;
    }

    // Judged on the file name without its extension: a folder can be named after
    // a mod's reloads, and a graph-bound clip has only a name.
    [[nodiscard]] constexpr ClipKind classifyClip(const std::string_view animationPath) noexcept
    {
        const auto separator = animationPath.find_last_of("/\\");
        auto stem = separator == std::string_view::npos ? animationPath : animationPath.substr(separator + 1);
        const auto extension = stem.rfind('.');
        if (extension != std::string_view::npos) {
            stem = stem.substr(0, extension);
        }

        constexpr std::array<std::string_view, 3> kIdleStems{ "wpnidleready", "wpnidle", "wpnidlesighted" };
        for (const auto idle : kIdleStems) {
            if (stem.size() == idle.size() && matchesAt(stem, idle, 0)) {
                return ClipKind::Idle;
            }
        }
        for (std::size_t start = 0; start < stem.size(); ++start) {
            if (matchesAt(stem, "reload", start)) {
                return ClipKind::Reload;
            }
        }
        return matchesAt(stem, "wpnfire", 0) ? ClipKind::Fire : ClipKind::Other;
    }

    // A manual action stays where the hand left it. Every other gun is judged by
    // its own clips: if none of them opens its bolt there is no pose to hold.
    [[nodiscard]] constexpr bool familyHoldsOpen(const std::uint64_t familyFlags) noexcept
    {
        return (familyFlags & static_cast<std::uint64_t>(api::WeaponFamilyFlag::ManualCycle)) == 0;
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
