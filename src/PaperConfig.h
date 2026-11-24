#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace paper
{
    // PAPER's settings as the runtime reads them, from PAPER.toml with Mods
    // Config FO4 VR's live previews laid over the file's values.
    //
    // Threads: the game's main thread runs reload() (F4SE messages) and every
    // preview call, the read after a Save included (Mods Config calls only
    // there); the watcher thread reads the file after it changes on disk. Both
    // build a new PaperConfig under one mutex
    // and publish it as the pending config; the frame thread (ROCK's BeforeRock
    // phase, also the main thread) takes it in processPendingReload, the only
    // place g_config changes after a load. Previews never write a file.
    struct PaperConfig
    {
        bool manualReloadOnly{ true };
        float handBlendSeconds{ 0.15f };
        bool slideOpenOnEmpty{ true };
        float slideCloseSeconds{ 0.15f };
        bool chamberRound{ true };
        bool boltAppreciation{ false };
        float boltPullDistance{ 7.0f };
        bool debugDrawNativeAnimation{ false };
        bool debugDrawNativeAnimationText{ true };
        float debugNativeAnimationAxisLength{ 5.0f };
        float debugNativeAnimationMarkerSize{ 1.5f };
        int logLevel{ 2 };
        std::string activePath{};

        [[nodiscard]] bool operator==(const PaperConfig&) const = default;

        [[nodiscard]] bool reload();
        void startWatching();
        [[nodiscard]] bool processPendingReload();

        // Mods Config's live preview of one option, by its TOML table and key;
        // false, and logged, for an option PAPER lacks or a value it cannot hold.
        [[nodiscard]] static bool setPreviewValue(std::string_view table, std::string_view key, std::string_view value);
        // Ends one option's preview; false when it had none.
        static bool discardPreviewValue(std::string_view table, std::string_view key);
        // Ends every preview and returns how many there were.
        static std::size_t clearPreviewValues();
        // After a Save: ends every preview and reads PAPER.toml at once, so the
        // saved values follow the previews without the watcher's delay.
        static void reloadAfterSave();
    };

    extern PaperConfig g_config;
}
