#pragma once
#include "api/PAPERPhysicalReloadV2.h"
#include "animation/NativeAnimationAuthorityPolicy.h"

namespace paper::physical_reload {
    // Physical control does not consume native right-primary hand animation.
    // Retain all identity/readiness checks while allowing support-hand carry.
    inline bool physicalWeaponReady(bool operational,
        const native_animation_authority_policy::NativeAnimationCompatibilityObservation& weapon) noexcept {
        return operational && weapon.handlingStateValid && weapon.weaponPresent &&
            weapon.weaponIdentityCoherent && weapon.weaponFormId && weapon.weaponGenerationKey && !weapon.akimboActive;
    }
    struct Environment {
        api::PaperRuntimeStateV1 identity;
        bool ready{};
        bool current(std::uint64_t frame)const noexcept {
            return ready && identity.frameIndex<=frame && frame-identity.frameIndex<=1;
        }
    };
    inline Request identity(const v2::Request& r) noexcept {
        Request out;out.weaponFormId=r.weaponFormId;out.leaseFrames=r.leaseFrames;
        out.weaponGenerationKey=r.weaponGenerationKey;out.observedFrame=r.observedFrame;
        out.worldGeneration=r.worldGeneration;out.skeletonGeneration=r.skeletonGeneration;
        out.rockProviderGeneration=r.rockProviderGeneration;out.paperProviderGeneration=r.paperProviderGeneration;
        return out;
    }
}
