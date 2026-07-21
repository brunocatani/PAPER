#include "ReanimateConfig.h"

#include "ReanimateLog.h"

#include <Windows.h>
#include <ShlObj.h>
#include <SimpleIni.h>

#include <filesystem>

namespace rock_reanimate
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
                    R"(\My Games\Fallout4VR\ROCK_Reanimate_Config\ROCK_Reanimate.ini)";
            }
            return R"(Data\ROCK_Reanimate_Config\ROCK_Reanimate.ini)";
        }
    }

    ReanimateConfig g_config{};

    bool ReanimateConfig::reload()
    {
        activePath = resolveActiveIniPath();
        CSimpleIniA ini;
        ini.SetUnicode();
        const SI_Error result = ini.LoadFile(activePath.c_str());
        if (result < 0) {
            REANIMATE_LOG_WARN(
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
        logger::setLevel(logLevel);
        REANIMATE_LOG_INFO(
            Config,
            "Loaded '{}' enabled={} nativeReload={} partialAuthority={}",
            activePath,
            enabled,
            nativeReloadAnimationAuthorityTestEnabled,
            nativeReloadAnimationPartialAuthorityTestEnabled);
        return true;
    }
}
