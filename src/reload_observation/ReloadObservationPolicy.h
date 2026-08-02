#pragma once

#include "api/PAPERApi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace paper::reload_observation_policy
{
    template <class Transform, class Compose>
    [[nodiscard]] constexpr Transform resolveWeaponLocalFromGraph(
        const bool isWeaponRoot,
        const Transform& identity,
        const Transform& parentWeaponLocal,
        const Transform& objectLocal,
        Compose&& compose)
    {
        return isWeaponRoot ?
            identity :
            compose(parentWeaponLocal, objectLocal);
    }

    struct EvidenceNodeCandidate
    {
        std::int32_t sourceNodeId{ -1 };
        std::int32_t interactionNodeId{ -1 };
    };

    struct ObservationTargetSelection
    {
        std::array<
            std::uint32_t,
            api::PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1>
            nodeIds{};
        std::uint32_t count{ 0 };
        std::uint32_t candidateCount{ 0 };
        std::uint32_t omittedCount{ 0 };
    };

    [[nodiscard]] constexpr ObservationTargetSelection
    selectObservationTargets(
        const std::uint32_t nodeCount,
        const std::span<const EvidenceNodeCandidate> evidence,
        const std::span<const std::uint8_t> namedNodes)
    {
        ObservationTargetSelection result{};
        std::array<
            std::uint8_t,
            api::PAPER_MAX_RELOAD_CATALOG_NODES_V1>
            selected{};
        const auto boundedNodeCount = (std::min)(
            nodeCount,
            api::PAPER_MAX_RELOAD_CATALOG_NODES_V1);

        const auto append = [&](const std::int32_t candidate) {
            if (candidate < 0 ||
                static_cast<std::uint32_t>(candidate) >= boundedNodeCount) {
                return;
            }
            const auto nodeId = static_cast<std::uint32_t>(candidate);
            if (selected[nodeId] != 0) {
                return;
            }
            selected[nodeId] = 1;
            ++result.candidateCount;
            if (result.count < result.nodeIds.size()) {
                result.nodeIds[result.count++] = nodeId;
            }
        };

        for (const auto& candidate : evidence) {
            append(candidate.sourceNodeId);
            append(candidate.interactionNodeId);
        }

        const auto namedCount = (std::min<std::size_t>)(
            boundedNodeCount,
            namedNodes.size());
        for (std::uint32_t nodeId = 0;
             nodeId < namedCount;
             ++nodeId) {
            if (namedNodes[nodeId] != 0) {
                append(static_cast<std::int32_t>(nodeId));
            }
        }

        result.omittedCount =
            result.candidateCount > result.count ?
                result.candidateCount - result.count :
                0;
        return result;
    }
}
