#pragma once

#include <PAPER/Provider.h>
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
    void setCacheAccess(api::WeaponMotionCacheAccess access);
    [[nodiscard]] bool requiresExactAnimationEvidence(bool cacheReadRequested);
    void reset(ResetReason reason);
    void completeFrame(
        const rock::api::animation::AnimationPhaseContext& context,
        RockApiClient& rockApi,
        std::uint32_t paperProviderGeneration,
        const RuntimeOptions& options);

    [[nodiscard]] api::Status getLimits(
        api::WeaponMotionLimits& outLimits);
    [[nodiscard]] api::Status getCatalogState(
        api::WeaponMotionCatalogState& outState);
    [[nodiscard]] api::Status copyParts(
        std::uint64_t catalogSequence,
        std::uint32_t firstPart,
        api::WeaponMotionPart* outParts,
        std::uint32_t maxParts,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyStages(
        std::uint64_t catalogSequence,
        std::uint32_t firstStage,
        api::WeaponMotionStage* outStages,
        std::uint32_t maxStages,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyStageKeys(
        std::uint64_t catalogSequence,
        std::uint32_t stageId,
        std::uint32_t firstKey,
        api::ReloadQsTransform* outKeys,
        std::uint32_t maxKeys,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyFollowers(
        std::uint64_t catalogSequence,
        std::uint32_t stageId,
        std::uint32_t firstFollower,
        api::WeaponMotionFollower* outFollowers,
        std::uint32_t maxFollowers,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyFollowerKeys(
        std::uint64_t catalogSequence,
        std::uint32_t stageId,
        std::uint32_t followerIndex,
        std::uint32_t firstKey,
        api::ReloadQsTransform* outKeys,
        std::uint32_t maxKeys,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status getLearningState(
        api::WeaponMotionLearningState& outState);
    [[nodiscard]] api::Status copyRecorders(
        std::uint64_t snapshotSequence,
        std::uint32_t firstRecorder,
        api::WeaponMotionRecorder* outRecorders,
        std::uint32_t maxRecorders,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status getManipulationFrameState(
        api::WeaponManipulationFrameState& outState);
    [[nodiscard]] api::Status getManipulationHandState(
        api::Hand hand,
        api::WeaponManipulationHandState& outState);
    [[nodiscard]] api::Status getStoreState(
        api::WeaponMotionStoreState& outState);

    [[nodiscard]] std::uint32_t drainEvents(
        api::Event* outEvents,
        std::uint32_t maxEvents);
}
