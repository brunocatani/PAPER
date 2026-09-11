#pragma once

#include "api/PAPERApi.h"

#include <cstdint>
#include <string>

namespace paper
{
    struct PaperConfig
    {
        bool enabled{ true };
        bool manualReloadOnly{ true };
        bool debugDrawNativeAnimation{ false };
        bool debugDrawNativeAnimationText{ true };
        float debugNativeAnimationAxisLength{ 5.0f };
        float debugNativeAnimationMarkerSize{ 1.5f };
        api::PaperDevelopmentCaptureModeV1 developmentCaptureMode{
            api::PaperDevelopmentCaptureModeV1::User
        };
        api::PaperWeaponMotionCacheAccessV1 weaponMotionCacheAccess{
            api::PaperWeaponMotionCacheAccessV1::Off
        };
        bool developmentCaptureAutoStart{ false };
        bool developmentCaptureAllowApiActivation{ false };
        std::uint32_t weaponMotionSessionCacheMiB{ 64 };
        std::uint32_t weaponMotionDiskCacheMiB{ 256 };
        std::uint32_t weaponMotionMaximumFileMiB{ 16 };
        std::uint32_t weaponMotionMaximumEntries{ 256 };
        int logLevel{ 2 };
        std::string activePath{};

        [[nodiscard]] bool reload();
        void startWatching();
        [[nodiscard]] bool processPendingReload();
    };

    extern PaperConfig g_config;
}
