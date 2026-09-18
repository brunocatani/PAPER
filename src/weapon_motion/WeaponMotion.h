#pragma once

#include "api/PAPERApi.h"
#include "api/RockTypes.h"

#include <cstdint>

namespace paper
{
    class RockApiClient;
    struct PaperConfig;
}

namespace paper::weapon_motion
{
    struct RuntimeOptions
    {
        bool cacheRead{ false };
        bool cacheWrite{ false };
        bool liveMotionLearning{ false };
        bool manipulationTelemetry{ false };

        [[nodiscard]] bool operator==(const RuntimeOptions&) const = default;
    };

    enum class ResetReason : std::uint32_t
    {
        RuntimeReset = 0,
        DemandEnded = 1,
        ProviderShutdown = 2,
    };

    void activate(const RuntimeOptions& options);
    void configureCache(const PaperConfig& config);
    void setCacheAccess(api::PaperWeaponMotionCacheAccessV1 access);
    [[nodiscard]] bool requiresExactAnimationEvidence(bool cacheReadRequested);
    void reset(ResetReason reason);
    void completeFrame(
        const rock::api::core::AnimationPhaseContextV1& context,
        RockApiClient& rockApi,
        std::uint32_t paperProviderGeneration,
        const RuntimeOptions& options);

    [[nodiscard]] api::PaperResultV1 getLimits(
        api::PaperWeaponMotionLimitsV1& outLimits);
    [[nodiscard]] api::PaperResultV1 getCatalogState(
        api::PaperWeaponMotionCatalogStateV1& outState);
    [[nodiscard]] api::PaperResultV1 copyParts(
        std::uint64_t catalogSequence,
        std::uint32_t firstPart,
        api::PaperWeaponMotionPartV1* outParts,
        std::uint32_t maxParts,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyStages(
        std::uint64_t catalogSequence,
        std::uint32_t firstStage,
        api::PaperWeaponMotionStageV1* outStages,
        std::uint32_t maxStages,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyStageKeys(
        std::uint64_t catalogSequence,
        std::uint32_t stageId,
        std::uint32_t firstKey,
        api::PaperReloadQsTransformV1* outKeys,
        std::uint32_t maxKeys,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyFollowers(
        std::uint64_t catalogSequence,
        std::uint32_t stageId,
        std::uint32_t firstFollower,
        api::PaperWeaponMotionFollowerV1* outFollowers,
        std::uint32_t maxFollowers,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyFollowerKeys(
        std::uint64_t catalogSequence,
        std::uint32_t stageId,
        std::uint32_t followerIndex,
        std::uint32_t firstKey,
        api::PaperReloadQsTransformV1* outKeys,
        std::uint32_t maxKeys,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 getLearningState(
        api::PaperWeaponMotionLearningStateV1& outState);
    [[nodiscard]] api::PaperResultV1 copyRecorders(
        std::uint64_t snapshotSequence,
        std::uint32_t firstRecorder,
        api::PaperWeaponMotionRecorderV1* outRecorders,
        std::uint32_t maxRecorders,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 getManipulationFrameState(
        api::PaperWeaponManipulationFrameStateV1& outState);
    [[nodiscard]] api::PaperResultV1 getManipulationHandState(
        api::PaperHandV1 hand,
        api::PaperWeaponManipulationHandStateV1& outState);
    [[nodiscard]] api::PaperResultV1 getStoreState(
        api::PaperWeaponMotionStoreStateV1& outState);

    [[nodiscard]] std::uint32_t drainEvents(
        api::PaperEventV1* outEvents,
        std::uint32_t maxEvents);
}
