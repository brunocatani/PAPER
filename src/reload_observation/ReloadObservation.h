#pragma once

#include "api/PAPERApi.h"
#include "api/RockTypes.h"

#include <cstdint>

namespace paper::reload_observation
{
    enum class EvidenceMotionSourceFlag : std::uint32_t
    {
        None = 0,
        BaselineValid = 1u << 0,
        CurrentValid = 1u << 1,
        SourceNodeSelected = 1u << 2,
        InteractionNodeFallback = 1u << 3,
    };

    struct EvidenceMotionSource
    {
        std::uint32_t evidenceId{ 0 };
        std::uint32_t bodyId{ 0x7FFF'FFFF };
        std::int32_t sourceNodeId{ -1 };
        std::int32_t interactionNodeId{ -1 };
        std::int32_t selectedNodeId{ -1 };
        std::uint32_t flags{ 0 };
        std::uint32_t partKind{ 0 };
        std::uint32_t actionRole{ 0 };
        api::PaperTransformV1 baselineWeaponLocal{};
        api::PaperTransformV1 currentWeaponLocal{};
    };

    void reset();

    // Game-thread ROCK phase ownership. The grip state's scene pointer is a
    // current-callback witness only and is never retained by this subsystem.
    void advanceFrame(
        const rock::api::core::AnimationPhaseContextV1& context,
        const paper::RockWeaponGripState* gripState,
        std::uint32_t paperProviderGeneration,
        bool collectEvidenceGeometry);
    void capturePhase(
        const rock::api::core::AnimationPhaseContextV1& context,
        const paper::RockWeaponGripState& gripState,
        api::PaperReloadObservationPhaseV1 phase);
    void completeFrame(
        const rock::api::core::AnimationPhaseContextV1& context,
        std::uint32_t paperProviderGeneration);

    [[nodiscard]] api::PaperResultV1 getLimits(
        api::PaperReloadObservationLimitsV1& outLimits);
    [[nodiscard]] api::PaperResultV1 getCatalogState(
        api::PaperReloadCatalogStateV1& outState);
    [[nodiscard]] api::PaperResultV1 copyCatalogNodes(
        std::uint64_t catalogSequence,
        std::uint32_t firstNode,
        api::PaperReloadNodeCatalogEntryV1* outNodes,
        std::uint32_t maxNodes,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyEvidence(
        std::uint64_t catalogSequence,
        std::uint32_t firstEvidence,
        api::PaperReloadEvidenceV1* outEvidence,
        std::uint32_t maxEvidence,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 copyEvidencePoints(
        std::uint64_t catalogSequence,
        std::uint32_t evidenceId,
        std::uint32_t firstPoint,
        api::PaperPoint3V1* outPoints,
        std::uint32_t maxPoints,
        std::uint32_t& outCopied);
    [[nodiscard]] api::PaperResultV1 getFrameState(
        api::PaperReloadFrameStateV1& outState);
    [[nodiscard]] api::PaperResultV1 copyNodeObservations(
        std::uint64_t snapshotSequence,
        std::uint32_t firstObservation,
        api::PaperReloadNodeObservationV1* outObservations,
        std::uint32_t maxObservations,
        std::uint32_t& outCopied);
    // Internal value-only bridge for derived providers. It selects the source
    // scene node when available and falls back to the interaction root without
    // changing the public raw observation records.
    [[nodiscard]] api::PaperResultV1 copyEvidenceMotionSources(
        EvidenceMotionSource* outSources,
        std::uint32_t maxSources,
        std::uint32_t& outCopied);
}
