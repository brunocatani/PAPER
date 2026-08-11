#pragma once

#include "api/PAPERApi.h"

#include <cstdint>

namespace paper::development_capture_policy
{
    [[nodiscard]] constexpr std::uint32_t flag(
        const api::PaperDevelopmentCaptureScopeV1 scope)
    {
        return static_cast<std::uint32_t>(scope);
    }

    inline constexpr std::uint32_t kAllScopes =
        flag(api::PaperDevelopmentCaptureScopeV1::All);

    [[nodiscard]] constexpr bool validMode(
        const api::PaperDevelopmentCaptureModeV1 mode)
    {
        return mode >= api::PaperDevelopmentCaptureModeV1::User &&
            mode <= api::PaperDevelopmentCaptureModeV1::Capture;
    }

    [[nodiscard]] constexpr bool validCacheAccess(
        const api::PaperWeaponMotionCacheAccessV1 access)
    {
        return access >= api::PaperWeaponMotionCacheAccessV1::Off &&
            access <= api::PaperWeaponMotionCacheAccessV1::ReadWrite;
    }

    [[nodiscard]] constexpr std::uint32_t expandDependencies(
        std::uint32_t scopes)
    {
        scopes &= kAllScopes;
        const auto passive = flag(
            api::PaperDevelopmentCaptureScopeV1::PassiveObservation);
        const auto compilation = flag(
            api::PaperDevelopmentCaptureScopeV1::WeaponMotionCompilation);
        const auto cacheRead = flag(
            api::PaperDevelopmentCaptureScopeV1::CompiledCacheRead);
        const auto cacheWrite = flag(
            api::PaperDevelopmentCaptureScopeV1::CompiledCacheWrite);
        const auto dependent = flag(
                api::PaperDevelopmentCaptureScopeV1::ExactAnimationHarvest) |
            compilation | cacheRead | cacheWrite |
            flag(api::PaperDevelopmentCaptureScopeV1::LiveMotionLearning);
        if ((scopes & dependent) != 0) {
            scopes |= passive;
        }
        if ((scopes & (cacheRead | cacheWrite |
                          flag(api::PaperDevelopmentCaptureScopeV1::
                              LiveMotionLearning))) != 0) {
            scopes |= compilation;
        }
        if ((scopes & cacheWrite) != 0) {
            scopes |= cacheRead;
        }
        return scopes;
    }

    [[nodiscard]] constexpr std::uint32_t allowedScopes(
        const api::PaperDevelopmentCaptureModeV1 mode,
        const api::PaperWeaponMotionCacheAccessV1 cacheAccess)
    {
        if (!validMode(mode) || !validCacheAccess(cacheAccess) ||
            mode == api::PaperDevelopmentCaptureModeV1::User) {
            return 0;
        }

        std::uint32_t scopes = flag(
            api::PaperDevelopmentCaptureScopeV1::PassiveObservation);
        if (mode >= api::PaperDevelopmentCaptureModeV1::Harvest) {
            scopes |= flag(
                api::PaperDevelopmentCaptureScopeV1::ExactAnimationHarvest);
        }
        if (cacheAccess != api::PaperWeaponMotionCacheAccessV1::Off) {
            scopes |= flag(
                api::PaperDevelopmentCaptureScopeV1::WeaponMotionCompilation) |
                flag(api::PaperDevelopmentCaptureScopeV1::CompiledCacheRead);
        }
        if (cacheAccess == api::PaperWeaponMotionCacheAccessV1::ReadWrite) {
            scopes |= flag(
                api::PaperDevelopmentCaptureScopeV1::CompiledCacheWrite);
        }
        if (mode == api::PaperDevelopmentCaptureModeV1::Capture) {
            scopes |= flag(
                api::PaperDevelopmentCaptureScopeV1::WeaponMotionCompilation) |
                flag(api::PaperDevelopmentCaptureScopeV1::LiveMotionLearning);
        }
        return expandDependencies(scopes);
    }

    [[nodiscard]] constexpr std::uint32_t autoStartScopes(
        const api::PaperDevelopmentCaptureModeV1 mode,
        const api::PaperWeaponMotionCacheAccessV1 cacheAccess,
        const bool autoStart)
    {
        if (!autoStart) {
            return 0;
        }
        auto scopes = allowedScopes(mode, cacheAccess);
        // When motion compilation is active, exact harvesting is activated by
        // a compiled-cache miss. This keeps a valid hit from repeating the
        // expensive off-screen sampling pass. Harvest-only mode without a
        // compiler must retain its explicit exact-harvest behavior.
        if ((scopes & flag(api::PaperDevelopmentCaptureScopeV1::
                           WeaponMotionCompilation)) != 0) {
            scopes &= ~flag(
                api::PaperDevelopmentCaptureScopeV1::ExactAnimationHarvest);
        }
        return scopes;
    }
}
