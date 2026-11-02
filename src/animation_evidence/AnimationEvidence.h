#pragma once

#include "animation_evidence/ExactClipPreharvest.h"
#include <PAPER/Provider.h>
#include "api/RockTypes.h"

#include <cstdint>

namespace paper::animation_evidence
{
    struct FrameDiagnostics
    {
        std::uint64_t totalMicroseconds{ 0 };
        std::uint64_t catalogSetupMicroseconds{ 0 };
        std::uint64_t passiveDrainMicroseconds{ 0 };
        std::uint64_t nameCollectionMicroseconds{ 0 };
        std::uint64_t passiveUpdateMicroseconds{ 0 };
        std::uint64_t exactUpdateMicroseconds{ 0 };
        std::uint64_t passiveStatsMicroseconds{ 0 };
        exact_clip_preharvest::RuntimeDiagnostics exact{};
    };

    void reset();

    void advanceFrame(
        const rock::api::animation::AnimationPhaseContext& context,
        const paper::RockWeaponGripState* gripState,
        std::uint32_t paperProviderGeneration,
        bool exactPreharvestDemand);
    void completeFrame(
        const rock::api::animation::AnimationPhaseContext& context);
    [[nodiscard]] FrameDiagnostics snapshotFrameDiagnostics();

    [[nodiscard]] api::Status getLimits(
        api::ReloadAnimationLimits& outLimits);
    [[nodiscard]] api::Status getCatalogState(
        api::ReloadAnimationCatalogState& outState);
    [[nodiscard]] api::Status getLiveState(
        api::ReloadAnimationLiveState& outState);
    [[nodiscard]] api::Status copyClips(
        std::uint64_t catalogSequence,
        std::uint32_t firstClip,
        api::ReloadAnimationClip* outClips,
        std::uint32_t maxClips,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyTracks(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t firstTrack,
        api::ReloadAnimationTrack* outTracks,
        std::uint32_t maxTracks,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copySamples(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t trackId,
        std::uint32_t firstSample,
        api::ReloadAnimationSample* outSamples,
        std::uint32_t maxSamples,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyAnnotations(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t firstAnnotation,
        api::ReloadAnimationAnnotation* outAnnotations,
        std::uint32_t maxAnnotations,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyTriggers(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t firstTrigger,
        api::ReloadAnimationTrigger* outTriggers,
        std::uint32_t maxTriggers,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copySkeleton(
        std::uint64_t catalogSequence,
        std::uint32_t firstBone,
        api::ReloadAnimationSkeletonBone* outBones,
        std::uint32_t maxBones,
        std::uint32_t& outCopied);
}
