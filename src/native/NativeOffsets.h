#pragma once

#include <cstdint>

namespace paper::offsets
{
    inline constexpr std::uintptr_t kFunc_PlayerControls_DoAction = 0x0FC07E0;
    inline constexpr std::uintptr_t kFunc_PlayerControls_ExecuteAction = 0x0F1EAA0;
    inline constexpr std::uintptr_t kCallsite_PlayerControls_ExecuteAction = 0x0FC0C39;
    inline constexpr std::uintptr_t kSignature_PlayerControls_AutomaticReload = 0x0FC0D18;
    inline constexpr std::uintptr_t kCallsite_PlayerControls_AutomaticReload = 0x0FC0D4F;
    inline constexpr std::uintptr_t kFunc_ReadyWeaponHandler_ShouldHandleEvent = 0x0FCE650;
    inline constexpr std::uintptr_t kFunc_WeaponFireHandler_Handle = 0x0FF2A40;
    inline constexpr std::uintptr_t kFunc_ReloadStateChangeHandler_Handle = 0x0FF2B90;
    inline constexpr std::uintptr_t kFunc_GetReloadStartStateToken = 0x16A3070;
    inline constexpr std::uintptr_t kFunc_GetReloadEndStateToken = 0x16A30D0;
    inline constexpr std::uintptr_t kVtableEntry_ReadyWeaponHandler_ShouldHandleEvent = 0x2D8A480;
    inline constexpr std::uintptr_t kVtableEntry_WeaponFireHandler_Handle = 0x2D8D2E8;
    inline constexpr std::uintptr_t kVtableEntry_ReloadStateChangeHandler_Handle = 0x2D8D300;
}
