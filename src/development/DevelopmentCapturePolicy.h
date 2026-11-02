#pragma once

#include <PAPER/Provider.h>

#include <cstdint>

namespace paper::development_capture_policy
{
    [[nodiscard]] constexpr std::uint32_t flag(
        const api::DevelopmentCaptureScope scope)
    {
        return static_cast<std::uint32_t>(scope);
    }

    inline constexpr std::uint32_t kAllScopes =
        flag(api::DevelopmentCaptureScope::All);

    [[nodiscard]] constexpr bool validMode(
        const api::DevelopmentCaptureMode mode)
    {
        return mode >= api::DevelopmentCaptureMode::User &&
            mode <= api::DevelopmentCaptureMode::Capture;
    }

    [[nodiscard]] constexpr bool validCacheAccess(
        const api::WeaponMotionCacheAccess access)
    {
        return access >= api::WeaponMotionCacheAccess::Off &&
            access <= api::WeaponMotionCacheAccess::ReadWrite;
    }

    [[nodiscard]] constexpr std::uint32_t expandDependencies(
        std::uint32_t scopes)
    {
        scopes &= kAllScopes;
        const auto passive = flag(
            api::DevelopmentCaptureScope::PassiveObservation);
        const auto compilation = flag(
            api::DevelopmentCaptureScope::WeaponMotionCompilation);
        const auto cacheRead = flag(
            api::DevelopmentCaptureScope::CompiledCacheRead);
        const auto cacheWrite = flag(
            api::DevelopmentCaptureScope::CompiledCacheWrite);
        const auto dependent = flag(
                api::DevelopmentCaptureScope::ExactAnimationHarvest) |
            compilation | cacheRead | cacheWrite |
            flag(api::DevelopmentCaptureScope::LiveMotionLearning);
        if ((scopes & dependent) != 0) {
            scopes |= passive;
        }
        if ((scopes & (cacheRead | cacheWrite |
                          flag(api::DevelopmentCaptureScope::
                              LiveMotionLearning))) != 0) {
            scopes |= compilation;
        }
        if ((scopes & cacheWrite) != 0) {
            scopes |= cacheRead;
        }
        return scopes;
    }

    [[nodiscard]] constexpr std::uint32_t allowedScopes(
        const api::DevelopmentCaptureMode mode,
        const api::WeaponMotionCacheAccess cacheAccess)
    {
        if (!validMode(mode) || !validCacheAccess(cacheAccess) ||
            mode == api::DevelopmentCaptureMode::User) {
            return 0;
        }

        std::uint32_t scopes = flag(
            api::DevelopmentCaptureScope::PassiveObservation);
        if (mode >= api::DevelopmentCaptureMode::Harvest) {
            scopes |= flag(
                api::DevelopmentCaptureScope::ExactAnimationHarvest);
        }
        if (cacheAccess != api::WeaponMotionCacheAccess::Off) {
            scopes |= flag(
                api::DevelopmentCaptureScope::WeaponMotionCompilation) |
                flag(api::DevelopmentCaptureScope::CompiledCacheRead);
        }
        if (cacheAccess == api::WeaponMotionCacheAccess::ReadWrite) {
            scopes |= flag(
                api::DevelopmentCaptureScope::CompiledCacheWrite);
        }
        if (mode == api::DevelopmentCaptureMode::Capture) {
            scopes |= flag(
                api::DevelopmentCaptureScope::WeaponMotionCompilation) |
                flag(api::DevelopmentCaptureScope::LiveMotionLearning);
        }
        return expandDependencies(scopes);
    }

    [[nodiscard]] constexpr std::uint32_t autoStartScopes(
        const api::DevelopmentCaptureMode mode,
        const api::WeaponMotionCacheAccess cacheAccess,
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
        if ((scopes & flag(api::DevelopmentCaptureScope::
                           WeaponMotionCompilation)) != 0) {
            scopes &= ~flag(
                api::DevelopmentCaptureScope::ExactAnimationHarvest);
        }
        return scopes;
    }
}
