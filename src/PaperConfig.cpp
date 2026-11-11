#include "PaperConfig.h"

#include "PaperLog.h"
#include "PaperSettings.h"

#include <Windows.h>
#include <ShlObj.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace paper
{
    namespace
    {
        using namespace std::chrono_literals;

        std::atomic<std::shared_ptr<const PaperConfig>> s_pendingConfig{};
        std::filesystem::path s_watchedPath{};
        std::jthread s_watcher{};
        // PAPER.toml's store, read by reload() on the game thread and by the
        // watcher thread; the mutex serializes them.
        std::mutex s_storeMutex;
        std::unique_ptr<RPS::Settings::Store> s_store;

        [[nodiscard]] std::filesystem::path resolveConfigDirectory()
        {
            PWSTR documents{};
            if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents))) {
                return {};
            }
            const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(documents, &CoTaskMemFree);
            return std::filesystem::path(documents) / L"My Games" / L"Fallout4VR" / L"Mods_Config" / L"PAPER";
        }

        // Reads PAPER.toml; startup also organizes it and migrates PAPER.ini once.
        // A file that cannot be read gives nothing, so the last values stay.
        [[nodiscard]] std::optional<PaperConfig> loadConfig(const bool startup)
        {
            std::scoped_lock lock(s_storeMutex);
            try {
                if (!s_store) {
                    const auto directory = resolveConfigDirectory();
                    if (directory.empty()) {
                        PAPER_LOG_WARN(Config, "Could not resolve Documents; retaining compiled defaults without configuration I/O");
                        return std::nullopt;
                    }
                    s_store = settings::makeStore(directory);
                }
                s_store->load(startup);
                for (const auto& notice : s_store->notices()) {
                    PAPER_LOG_INFO(Config, "{}", notice);
                }
                for (const auto& message : s_store->messages()) {
                    PAPER_LOG_WARN(Config, "{}", message);
                }
                return settings::fromStore(*s_store);
            } catch (const std::exception& error) {
                PAPER_LOG_WARN(Config, "Could not load PAPER settings: {}", error.what());
                return std::nullopt;
            }
        }

        void logLoadedConfig(const PaperConfig& config, const bool hotReload)
        {
            PAPER_LOG_INFO(
                Config,
                "{} '{}' manualReloadOnly={} handBlendSeconds={:.2f} slideOpenOnEmpty={} slideCloseSeconds={:.2f} chamberRound={} boltAppreciation={} boltPullDistance={:.1f}",
                hotReload ? "Hot-reloaded" : "Loaded",
                config.activePath,
                config.manualReloadOnly,
                config.handBlendSeconds,
                config.slideOpenOnEmpty,
                config.slideCloseSeconds,
                config.chamberRound,
                config.boltAppreciation,
                config.boltPullDistance);
        }
    }

    PaperConfig g_config{};

    bool PaperConfig::reload()
    {
        s_pendingConfig.store(
            std::shared_ptr<const PaperConfig>{}, std::memory_order_release);
        auto loaded = loadConfig(true);
        if (!loaded) {
            PAPER_LOG_WARN(Config, "Retaining safe compiled defaults");
            *this = PaperConfig{};
            // Keep naming the file so the watcher can pick up a later fix.
            if (const auto directory = resolveConfigDirectory(); !directory.empty()) {
                activePath = (directory / settings::kFileName).string();
            }
            logger::setLevel(logLevel);
            return false;
        }
        *this = std::move(*loaded);
        logger::setLevel(logLevel);
        logLoadedConfig(*this, false);
        return true;
    }

    void PaperConfig::startWatching()
    {
        if (s_watcher.joinable() || activePath.empty()) {
            return;
        }
        s_watchedPath = activePath;
        s_watcher = std::jthread([](const std::stop_token stopToken) {
            std::error_code error;
            auto observedWriteTime = std::filesystem::last_write_time(
                s_watchedPath, error);
            while (!stopToken.stop_requested()) {
                std::this_thread::sleep_for(250ms);
                error.clear();
                const auto writeTime = std::filesystem::last_write_time(
                    s_watchedPath, error);
                if (error || writeTime == observedWriteTime) {
                    continue;
                }
                observedWriteTime = writeTime;
                std::this_thread::sleep_for(250ms);
                if (stopToken.stop_requested()) {
                    break;
                }
                if (auto loaded = loadConfig(false)) {
                    s_pendingConfig.store(
                        std::make_shared<const PaperConfig>(std::move(*loaded)),
                        std::memory_order_release);
                }
            }
        });
    }

    bool PaperConfig::processPendingReload()
    {
        auto pending = s_pendingConfig.exchange(
            std::shared_ptr<const PaperConfig>{}, std::memory_order_acq_rel);
        if (!pending) {
            return false;
        }

        *this = *pending;
        logger::setLevel(logLevel);
        logLoadedConfig(*this, true);
        return true;
    }
}
