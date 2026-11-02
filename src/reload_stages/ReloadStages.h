#pragma once

#include <PAPER/Provider.h>
#include "api/RockTypes.h"

#include <cstdint>

namespace paper::reload_stages
{
    void reset();
    void completeFrame(
        const rock::api::animation::AnimationPhaseContext& context);

    [[nodiscard]] api::Status getState(
        api::ReloadStageState& outState);
    [[nodiscard]] api::Status copyParts(
        std::uint64_t snapshotSequence,
        std::uint32_t firstPart,
        api::ReloadStagePart* outParts,
        std::uint32_t maxParts,
        std::uint32_t& outCopied);
}
