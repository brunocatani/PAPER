#pragma once

#include "api/RockTypes.h"

#include <cstdint>
#include <span>

namespace paper::weapon_classification
{
    // The live form's WeaponFamilyFlag bits from ROCK's classification of it
    // and its parts' evidence, for either weapon slot. Empty evidence marks the
    // part evidence incomplete.
    [[nodiscard]] std::uint64_t classifyWeaponFamilyFlags(
        std::uint32_t weaponFormId,
        const rock::api::weapon::Classification& classification,
        std::span<const rock::api::weaponparts::EvidenceDetail> evidence);
}
