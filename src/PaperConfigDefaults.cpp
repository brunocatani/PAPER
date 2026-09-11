#include "PaperConfigDefaults.h"

#include "PaperConfig.h"

#include <array>
#include <format>
#include <string_view>

namespace paper::config_defaults
{
    std::string makeDefaultIni()
    {
        const PaperConfig defaults{};
        constexpr std::array<std::string_view, 4> captureModes{
            "User", "Observe", "Harvest", "Capture"
        };
        constexpr std::array<std::string_view, 3> cacheAccessModes{
            "Off", "ReadOnly", "ReadWrite"
        };
        return std::format(R"INI(; PAPER created this file from compiled defaults because it was missing.
; Existing files are preserved. Edit this file to change settings; hot reload
; applies automatically. Cache size limits take effect at the next game session.

[Main]
; Enables PAPER's reload coordination and manual-reload-only behavior.
bEnabled = {}
; Log file: Documents\My Games\Fallout4VR\F4SE\PAPER.log.
; 0=Trace, 1=Debug, 2=Info (normal play), 3=Warning, 4=Error, 5=Critical, 6=Off.
; Includes the selected level and all more severe messages. Info includes
; startup, hook validation, and reload diagnostics. Trace/Debug are more verbose.
; Changes apply automatically; this does not enable capture or debug drawings.
iLogLevel = {}

[Reload]
; Blocks native automatic reload after ammunition reaches zero, including
; immediate, delayed, and post-fire requests. Manual reload input, ROCK reload
; dispatch, Tactical Reload, and reload resumption remain available.
bManualReloadOnly = {}

[Debug]
; Draws Paper's native weapon frame, native hand targets, ROCK live grip
; targets, applied FRIK hands, and their target-error lines through ROCK's
; stereo debug renderer. Diagnostic only; disabled for normal play.
bDebugDrawNativeAnimation = {}

; Adds concise mode, capture, and per-hand motion/error labels.
bDebugDrawNativeAnimationText = {}

; World-space size in game units for transform axes and cross markers.
fDebugNativeAnimationAxisLength = {:.1f}
fDebugNativeAnimationMarkerSize = {:.1f}

[DevelopmentCapture]
; User keeps all observation, exact animation harvesting, motion compilation,
; learning, and cache work dormant. Observe permits passive telemetry, Harvest
; additionally permits exact off-screen clip sampling, and Capture also permits
; live part-motion learning. This is a hard ceiling for API requests.
sMaximumMode = {}

; Starts the permitted development scopes without a consumer lease. Exact
; harvesting still waits for a compiled-cache miss when cache reads are active.
bAutoStart = {}

; Allows consumer-driven activation: legacy read capabilities and bounded
; DevelopmentCaptureControl leases. Work can never exceed the configured ceiling.
bAllowApiActivation = {}

[WeaponMotionCache]
; Stores only compact, value-only compiled authored paths. Native animation
; graphs and raw sampled clips are never retained. Off performs no cache I/O,
; ReadOnly never creates, touches, prunes, or writes files, and ReadWrite enables
; bounded persistence. User mode keeps the worker dormant regardless.
sAccess = {}

; In-process LRU cache for fast re-equips during this game session.
iSessionCacheMiB = {}

; Global persistent LRU bounds under Mods_Config\PAPER\MotionCache\v1.
; Changes to these bounds during hot reload apply on the next game session.
iDiskCacheMiB = {}
iMaximumFileMiB = {}
iMaximumEntries = {}
)INI",
            defaults.enabled,
            defaults.logLevel,
            defaults.manualReloadOnly,
            defaults.debugDrawNativeAnimation,
            defaults.debugDrawNativeAnimationText,
            defaults.debugNativeAnimationAxisLength,
            defaults.debugNativeAnimationMarkerSize,
            captureModes.at(static_cast<std::size_t>(defaults.developmentCaptureMode)),
            defaults.developmentCaptureAutoStart,
            defaults.developmentCaptureAllowApiActivation,
            cacheAccessModes.at(static_cast<std::size_t>(defaults.weaponMotionCacheAccess)),
            defaults.weaponMotionSessionCacheMiB,
            defaults.weaponMotionDiskCacheMiB,
            defaults.weaponMotionMaximumFileMiB,
            defaults.weaponMotionMaximumEntries);
    }
}
