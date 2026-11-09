#pragma once

#include <string>

namespace paper
{
    struct PaperConfig
    {
        bool enabled{ true };
        bool manualReloadOnly{ true };
        float handBlendSeconds{ 0.15f };
        bool boltHoldOpen{ true };
        float boltHoldCloseSeconds{ 0.15f };
        bool chamberRound{ true };
        bool boltAppreciation{ false };
        float boltPullDistance{ 7.0f };
        bool boltHandFromAnimation{ true };
        bool debugDrawNativeAnimation{ false };
        bool debugDrawNativeAnimationText{ true };
        float debugNativeAnimationAxisLength{ 5.0f };
        float debugNativeAnimationMarkerSize{ 1.5f };
        int logLevel{ 2 };
        std::string activePath{};

        [[nodiscard]] bool reload();
        void startWatching();
        [[nodiscard]] bool processPendingReload();
    };

    extern PaperConfig g_config;
}
