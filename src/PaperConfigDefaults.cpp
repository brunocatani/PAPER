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
; true blocks automatic reload when ammunition reaches zero, including
; immediate, delayed, and post-fire requests. false (default) allows normal
; automatic reload. Manual input, ROCK reload dispatch, Tactical Reload,
; and reload resumption remain available with either value. While true, each
; trigger pull on an empty gun with rounds to load plays one dry-fire click,
; except on a charging-reload gun such as the laser musket.
bManualReloadOnly = {}

; Seconds the left hand takes to move from your controller or grip onto the
; animation when a reload, pump stroke or cycle takes it, and back when that
; ends. 0 moves it instantly.
fHandBlendSeconds = {:.2f}

; true leaves the bolt or slide open on an empty gun, in either hand, until a
; reload starts or rounds arrive, with the parts that move with it such as a
; tilting barrel. It needs a bolt on the game's standard WeaponBolt bone. The open
; position is read off screen from the weapon's own clips the first time it is
; equipped each session, so a gun none of whose clips open its bolt never holds.
; Manual actions such as bolt, lever, pump and double-barrel guns and revolvers
; never hold.
bBoltHoldOpen = {}

; Seconds the open bolt takes to return to the animation when a reload starts.
; 0 moves it instantly.
fBoltHoldCloseSeconds = {:.2f}

; true keeps a round in the chamber of every gun that fires cartridges, in either
; hand: pistols, rifles, shotguns and machine guns. Energy weapons, the Gauss rifle,
; launchers and other guns with no case to throw out are left alone, and so is a gun
; a weapon mod has changed to such ammo. A round in the chamber when you reload stays
; there on top of the full magazine, as far as your reserve goes, except on revolvers,
; double-barrels and miniguns. Each gun keeps its rounds and chamber when you unequip
; it and in your saves, unless it sits in a stack of identical guns. The ammo counter
; shows a cartridge for the chamber: solid with a round in it, dim without.
bChamberRound = {}

; true makes you work the bolt of a bolt-action rifle by hand after each shot. Use
; the hand the rifle's own cycle animation uses, usually the firing hand: hold the
; gun with your other hand, let go of the firing grip and grip the bolt where the
; small white marker shows. Pull it all the way back to throw out the case and push
; it home to chamber the next round. The trigger does nothing until you have, and
; clicks if you pull it. The reload stays as it was. These rifles keep a chamber as
; bChamberRound describes, whatever that key says. A rifle whose cycle animation shows
; no hand on the bolt keeps its usual cycle. Lever guns, pumps and revolvers are not
; affected.
bBoltAppreciation = {}

; The length of a full pull, in game units, the same on every rifle. 7 is about 9 cm.
; Moving your hand past half of it takes the bolt all the way, at the speed of the
; rifle's own animation. Let go and the bolt stays where it is, open or closed.
fBoltPullDistance = {:.1f}

; true lets the hand the rifle's cycle animation uses take the bolt the moment the
; rifle is drawn, placed and posed the way the animation does as the bolt moves. Your
; other hand takes it where your grip puts it, with the fingers closed on the bolt's
; surface, once the weapon's collision is built, about a second after equipping.
; false treats both hands that way.
bBoltHandFromAnimation = {}

[Debug]
; Draws Paper's native weapon frame, native hand targets, ROCK live grip
; targets, applied FRIK hands, and their target-error lines through RPS UI
; Framework's world drawing. Diagnostic only; disabled for normal play.
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

; Allows other mods to wake permitted work, both by registering the read
; capabilities that need it and through bounded DevelopmentCaptureControl
; leases. Work can never exceed the configured ceiling.
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
            defaults.handBlendSeconds,
            defaults.boltHoldOpen,
            defaults.boltHoldCloseSeconds,
            defaults.chamberRound,
            defaults.boltAppreciation,
            defaults.boltPullDistance,
            defaults.boltHandFromAnimation,
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
