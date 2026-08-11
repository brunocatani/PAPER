#pragma once

#include "animation/NativeAnimationAuthorityPolicy.h"
#include "api/ROCKProviderApi.h"

#include <cstdint>

namespace paper::native_pose_pipeline
{
    void publishFrame(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
        native_animation_authority_policy::NativeAnimationCompatibilityReason
            compatibilityReason,
        std::uint32_t localAuthorityFlags,
        std::uint32_t consumerAuthorityFlags,
        std::uint32_t weaponFormId,
        std::uint64_t weaponGenerationKey,
        bool runtimeOperational);
}
