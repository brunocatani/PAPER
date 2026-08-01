#pragma once

#include "animation_evidence/AnimationEvidenceTypes.h"

#include <cstdint>

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

    [[nodiscard]] Stats snapshotStats() noexcept;
}
