#pragma once

#include "api/ROCKProviderApi.h"

namespace paper::debug_visualization
{
    void publish(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
        const rock::provider::RockProviderEquippedWeaponGripStateV1& gripState);
    void clear();
}
