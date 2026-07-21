#pragma once

#include <cstdint>

namespace rock_reanimate::offsets
{
    inline constexpr std::uintptr_t kFunc_WeaponFireHandler_Handle = 0x0FF2A40;
    inline constexpr std::uintptr_t kFunc_ReloadStateChangeHandler_Handle = 0x0FF2B90;
    inline constexpr std::uintptr_t kFunc_GetReloadStartStateToken = 0x16A3070;
    inline constexpr std::uintptr_t kFunc_GetReloadEndStateToken = 0x16A30D0;
    inline constexpr std::uintptr_t kVtableEntry_WeaponFireHandler_Handle = 0x2D8D2E8;
    inline constexpr std::uintptr_t kVtableEntry_ReloadStateChangeHandler_Handle = 0x2D8D300;
}
