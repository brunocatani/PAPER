#pragma once

#include "reload_control/BoltHoldPolicy.h"

#include <RE/NetImmerse/NiTransform.h>

#include <array>
#include <cstdint>

namespace paper
{
    class RockApiClient;
}

namespace paper::bolt_hold
{
    struct FrameInput
    {
        float deltaSeconds{ 0.0f };
        float closeSeconds{ 0.15f };
        bool enabled{ false };
        // Slot 0's bolt rifles need their action's pose read too, for ManualBolt.
        bool manualBoltEnabled{ false };
        // The player's graph is in a reload, which is slot 0's reload.
        bool playerReloadActive{ false };
        // A PhysicalReload consumer owns slot 0's bolt.
        bool physicalReloadActive{ false };
        // The clip sampler runs one job at a time, and an exact harvest has it.
        bool clipSamplerAvailable{ false };
    };

    /*
     * Leaves an empty gun's action open, on both weapon slots, until a reload
     * starts or rounds arrive. Runs once a frame in ROCK's BeforeRock phase on its
     * frame thread, which owns all of this state.
     *
     * The open pose is read off screen from the weapon's own clips the first time
     * a form is equipped, one form at a time, so it is known before the first
     * magazine runs dry. Reloads are read first: the bolt parked open in any
     * reload variant is the animator's locked-back pose, across the bolt and the
     * parts that keep still beside it. Failing that the other clips are read, and
     * the farthest frame of a stroke stands in. A gun none of whose clips open its
     * bolt has no pose and never holds.
     *
     * ROCK applies the pose under a two-frame lease, so a lost owner or a missed
     * frame simply lets the graph have the bones back.
     */
    void update(RockApiClient& rock, const FrameInput& input);

    // Forgets the equipped weapons and ends any hold or clip read. Read poses
    // stay: they belong to weapon forms, which hold for the whole process.
    void reset(RockApiClient& rock);

    // What slot 0's weapon is, once its parts and family are known.
    enum class BoltFamily : std::uint8_t
    {
        Pending,
        NotBoltAction,
        BoltAction,
    };
    [[nodiscard]] BoltFamily slotZeroFamily(std::uint64_t generation);

    // The action of slot 0's bolt rifle: each bone's key, and its closed and open
    // poses read from the weapon's clips, in the order of kActionBoneNames.
    struct ManualAction
    {
        std::array<std::uint64_t, bolt_hold_policy::kActionBoneNames.size()> keys{};
        std::array<bool, bolt_hold_policy::kActionBoneNames.size()> posed{};
        std::array<RE::NiTransform, bolt_hold_policy::kActionBoneNames.size()> closed{};
        std::array<RE::NiTransform, bolt_hold_policy::kActionBoneNames.size()> open{};
    };
    // False while the clips are being read and for a gun none of whose clips move its bolt.
    [[nodiscard]] bool queryManualAction(std::uint64_t generation, ManualAction& out);
}
