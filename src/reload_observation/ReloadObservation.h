#pragma once

#include "api/PAPERApi.h"
#include "api/ROCKProviderApi.h"

#include <cstdint>

namespace paper::reload_observation
{
    void reset();

    // Game-thread ROCK phase ownership. The grip state's scene pointer is a
    // current-callback witness only and is never retained by this subsystem.
    void advanceFrame(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
        const rock::provider::RockProviderEquippedWeaponGripStateV1* gripState,
        std::uint32_t paperProviderGeneration);
    void capturePhase(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
        const rock::provider::RockProviderEquippedWeaponGripStateV1& gripState,
        api::PaperReloadObservationPhaseV1 phase);
    void completeFrame(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
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
}
