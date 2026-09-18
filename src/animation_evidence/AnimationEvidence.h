#pragma once

#include "animation_evidence/ExactClipPreharvest.h"
#include "api/PAPERApi.h"
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
        const rock::api::core::AnimationPhaseContextV1& context,
        const paper::RockWeaponGripState* gripState,
        std::uint32_t paperProviderGeneration,
        bool exactPreharvestDemand);
    void completeFrame(
        const rock::api::core::AnimationPhaseContextV1& context);
    [[nodiscard]] FrameDiagnostics snapshotFrameDiagnostics();

    [[nodiscard]] api::PaperResultV1 getLimits(
        api::PaperReloadAnimationLimitsV1& outLimits);
    [[nodiscard]] api::PaperResultV1 getCatalogState(
        api::PaperReloadAnimationCatalogStateV1& outState);
    [[nodiscard]] api::PaperResultV1 getLiveState(
        api::PaperReloadAnimationLiveStateV1& outState);
    [[nodiscard]] api::PaperResultV1 copyClips(
        std::uint64_t catalogSequence,
        std::uint32_t firstClip,
        api::PaperReloadAnimationClipV1* outClips,
        std::uint32_t maxClips,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyTracks(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t firstTrack,
        api::PaperReloadAnimationTrackV1* outTracks,
        std::uint32_t maxTracks,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copySamples(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t trackId,
        std::uint32_t firstSample,
        api::PaperReloadAnimationSampleV1* outSamples,
        std::uint32_t maxSamples,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyAnnotations(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t firstAnnotation,
        api::PaperReloadAnimationAnnotationV1* outAnnotations,
        std::uint32_t maxAnnotations,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyTriggers(
        std::uint64_t catalogSequence,
        std::uint32_t clipId,
        std::uint32_t firstTrigger,
        api::PaperReloadAnimationTriggerV1* outTriggers,
        std::uint32_t maxTriggers,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copySkeleton(
        std::uint64_t catalogSequence,
        std::uint32_t firstBone,
        api::PaperReloadAnimationSkeletonBoneV1* outBones,
        std::uint32_t maxBones,
        std::uint32_t& outCopied);
}
