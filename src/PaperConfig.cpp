#include "PaperConfig.h"

#include "PaperLog.h"
#include "PaperSettings.h"

#include <Windows.h>
#include <ShlObj.h>

#include <atomic>
#include <chrono>
#include <cstdint>
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

        // A config for the next processPendingReload; fileRead marks one built
        // after reading PAPER.toml, which is logged as a reload.
        struct PendingConfig
        {
            PaperConfig config;
            bool fileRead{ false };
        };

        std::atomic<std::shared_ptr<const PendingConfig>> s_pendingConfig{};
        std::filesystem::path s_watchedPath{};
        std::jthread s_watcher{};
        // PAPER.toml's store and the preview values, used by the main thread
        // and the watcher thread; the mutex serializes them. A config is built
        // and published under it, so the last one built is the one applied.
        std::mutex s_storeMutex;
        std::unique_ptr<RPS::Settings::Store> s_store;
        // Under s_storeMutex: Mods Config's preview values (canonical, by
        // settingPath), a revision bumped with each change of them, and the
        // store and preview revisions the last config was built from.
        RPS::Settings::LoadedValues s_previewValues;
        std::uint64_t s_previewRevision{ 0 };
        std::uint64_t s_builtStoreRevision{ 0 };
        std::uint64_t s_builtPreviewRevision{ 0 };

        [[nodiscard]] std::filesystem::path resolveConfigDirectory()
        {
            PWSTR documents{};
            if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents))) {
                return {};
            }
            const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(documents, &CoTaskMemFree);
            return std::filesystem::path(documents) / L"My Games" / L"Fallout4VR" / L"Mods_Config" / L"PAPER";
        }

        // Under s_storeMutex, with the store made: the file's values with the
        // previews laid over them.
        [[nodiscard]] PaperConfig buildLocked()
        {
            s_builtStoreRevision = s_store->revision();
            s_builtPreviewRevision = s_previewRevision;
            return settings::fromStore(*s_store, s_previewValues);
        }

        // Under s_storeMutex. A file read still waiting keeps its mark when a
        // preview replaces it.
        void publishLocked(PaperConfig config, bool fileRead)
        {
            const auto waiting = s_pendingConfig.load(std::memory_order_acquire);
            fileRead = fileRead || (waiting && waiting->fileRead);
            s_pendingConfig.store(
                std::make_shared<const PendingConfig>(PendingConfig{ std::move(config), fileRead }),
                std::memory_order_release);
        }

        // Under s_storeMutex. Reads PAPER.toml; startup also organizes it and
        // migrates PAPER.ini once. A file that cannot be read gives nothing, so
        // the last values stay; with onlyIfChanged so does a read that changed
        // neither the file's values nor the previews since the last build.
        [[nodiscard]] std::optional<PaperConfig> loadLocked(const bool startup, const bool onlyIfChanged)
        {
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
                if (onlyIfChanged && s_store->revision() == s_builtStoreRevision &&
                    s_previewRevision == s_builtPreviewRevision) {
                    return std::nullopt;
                }
                return buildLocked();
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
            std::shared_ptr<const PendingConfig>{}, std::memory_order_release);
        std::optional<PaperConfig> loaded;
        {
            std::scoped_lock lock(s_storeMutex);
            loaded = loadLocked(true, false);
        }
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
                std::scoped_lock lock(s_storeMutex);
                if (auto loaded = loadLocked(false, true)) {
                    publishLocked(std::move(*loaded), true);
                }
            }
        });
    }

    bool PaperConfig::processPendingReload()
    {
        auto pending = s_pendingConfig.exchange(
            std::shared_ptr<const PendingConfig>{}, std::memory_order_acq_rel);
        if (!pending) {
            return false;
        }

        *this = pending->config;
        logger::setLevel(logLevel);
        if (pending->fileRead) {
            logLoadedConfig(*this, true);
        } else {
            PAPER_LOG_DEBUG(Config, "Applied Mods Config preview values");
        }
        return true;
    }

    bool PaperConfig::setPreviewValue(std::string_view table, std::string_view key, std::string_view value)
    {
        {
            std::scoped_lock lock(s_storeMutex);
            const auto* setting = s_store ? s_store->find(table, key) : nullptr;
            const auto canonical = setting ? RPS::Settings::normalizeValue(setting->type, value) : std::nullopt;
            if (canonical) {
                auto path = RPS::Settings::settingPath(table, key);
                if (const auto found = s_previewValues.find(path);
                    found != s_previewValues.end() && found->second == *canonical) {
                    return true;
                }
                s_previewValues.insert_or_assign(std::move(path), *canonical);
                ++s_previewRevision;
                publishLocked(buildLocked(), false);
                return true;
            }
        }
        PAPER_LOG_WARN(Config, "Preview of [{}] {} = '{}' ignored: no such option, or not a value of it", table, key, value);
        return false;
    }

    bool PaperConfig::discardPreviewValue(std::string_view table, std::string_view key)
    {
        std::scoped_lock lock(s_storeMutex);
        const auto found = s_previewValues.find(RPS::Settings::settingPath(table, key));
        if (found == s_previewValues.end()) {
            return false;
        }
        s_previewValues.erase(found);
        ++s_previewRevision;
        publishLocked(buildLocked(), false);
        return true;
    }

    std::size_t PaperConfig::clearPreviewValues()
    {
        std::scoped_lock lock(s_storeMutex);
        const auto removed = s_previewValues.size();
        if (removed == 0) {
            return 0;
        }
        s_previewValues.clear();
        ++s_previewRevision;
        publishLocked(buildLocked(), false);
        return removed;
    }

    void PaperConfig::reloadAfterSave()
    {
        std::scoped_lock lock(s_storeMutex);
        if (!s_previewValues.empty()) {
            s_previewValues.clear();
            ++s_previewRevision;
        }
        if (auto loaded = loadLocked(false, true)) {
            publishLocked(std::move(*loaded), true);
        } else if (s_store && s_previewRevision != s_builtPreviewRevision) {
            // The file could not be read: the previews still end, over the last values read.
            publishLocked(buildLocked(), false);
        }
    }
}
