#pragma once

#include <string>
#include <cstdint>

namespace paper
{
    struct PaperConfig
    {
        bool enabled{ true };
        bool manualReloadOnly{ true };
        bool nativeReloadAnimationAuthorityTestEnabled{ true };
        bool nativeReloadAnimationPartialAuthorityTestEnabled{ true };
        bool debugDrawNativeAnimation{ false };
        bool debugDrawNativeAnimationText{ true };
        float debugNativeAnimationAxisLength{ 5.0f };
        float debugNativeAnimationMarkerSize{ 1.5f };
        bool weaponMotionCacheEnabled{ true };
        std::uint32_t weaponMotionSessionCacheMiB{ 64 };
        std::uint32_t weaponMotionDiskCacheMiB{ 256 };
        std::uint32_t weaponMotionMaximumFileMiB{ 16 };
        std::uint32_t weaponMotionMaximumEntries{ 256 };
        int logLevel{ 2 };
        std::string activePath{};

        [[nodiscard]] bool reload();
    };

    extern PaperConfig g_config;
}
