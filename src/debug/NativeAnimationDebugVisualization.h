#pragma once

#include "api/RockTypes.h"

namespace paper::debug_visualization
{
    void publish(
        const rock::api::core::AnimationPhaseContextV1& context,
        const paper::RockWeaponGripState& gripState);
    void clear();
}
