#pragma once

#include <cstdint>
#include <string_view>

namespace paper::exact_clip_preharvest
{
    enum class State : std::uint8_t
    {
        Idle,
        LoadingBaseGraphs,
        LoadingWeaponSubgraph,
        LoadingClip,
        SamplingClip,
        Completed,
        Failed,
    };

    struct StepResult
    {
        State state{ State::Idle };
    };

    struct PoseBone
    {
        std::uint32_t nameIndex{ 0 };
        // In the bone's skeleton parent: the space the graph writes to its node.
        RE::NiTransform local{};
        // In the frame of the skeleton's Weapon bone.
        RE::NiTransform inWeapon{};
    };

    // What the animator marked at one time of a clip, such as "SoundPlay.WPNBoltOpen".
    struct PoseMarker
    {
        float timeSeconds{ 0.0f };
        const char* text{ nullptr };
    };

    // One sampled pose. Only the bones this clip has a track for are present: an
    // untracked bone would show the shared skeleton's bind pose, which is no
    // position of this weapon. The clip's markers come with its last pose, borrowed
    // for the call.
    struct PoseFrame
    {
        const char* animationPath{ nullptr };
        std::uint32_t sampleIndex{ 0 };
        std::uint32_t sampleCount{ 0 };
        float timeSeconds{ 0.0f };
        const PoseBone* bones{ nullptr };
        std::uint32_t boneCount{ 0 };
        const PoseMarker* markers{ nullptr };
        std::uint32_t markerCount{ 0 };
    };

    using ClipOrderFn = std::uint32_t (*)(std::string_view animationPath) noexcept;
    using ReadsClipFn = bool (*)(std::string_view animationPath, void* userData) noexcept;
    using PoseFn = bool (*)(const PoseFrame& frame, void* userData) noexcept;

    struct PoseReader
    {
        float samplesPerSecond{ 0.0f };
        // Clips are read in ascending order; equal clips keep the graph's order.
        ClipOrderFn clipOrder{ nullptr };
        // Asked before each clip; false ends the read as completed.
        ReadsClipFn readsClip{ nullptr };
        // False skips the rest of this clip.
        PoseFn pose{ nullptr };
        void* userData{ nullptr };
        // Bones outside the weapon, such as the hands. Their nameIndex continues
        // after the node names. A clip's frame leaves them out when a bone between
        // them and the weapon has a transform NiTransform cannot hold.
        const char* const* armNodeNames{ nullptr };
        std::uint32_t armNodeNameCount{ 0 };
    };

    /*
     * Loads the equipped weapon's own animation subgraph off screen and samples
     * its clips for the reader: each sampled pose goes to the reader and nothing
     * is kept. A clip with no track for the first name is skipped unread. Finds
     * the weapon in any equipped slot. One job runs at a time; a new weapon or
     * generation restarts it.
     */
    [[nodiscard]] StepResult stepPoses(
        std::uint32_t weaponFormId,
        std::uint64_t weaponGenerationKey,
        const char* const* nodeNames,
        std::uint32_t nodeNameCount,
        const PoseReader& reader) noexcept;

    void reset() noexcept;
}
