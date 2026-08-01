#pragma once

#include "api/PAPERApi.h"
#include "api/ROCKProviderApi.h"

#include <cstdint>

namespace paper::reload_stages
{
    void reset();
    void completeFrame(
        const rock::provider::RockProviderAnimationPhaseContextV1& context);

    [[nodiscard]] api::PaperResultV1 getState(
        api::PaperReloadStageStateV1& outState);
    [[nodiscard]] api::PaperResultV1 copyParts(
        std::uint64_t snapshotSequence,
        std::uint32_t firstPart,
        api::PaperReloadStagePartV1* outParts,
        std::uint32_t maxParts,
        std::uint32_t& outCopied);
}
