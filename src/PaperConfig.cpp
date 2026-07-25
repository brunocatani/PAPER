#include "PaperConfig.h"

#include "PaperConfigFile.h"
#include "PaperDefaultIni.h"
#include "PaperLog.h"

#include <Windows.h>
#include <ShlObj.h>
#include <SimpleIni.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace paper
{
    namespace
    {
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
                    R"(\My Games\Fallout4VR\PAPER_Config\PAPER.ini)";
            }
            return R"(Data\PAPER_Config\PAPER.ini)";
        }
    }

    PaperConfig g_config{};

    bool PaperConfig::reload()
    {
        activePath = resolveActiveIniPath();
        const auto ensureResult = config_file::ensureFileExists(
            std::filesystem::path(activePath),
            config_defaults::kIni);
        if (ensureResult.status == config_file::EnsureStatus::Created) {
            PAPER_LOG_INFO(Config, "Created default INI at '{}'", activePath);
        } else if (ensureResult.status == config_file::EnsureStatus::Failed) {
            PAPER_LOG_WARN(
                Config,
                "Could not create missing INI at '{}': {}; retaining safe compiled defaults if loading also fails",
                activePath,
                ensureResult.error.message());
        }

        CSimpleIniA ini;
        ini.SetUnicode();
        const SI_Error result = ini.LoadFile(activePath.c_str());
        if (result < 0) {
            PAPER_LOG_WARN(
                Config,
                "Could not load '{}'; retaining safe compiled defaults",
                activePath);
            logger::setLevel(logLevel);
            return false;
        }

        enabled = ini.GetBoolValue("Main", "bEnabled", enabled);
        logLevel = static_cast<int>(
            ini.GetLongValue("Main", "iLogLevel", logLevel));
        nativeReloadAnimationAuthorityTestEnabled = ini.GetBoolValue(
            "NativeAnimation",
            "bNativeReloadAnimationAuthorityTestEnabled",
            nativeReloadAnimationAuthorityTestEnabled);
        nativeReloadAnimationPartialAuthorityTestEnabled = ini.GetBoolValue(
            "NativeAnimation",
            "bNativeReloadAnimationPartialAuthorityTestEnabled",
            nativeReloadAnimationPartialAuthorityTestEnabled);
        debugDrawNativeAnimation = ini.GetBoolValue(
            "Debug",
            "bDebugDrawNativeAnimation",
            debugDrawNativeAnimation);
        debugDrawNativeAnimationText = ini.GetBoolValue(
            "Debug",
            "bDebugDrawNativeAnimationText",
            debugDrawNativeAnimationText);
        debugNativeAnimationAxisLength = static_cast<float>(ini.GetDoubleValue(
            "Debug",
            "fDebugNativeAnimationAxisLength",
            debugNativeAnimationAxisLength));
        debugNativeAnimationMarkerSize = static_cast<float>(ini.GetDoubleValue(
            "Debug",
            "fDebugNativeAnimationMarkerSize",
            debugNativeAnimationMarkerSize));
        if (!std::isfinite(debugNativeAnimationAxisLength)) {
            debugNativeAnimationAxisLength = 5.0f;
        }
        if (!std::isfinite(debugNativeAnimationMarkerSize)) {
            debugNativeAnimationMarkerSize = 1.5f;
        }
        debugNativeAnimationAxisLength = std::clamp(
            debugNativeAnimationAxisLength,
            1.0f,
            20.0f);
        debugNativeAnimationMarkerSize = std::clamp(
            debugNativeAnimationMarkerSize,
            0.25f,
            5.0f);
        logger::setLevel(logLevel);
        PAPER_LOG_INFO(
            Config,
            "Loaded '{}' enabled={} nativeReload={} partialAuthority={} debugNativeAnimation={} debugText={} debugAxis={:.2f} debugMarker={:.2f}",
            activePath,
            enabled,
            nativeReloadAnimationAuthorityTestEnabled,
            nativeReloadAnimationPartialAuthorityTestEnabled,
            debugDrawNativeAnimation,
            debugDrawNativeAnimationText,
            debugNativeAnimationAxisLength,
            debugNativeAnimationMarkerSize);
        return true;
    }
}
