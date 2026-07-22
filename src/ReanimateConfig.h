#pragma once

#include <string>

namespace rock_reanimate
{
    struct ReanimateConfig
    {
        bool enabled{ true };
        bool nativeReloadAnimationAuthorityTestEnabled{ true };
        bool nativeReloadAnimationPartialAuthorityTestEnabled{ true };
        bool debugDrawNativeAnimation{ false };
        bool debugDrawNativeAnimationText{ true };
        float debugNativeAnimationAxisLength{ 5.0f };
        float debugNativeAnimationMarkerSize{ 1.5f };
        int logLevel{ 2 };
        std::string activePath{};

        [[nodiscard]] bool reload();
    };

    extern ReanimateConfig g_config;
}
