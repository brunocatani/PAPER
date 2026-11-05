#pragma once

#include <cstdint>

namespace paper::offsets
{
    inline constexpr std::uintptr_t kFunc_PlayerControls_DoAction = 0x0FC07E0;
    inline constexpr std::uintptr_t kFunc_PlayerControls_ExecuteAction = 0x0F1EAA0;
    inline constexpr std::uintptr_t kCallsite_PlayerControls_ExecuteAction = 0x0FC0C39;
    inline constexpr std::uintptr_t kSignature_PlayerControls_AutomaticReload = 0x0FC0D18;
    inline constexpr std::uintptr_t kCallsite_PlayerControls_AutomaticReload = 0x0FC0D4F;
    inline constexpr std::uintptr_t kFunc_PlaySoundDescriptor = 0x081FBF0;
    inline constexpr std::uintptr_t kFunc_AIProcess_SetLoadedRounds = 0x0EC4B90;
    inline constexpr std::uintptr_t kFunc_Actor_GetEquippedInstance = 0x0E50DA0;
    inline constexpr std::uintptr_t kFunc_ExtraDataList_ReadUniqueId = 0x0098830;
    inline constexpr std::uintptr_t kFunc_ExtraDataList_WriteUniqueId = 0x0098880;
    inline constexpr std::uintptr_t kFunc_Reference_AllocateUniqueId = 0x03E3A10;
    inline constexpr std::uintptr_t kFunc_EjectShellCasing = 0x0330C80;
    inline constexpr std::uintptr_t kCallsite_EjectShellCasingHandler = 0x0FF41D9;
    inline constexpr std::uintptr_t kFunc_TESActionData_Process = 0x0F9BEF0;
    inline constexpr std::uintptr_t kSignature_PlayerCharacter_ImmediateAutomaticReload = 0x0F7A4A5;
    inline constexpr std::uintptr_t kCallsite_PlayerCharacter_ImmediateAutomaticReload = 0x0F7A579;
    inline constexpr std::uintptr_t kSignature_PlayerCharacter_DelayedAutomaticReload = 0x0F07335;
    inline constexpr std::uintptr_t kCallsite_PlayerCharacter_DelayedAutomaticReload = 0x0F073E8;
    inline constexpr std::uintptr_t kFunc_ReadyWeaponHandler_ShouldHandleEvent = 0x0FCE650;
    inline constexpr std::uintptr_t kFunc_WeaponFireHandler_Handle = 0x0FF2A40;
    inline constexpr std::uintptr_t kFunc_ReloadStateChangeHandler_Handle = 0x0FF2B90;
    inline constexpr std::uintptr_t kFunc_GetReloadStartStateToken = 0x16A3070;
    inline constexpr std::uintptr_t kFunc_GetReloadEndStateToken = 0x16A30D0;
    inline constexpr std::uintptr_t kVtableEntry_ReadyWeaponHandler_ShouldHandleEvent = 0x2D8A480;
    inline constexpr std::uintptr_t kVtableEntry_WeaponFireHandler_Handle = 0x2D8D2E8;
    inline constexpr std::uintptr_t kVtableEntry_ReloadStateChangeHandler_Handle = 0x2D8D300;
}
