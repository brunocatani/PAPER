#include "PaperConfig.h"

#include "PaperConfigFile.h"
#include "PaperDefaultIni.h"
#include "PaperLog.h"

#include <Windows.h>
#include <ShlObj.h>
#include <SimpleIni.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

namespace paper
{
    namespace
    {
        using namespace std::chrono_literals;

        std::atomic<std::shared_ptr<const PaperConfig>> s_pendingConfig{};
        std::filesystem::path s_watchedPath{};
        std::jthread s_watcher{};

        [[nodiscard]] std::string resolveActiveIniPath()
        {
            char documents[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathA(
                    nullptr,
                    CSIDL_MYDOCUMENTS,
                    nullptr,
                    0,
                    documents))) {
                return std::string(documents) +
                    R"(\My Games\Fallout4VR\Mods_Config\PAPER\PAPER.ini)";
            }
            return {};
        }

        [[nodiscard]] char asciiLower(const char value)
        {
            return value >= 'A' && value <= 'Z' ?
                static_cast<char>(value + ('a' - 'A')) : value;
        }

        [[nodiscard]] std::string normalized(std::string_view value)
        {
            std::string result(value);
            std::transform(
                result.begin(), result.end(), result.begin(), asciiLower);
            return result;
        }

        [[nodiscard]] api::PaperDevelopmentCaptureModeV1 readMode(
            const CSimpleIniA& ini)
        {
            const auto value = normalized(ini.GetValue(
                "DevelopmentCapture", "sMaximumMode", "User"));
            if (value == "observe") {
                return api::PaperDevelopmentCaptureModeV1::Observe;
            }
            if (value == "harvest") {
                return api::PaperDevelopmentCaptureModeV1::Harvest;
            }
            if (value == "capture") {
                return api::PaperDevelopmentCaptureModeV1::Capture;
            }
            return api::PaperDevelopmentCaptureModeV1::User;
        }

        [[nodiscard]] api::PaperWeaponMotionCacheAccessV1 readCacheAccess(
            const CSimpleIniA& ini)
        {
            const auto value = normalized(ini.GetValue(
                "WeaponMotionCache", "sAccess", "Off"));
            if (value == "readonly" || value == "read-only") {
                return api::PaperWeaponMotionCacheAccessV1::ReadOnly;
            }
            if (value == "readwrite" || value == "read-write") {
                return api::PaperWeaponMotionCacheAccessV1::ReadWrite;
            }
            return api::PaperWeaponMotionCacheAccessV1::Off;
        }

        [[nodiscard]] std::optional<PaperConfig> loadConfig(
            const std::filesystem::path& path)
        {
            CSimpleIniA ini;
            ini.SetUnicode();
            if (ini.LoadFile(path.string().c_str()) < 0) {
                return std::nullopt;
            }

            PaperConfig config{};
            config.activePath = path.string();
            config.enabled = ini.GetBoolValue("Main", "bEnabled", config.enabled);
            config.logLevel = static_cast<int>(
                ini.GetLongValue("Main", "iLogLevel", config.logLevel));
            config.manualReloadOnly = ini.GetBoolValue(
                "Reload", "bManualReloadOnly", config.manualReloadOnly);
            config.debugDrawNativeAnimation = ini.GetBoolValue(
                "Debug",
                "bDebugDrawNativeAnimation",
                config.debugDrawNativeAnimation);
            config.debugDrawNativeAnimationText = ini.GetBoolValue(
                "Debug",
                "bDebugDrawNativeAnimationText",
                config.debugDrawNativeAnimationText);
            config.debugNativeAnimationAxisLength = static_cast<float>(
                ini.GetDoubleValue(
                    "Debug",
                    "fDebugNativeAnimationAxisLength",
                    config.debugNativeAnimationAxisLength));
            config.debugNativeAnimationMarkerSize = static_cast<float>(
                ini.GetDoubleValue(
                    "Debug",
                    "fDebugNativeAnimationMarkerSize",
                    config.debugNativeAnimationMarkerSize));
            config.developmentCaptureMode = readMode(ini);
            config.developmentCaptureAutoStart = ini.GetBoolValue(
                "DevelopmentCapture",
                "bAutoStart",
                config.developmentCaptureAutoStart);
            config.developmentCaptureAllowApiActivation = ini.GetBoolValue(
                "DevelopmentCapture",
                "bAllowApiActivation",
                config.developmentCaptureAllowApiActivation);
            config.weaponMotionCacheAccess = readCacheAccess(ini);
            config.weaponMotionSessionCacheMiB = static_cast<std::uint32_t>(
                std::clamp<long>(
                    ini.GetLongValue(
                        "WeaponMotionCache",
                        "iSessionCacheMiB",
                        config.weaponMotionSessionCacheMiB),
                    8,
                    256));
            config.weaponMotionDiskCacheMiB = static_cast<std::uint32_t>(
                std::clamp<long>(
                    ini.GetLongValue(
                        "WeaponMotionCache",
                        "iDiskCacheMiB",
                        config.weaponMotionDiskCacheMiB),
                    32,
                    2048));
            config.weaponMotionMaximumFileMiB = static_cast<std::uint32_t>(
                std::clamp<long>(
                    ini.GetLongValue(
                        "WeaponMotionCache",
                        "iMaximumFileMiB",
                        config.weaponMotionMaximumFileMiB),
                    1,
                    64));
            config.weaponMotionMaximumFileMiB = (std::min)(
                config.weaponMotionMaximumFileMiB,
                config.weaponMotionDiskCacheMiB);
            config.weaponMotionMaximumEntries = static_cast<std::uint32_t>(
                std::clamp<long>(
                    ini.GetLongValue(
                        "WeaponMotionCache",
                        "iMaximumEntries",
                        config.weaponMotionMaximumEntries),
                    16,
                    2048));
            if (!std::isfinite(config.debugNativeAnimationAxisLength)) {
                config.debugNativeAnimationAxisLength = 5.0f;
            }
            if (!std::isfinite(config.debugNativeAnimationMarkerSize)) {
                config.debugNativeAnimationMarkerSize = 1.5f;
            }
            config.debugNativeAnimationAxisLength = std::clamp(
                config.debugNativeAnimationAxisLength, 1.0f, 20.0f);
            config.debugNativeAnimationMarkerSize = std::clamp(
                config.debugNativeAnimationMarkerSize, 0.25f, 5.0f);
            return config;
        }

        void logLoadedConfig(const PaperConfig& config, const bool hotReload)
        {
            PAPER_LOG_INFO(
                Config,
                "{} '{}' enabled={} manualReloadOnly={} captureMode={} autoStart={} apiActivation={} hotReload=enabled cacheAccess={} sessionMiB={} diskMiB={} maxFileMiB={} maxEntries={}",
                hotReload ? "Hot-reloaded" : "Loaded",
                config.activePath,
                config.enabled,
                config.manualReloadOnly,
                static_cast<std::uint32_t>(config.developmentCaptureMode),
                config.developmentCaptureAutoStart,
                config.developmentCaptureAllowApiActivation,
                static_cast<std::uint32_t>(config.weaponMotionCacheAccess),
                config.weaponMotionSessionCacheMiB,
                config.weaponMotionDiskCacheMiB,
                config.weaponMotionMaximumFileMiB,
                config.weaponMotionMaximumEntries);
        }
    }

    PaperConfig g_config{};

    bool PaperConfig::reload()
    {
        s_pendingConfig.store(
            std::shared_ptr<const PaperConfig>{}, std::memory_order_release);
        const auto resolvedPath = resolveActiveIniPath();
        PaperConfig safeDefaults{};
        safeDefaults.activePath = resolvedPath;
        activePath = resolvedPath;
        if (activePath.empty()) {
            PAPER_LOG_WARN(Config, "Could not resolve Documents; retaining compiled defaults without configuration I/O");
            *this = std::move(safeDefaults);
            logger::setLevel(logLevel);
            return false;
        }
        const auto ensureResult = config_file::ensureFileExists(
            std::filesystem::path(activePath), config_defaults::kIni);
        if (ensureResult.status == config_file::EnsureStatus::Created) {
            PAPER_LOG_INFO(Config, "Created default INI at '{}'", activePath);
        } else if (ensureResult.status == config_file::EnsureStatus::Failed) {
            PAPER_LOG_WARN(
                Config,
                "Could not create missing INI at '{}': {}; retaining safe compiled defaults if loading also fails",
                activePath,
                ensureResult.error.message());
        }

        auto loaded = loadConfig(activePath);
        if (!loaded) {
            PAPER_LOG_WARN(
                Config,
                "Could not load '{}'; retaining safe compiled defaults",
                activePath);
            *this = std::move(safeDefaults);
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
                if (auto loaded = loadConfig(s_watchedPath)) {
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

        auto next = *pending;
        const bool storageBoundsChanged =
            next.weaponMotionSessionCacheMiB != weaponMotionSessionCacheMiB ||
            next.weaponMotionDiskCacheMiB != weaponMotionDiskCacheMiB ||
            next.weaponMotionMaximumFileMiB != weaponMotionMaximumFileMiB ||
            next.weaponMotionMaximumEntries != weaponMotionMaximumEntries;
        next.weaponMotionSessionCacheMiB = weaponMotionSessionCacheMiB;
        next.weaponMotionDiskCacheMiB = weaponMotionDiskCacheMiB;
        next.weaponMotionMaximumFileMiB = weaponMotionMaximumFileMiB;
        next.weaponMotionMaximumEntries = weaponMotionMaximumEntries;
        *this = std::move(next);
        logger::setLevel(logLevel);
        if (storageBoundsChanged) {
            PAPER_LOG_WARN(
                Config,
                "Weapon motion cache size limits changed during hot reload; the new limits will apply next session");
        }
        logLoadedConfig(*this, true);
        return true;
    }
}
