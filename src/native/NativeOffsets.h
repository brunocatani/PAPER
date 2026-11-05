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
    inline constexpr std::uintptr_t kFunc_Actor_GetEquippedAmmo = 0x0E05BA0;
    inline constexpr std::uintptr_t kFunc_ExtraDataList_ReadUniqueId = 0x0098830;
    inline constexpr std::uintptr_t kFunc_ExtraDataList_WriteUniqueId = 0x0098880;
    inline constexpr std::uintptr_t kFunc_Reference_AllocateUniqueId = 0x03E3A10;
    inline constexpr std::uintptr_t kFunc_EjectShellCasing = 0x0330C80;
    inline constexpr std::uintptr_t kCallsite_EjectShellCasingHandler = 0x0FF41D9;
    // Inside the eject function: its search for the casing point, its debris storage
    // and its debris construction.
    inline constexpr std::uintptr_t kCallsite_EjectFindCasingPoint = 0x0330DFF;
    inline constexpr std::uintptr_t kFunc_FindCasingPoint = 0x033E130;
    inline constexpr std::uintptr_t kCallsite_EjectAllocateDebris = 0x0331057;
    inline constexpr std::uintptr_t kCallsite_EjectConstructDebris = 0x0331110;
    inline constexpr std::uintptr_t kSignature_EjectDebrisResult = 0x0331115;
    inline constexpr std::uintptr_t kFunc_AllocateDebrisStorage = 0x00030D0;
    inline constexpr std::uintptr_t kFunc_ConstructDebris = 0x071D520;
    inline constexpr std::uintptr_t kSignature_CreateReference = 0x011BD80;
    inline constexpr std::uintptr_t kSignature_CreateReferenceForcePersist = 0x011C1F5;
    inline constexpr std::uintptr_t kFunc_Cell_GetBhkWorld = 0x039B070;
    inline constexpr std::uintptr_t kFunc_SetLinearVelocity = 0x1E15630;
    inline constexpr std::uintptr_t kFunc_SetAngularVelocity = 0x1E156D0;
    inline constexpr std::uintptr_t kFunc_WorldLockWrite = 0x1DF5FB0;
    inline constexpr std::uintptr_t kFunc_WorldUnlockWrite = 0x1DF5FD0;
    inline constexpr std::uintptr_t kFunc_System_GetWorld = 0x1E0C530;
    inline constexpr std::uintptr_t kFunc_CollisionObject_GetBodyId = 0x1E07EE0;
    inline constexpr std::uintptr_t kFunc_CollisionObject_GetBody = 0x1E07D80;
    inline constexpr std::uintptr_t kFunc_World_SetMotionProperties = 0x153B2F0;
    inline constexpr std::uintptr_t kFunc_SetMotionType = 0x1DF95B0;
    inline constexpr std::uintptr_t kSignature_CollisionObjectGetSystemCall = 0x1E1B474;
    inline constexpr std::uintptr_t kGlobal_ThreadStateTlsIndex = 0x689CACC;
    // RemoveItem's HUD aggregation of what left the inventory.
    inline constexpr std::uintptr_t kCallsite_RemoveItemHudMessage = 0x03E1D74;
    inline constexpr std::uintptr_t kFunc_InventoryCount = 0x03E3750;
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
