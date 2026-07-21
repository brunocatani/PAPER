#pragma once

#include <string>

namespace rock_reanimate
{
    struct ReanimateConfig
    {
        bool enabled{ true };
        bool nativeReloadAnimationAuthorityTestEnabled{ true };
        bool nativeReloadAnimationPartialAuthorityTestEnabled{ true };
        int logLevel{ 2 };
        std::string activePath{};

        [[nodiscard]] bool reload();
    };

    extern ReanimateConfig g_config;
}
