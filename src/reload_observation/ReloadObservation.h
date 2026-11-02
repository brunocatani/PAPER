#pragma once

#include <PAPER/Provider.h>
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
        api::Transform baselineWeaponLocal{};
        api::Transform currentWeaponLocal{};
    };

    void reset();

    // Game-thread ROCK phase ownership. The grip state's scene pointer is a
    // current-callback witness only and is never retained by this subsystem.
    void advanceFrame(
        const rock::api::animation::AnimationPhaseContext& context,
        const paper::RockWeaponGripState* gripState,
        std::uint32_t paperProviderGeneration,
        bool collectEvidenceGeometry);
    void capturePhase(
        const rock::api::animation::AnimationPhaseContext& context,
        const paper::RockWeaponGripState& gripState,
        api::ReloadObservationPhase phase);
    void completeFrame(
        const rock::api::animation::AnimationPhaseContext& context,
        std::uint32_t paperProviderGeneration);

    [[nodiscard]] api::Status getLimits(
        api::ReloadObservationLimits& outLimits);
    [[nodiscard]] api::Status getCatalogState(
        api::ReloadCatalogState& outState);
    [[nodiscard]] api::Status copyCatalogNodes(
        std::uint64_t catalogSequence,
        std::uint32_t firstNode,
        api::ReloadNodeCatalogEntry* outNodes,
        std::uint32_t maxNodes,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyEvidence(
        std::uint64_t catalogSequence,
        std::uint32_t firstEvidence,
        api::ReloadEvidence* outEvidence,
        std::uint32_t maxEvidence,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status copyEvidencePoints(
        std::uint64_t catalogSequence,
        std::uint32_t evidenceId,
        std::uint32_t firstPoint,
        api::Point3* outPoints,
        std::uint32_t maxPoints,
        std::uint32_t& outCopied);
    [[nodiscard]] api::Status getFrameState(
        api::ReloadFrameState& outState);
    [[nodiscard]] api::Status copyNodeObservations(
        std::uint64_t snapshotSequence,
        std::uint32_t firstObservation,
        api::ReloadNodeObservation* outObservations,
        std::uint32_t maxObservations,
        std::uint32_t& outCopied);
    // Internal value-only bridge for derived providers. It selects the source
    // scene node when available and falls back to the interaction root without
    // changing the public raw observation records.
    [[nodiscard]] api::Status copyEvidenceMotionSources(
        EvidenceMotionSource* outSources,
        std::uint32_t maxSources,
        std::uint32_t& outCopied);
}
