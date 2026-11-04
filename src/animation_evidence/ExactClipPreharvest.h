#pragma once

#include "animation_evidence/AnimationEvidenceTypes.h"

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

    struct ClipView
    {
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        const char* animationPath{ nullptr };
        float durationSeconds{ 0.0f };
        std::int32_t animationType{ -1 };
        std::int32_t rawTransformTrackCount{ 0 };
        std::int32_t rawFloatTrackCount{ 0 };
        std::uint32_t sampleCount{ 0 };
        const animation_evidence::ExactTrackSamples* tracks{ nullptr };
        std::uint32_t trackCount{ 0 };
        const animation_evidence::ExactSkeletonBone* skeletonBones{ nullptr };
        std::uint32_t skeletonBoneCount{ 0 };
        std::int16_t weaponBoneIndex{ -1 };
        bool targetTracksTruncated{ false };
    };

    using ClipSink = void (*)(const ClipView& clip, void* userData) noexcept;

    struct StepResult
    {
        State state{ State::Idle };
        std::uint32_t clipsPublished{ 0 };
    };

    [[nodiscard]] StepResult step(
        std::uint32_t weaponFormId,
        std::uint64_t weaponGenerationKey,
        const char* const* allowedNodeNames,
        std::uint32_t allowedNodeNameCount,
        ClipSink sink,
        void* sinkUserData) noexcept;

    struct PoseBone
    {
        std::uint32_t nameIndex{ 0 };
        // In the bone's skeleton parent: the space the graph writes to its node.
        RE::NiTransform local{};
    };

    // One sampled pose. Only the bones this clip has a track for are present: an
    // untracked bone would show the shared skeleton's bind pose, which is no
    // position of this weapon.
    struct PoseFrame
    {
        const char* animationPath{ nullptr };
        bool preferredClip{ false };
        std::uint32_t sampleIndex{ 0 };
        std::uint32_t sampleCount{ 0 };
        float timeSeconds{ 0.0f };
        const PoseBone* bones{ nullptr };
        std::uint32_t boneCount{ 0 };
    };

    using PreferredClipFn = bool (*)(std::string_view animationPath) noexcept;
    using ReadsOtherClipsFn = bool (*)(void* userData) noexcept;
    using PoseFn = void (*)(const PoseFrame& frame, void* userData) noexcept;

    struct PoseReader
    {
        float samplesPerSecond{ 0.0f };
        // These clips are read first.
        PreferredClipFn isPreferredClip{ nullptr };
        // Asked before each other clip; false ends the read as completed.
        ReadsOtherClipsFn readsOtherClips{ nullptr };
        PoseFn pose{ nullptr };
        void* userData{ nullptr };
    };

    /*
     * The same off-screen job as step, for a reader that wants poses rather than
     * stored tracks: each sampled pose goes to the reader and nothing is kept. A
     * clip with no track for the first name is skipped unread. Finds the weapon in
     * any equipped slot. One job runs at a time, so this and step must not be
     * driven in the same frame; switching between them restarts the job.
     */
    [[nodiscard]] StepResult stepPoses(
        std::uint32_t weaponFormId,
        std::uint64_t weaponGenerationKey,
        const char* const* nodeNames,
        std::uint32_t nodeNameCount,
        const PoseReader& reader) noexcept;

    void reset() noexcept;

    struct Stats
    {
        std::uint32_t animationFileCount{ 0 };
        std::uint32_t clipsSampled{ 0 };
        std::uint32_t clipsPublished{ 0 };
        std::uint32_t clipsRejected{ 0 };
        std::uint32_t targetBonesTruncated{ 0 };
        bool usedLoadedGraphPathFallback{ false };
    };

    struct RuntimeDiagnostics
    {
        State state{ State::Idle };
        std::uint32_t animationPathIndex{ 0 };
        std::uint32_t animationPathCount{ 0 };
        std::uint32_t nextSample{ 0 };
        std::uint32_t sampleCount{ 0 };
        std::uint32_t samplesCompletedLastStep{ 0 };
        std::uint32_t samplingMicrosecondsLastStep{ 0 };
        bool samplingBudgetYielded{ false };
        bool sampleLimitYielded{ false };
        bool backgroundGraphActive{ false };
        bool clipResourceActive{ false };
    };

    [[nodiscard]] Stats snapshotStats() noexcept;
    [[nodiscard]] RuntimeDiagnostics snapshotDiagnostics() noexcept;
}
