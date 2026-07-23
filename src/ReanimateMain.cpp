#include "PCH.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation/NativeAnimationAuthorityPolicy.h"
#include "api/ApiTransform.h"
#include "api/ROCKReanimateProvider.h"
#include "api/RockApiClient.h"
#include "api/RockVisualAuthorityBridge.h"
#include "debug/NativeAnimationDebugVisualization.h"
#include "ReanimateConfig.h"
#include "ReanimateLog.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace
{
    using namespace rock_reanimate;

    const F4SE::MessagingInterface* s_messaging{ nullptr };
    std::atomic<bool> s_gameLoaded{ false };
    std::atomic<bool> s_hookAttempted{ false };
    std::uint64_t s_configRevision{ 0 };
    std::uint32_t s_lastAuthorityPublishFailureFlags{ UINT32_MAX };
    bool s_runtimeOperational{ false };
    rock::provider::RockProviderEquippedWeaponGripStateV1 s_gripState{};

    [[nodiscard]] bool hasContextFlag(
        const std::uint32_t flags,
        const rock::provider::RockProviderAnimationPhaseContextFlagV1 flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool hasGripFlag(
        const std::uint32_t flags,
        const rock::provider::RockProviderEquippedWeaponGripStateFlagV1 flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    void publishConfigState()
    {
        api::ReanimateConfigStateV1 state{};
        state.enabled = g_config.enabled ? 1u : 0u;
        state.nativeReloadAuthorityEnabled =
            g_config.nativeReloadAnimationAuthorityTestEnabled ? 1u : 0u;
        state.partialReloadAuthorityEnabled =
            g_config.nativeReloadAnimationPartialAuthorityTestEnabled ? 1u : 0u;
        state.logLevel = g_config.logLevel;
        state.revision = ++s_configRevision;
        provider::publishConfig(state);
        provider::dispatchEvent(api::ReanimateEventKindV1::ConfigReloaded);
    }

    void refreshGripState()
    {
        s_gripState = {};
        const bool queried =
            rockApiClient().queryEquippedWeaponGripState(s_gripState);
        const bool valid = queried && hasGripFlag(
            s_gripState.flags,
            rock::provider::RockProviderEquippedWeaponGripStateFlagV1::Valid);
        const bool manualCycleEligible =
            native_animation_authority_policy::canApplyManualCycleHandAnimation(
                native_animation_authority_policy::ManualCycleHandAnimationEligibility{
                    .gripStateValid = valid,
                    .firingHandIsLeft = hasGripFlag(
                        s_gripState.flags,
                        rock::provider::RockProviderEquippedWeaponGripStateFlagV1::FiringHandLeft),
                });
        native_animation_authority::setManualCycleHandAnimationEligible(
            manualCycleEligible);

        native_animation_authority::ManualCycleRockGripBaselines baselines{};
        if (valid && hasGripFlag(
                s_gripState.flags,
                rock::provider::RockProviderEquippedWeaponGripStateFlagV1::RightHandInWeaponValid)) {
            baselines.rightHandInWeapon =
                api_transform::toNi(s_gripState.rightHandInWeapon);
            baselines.rightValid = true;
        }
        if (valid && hasGripFlag(
                s_gripState.flags,
                rock::provider::RockProviderEquippedWeaponGripStateFlagV1::LeftHandInWeaponValid)) {
            baselines.leftHandInWeapon =
                api_transform::toNi(s_gripState.leftHandInWeapon);
            baselines.leftValid = true;
        }
        native_animation_authority::setManualCycleRockGripBaselines(baselines);
    }

    void configureRuntime(const bool operational)
    {
        native_animation_authority::setRuntimeEnabled(operational);
        native_animation_authority::setLocalManualCycleTestEnabled(
            operational &&
            g_config.nativeReloadAnimationAuthorityTestEnabled);
        native_animation_authority::setLocalReloadTestEnabled(
            operational &&
            g_config.nativeReloadAnimationAuthorityTestEnabled);
        native_animation_authority::setLocalReloadPartialAuthorityEnabled(
            operational &&
            g_config.nativeReloadAnimationAuthorityTestEnabled &&
            g_config.nativeReloadAnimationPartialAuthorityTestEnabled);
        s_runtimeOperational = operational;
    }

    void publishRockAuthorityFlags(const std::uint32_t requestedFlags)
    {
        if (rockApiClient().setNativeAnimationAuthority(requestedFlags)) {
            s_lastAuthorityPublishFailureFlags = UINT32_MAX;
            return;
        }
        if (s_lastAuthorityPublishFailureFlags != requestedFlags) {
            REANIMATE_LOG_ERROR(
                Api,
                "Could not publish native animation authority flags=0x{:X} to ROCK",
                requestedFlags);
            s_lastAuthorityPublishFailureFlags = requestedFlags;
        }
    }

    void publishRockAuthority(const bool operational)
    {
        publishRockAuthorityFlags(operational ?
            native_animation_authority::currentLocalAuthorityFlags() |
                provider::currentConsumerAuthorityFlags() :
            0);
    }

    void publishRuntimeState(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
        const bool rockConnected,
        const bool skeletonReady)
    {
        const auto native = native_animation_authority::queryRuntimeStatus();
        const auto localFlags =
            native_animation_authority::currentLocalAuthorityFlags();
        const auto consumerFlags = provider::currentConsumerAuthorityFlags();

        rock::provider::RockProviderNativeAnimationRuntimePublicationV1 rockState{};
        rockState.statusFlags = native.statusFlags;
        rockState.capturedTransformCount = native.capturedTransformCount;
        rockState.captureSequence = native.captureSequence;
        rockState.worldGeneration = context.worldGeneration;
        rockState.skeletonGeneration = context.skeletonGeneration;
        rockState.providerGeneration = context.providerGeneration;
        if (rockConnected) {
            (void)rockApiClient().publishNativeAnimationRuntime(rockState);
        }

        api::ReanimateRuntimeStateV1 state{};
        state.statusFlags = static_cast<std::uint32_t>(
            api::ReanimateRuntimeStatusFlagV1::ProviderReady);
        if (rockConnected) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::RockConnected);
        }
        if (skeletonReady) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::RockSkeletonReady);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::HookInstalled)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::HookInstalled);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::RuntimeEnabled)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::RuntimeEnabled);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::CaptureValid)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::CaptureValid);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::HookInstallFailed)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::HookInstallFailed);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::ThreadMismatch)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::ThreadMismatch);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::CaptureFault)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::CaptureFault);
        }
        if (native.reloadEventActive) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::ReloadEventActive);
        }
        if (native.localManualCycleLeaseActive) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::ReanimateRuntimeStatusFlagV1::ManualCycleActive);
        }
        state.activeAuthorityFlags = native.effectiveFlags;
        state.localAuthorityFlags = localFlags;
        state.consumerAuthorityFlags = consumerFlags;
        state.capturedTransformCount = native.capturedTransformCount;
        state.weaponFormId = s_gripState.weaponFormId;
        state.captureSequence = native.captureSequence;
        state.reloadStartSequence = native.reloadStartSequence;
        state.reloadEndSequence = native.reloadEndSequence;
        state.frameIndex = context.frameIndex;
        state.weaponGenerationKey = s_gripState.weaponGenerationKey;
        state.worldGeneration = context.worldGeneration;
        state.skeletonGeneration = context.skeletonGeneration;
        state.rockProviderGeneration = context.providerGeneration;
        state.reanimateProviderGeneration = provider::generation();
        provider::publishRuntime(state);
    }

    void ROCK_PROVIDER_CALL onRockAnimationPhase(
        const rock::provider::RockProviderAnimationPhaseContextV1* context,
        void*)
    {
        if (!context || !s_gameLoaded.load(std::memory_order_acquire)) {
            return;
        }

        const bool rockEnabled = hasContextFlag(
            context->flags,
            rock::provider::RockProviderAnimationPhaseContextFlagV1::RockEnabled);
        const bool rockReady = hasContextFlag(
            context->flags,
            rock::provider::RockProviderAnimationPhaseContextFlagV1::ProviderReady);
        const bool skeletonReady = hasContextFlag(
            context->flags,
            rock::provider::RockProviderAnimationPhaseContextFlagV1::SkeletonReady);
        frik_visual_authority::setSkeletonReadyHint(skeletonReady);

        const bool operational =
            g_config.enabled && rockEnabled && rockReady && skeletonReady;
        switch (context->phase) {
        case rock::provider::RockProviderAnimationPhaseV1::NativeGraphOutput: {
            const bool runtimeOperational =
                operational && s_runtimeOperational;
            // Publish before capture so ROCK's local authored-grip reader can
            // yield at this same graph sample when Reanimate owns authority.
            publishRockAuthority(runtimeOperational);
            native_animation_authority::captureNativeGraphOutput();
            break;
        }
        case rock::provider::RockProviderAnimationPhaseV1::BeforeRock: {
            provider::beginFrame(context->frameIndex);

            if (operational &&
                !s_hookAttempted.exchange(true, std::memory_order_acq_rel)) {
                if (!native_animation_authority::installEventHooks()) {
                    REANIMATE_LOG_CRITICAL(
                        Init,
                        "Native animation lifecycle hooks failed validation; Reanimate authority remains disabled");
                }
            }

            const bool runtimeOperational =
                operational && native_animation_authority::isHookInstalled();
            configureRuntime(runtimeOperational);
            // Remove this provider's preceding aggregate before advancing the
            // local lifecycle. Re-publish only consumer requests first so a
            // just-ended local lease cannot keep itself alive through ROCK's
            // aggregate state.
            rockApiClient().clearNativeAnimationAuthority();
            refreshGripState();
            publishRockAuthorityFlags(runtimeOperational ?
                provider::currentConsumerAuthorityFlags() :
                0);
            native_animation_authority::beginRockFrame(context->deltaSeconds);
            publishRockAuthority(runtimeOperational);
            (void)native_animation_authority::applyCapturedPose(
                native_animation_authority::ApplyPhase::BeforeRock);
            break;
        }
        case rock::provider::RockProviderAnimationPhaseV1::AfterRock:
            refreshGripState();
            (void)native_animation_authority::applyCapturedPose(
                native_animation_authority::ApplyPhase::AfterRock);
            debug_visualization::publish(*context, s_gripState);
            break;
        case rock::provider::RockProviderAnimationPhaseV1::Complete:
            native_animation_authority::completeRockFrame();
            publishRuntimeState(*context, rockApiClient().ready(), skeletonReady);
            provider::dispatchEvent(api::ReanimateEventKindV1::FrameComplete);
            provider::completeFrame();
            break;
        default:
            break;
        }
    }

    bool connectRock()
    {
        if (!rockApiClient().initialize()) {
            REANIMATE_LOG_ERROR(
                Api,
                "ROCK V1 connection unavailable; Reanimate will retry on the next game-session message");
            return false;
        }
        if (!rockApiClient().registerAnimationPhaseCallback(
                &onRockAnimationPhase,
                nullptr)) {
            REANIMATE_LOG_ERROR(
                Api,
                "Could not register ROCK animation-phase callback");
            return false;
        }
        REANIMATE_LOG_INFO(
            Api,
            "Connected to ROCK V1 animation coordination surface");
        return true;
    }

    void resetSession()
    {
        debug_visualization::clear();
        rockApiClient().clearNativeAnimationAuthority();
        native_animation_authority::resetTransientState();
        frik_visual_authority::setSkeletonReadyHint(false);
        s_gripState = {};
        s_lastAuthorityPublishFailureFlags = UINT32_MAX;
        s_runtimeOperational = false;
        provider::resetRuntime();
    }

    void onF4SEMessage(F4SE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        if (message->type == F4SE::MessagingInterface::kGameLoaded) {
            (void)g_config.reload();
            publishConfigState();
            s_gameLoaded.store(true, std::memory_order_release);
            (void)connectRock();
            REANIMATE_LOG_INFO(
                Init,
                "GameLoaded complete; waiting for ROCK skeleton-ready phases");
            return;
        }

        if (message->type == F4SE::MessagingInterface::kPreLoadGame) {
            resetSession();
            return;
        }

        if (message->type == F4SE::MessagingInterface::kPostLoadGame ||
            message->type == F4SE::MessagingInterface::kNewGame) {
            resetSession();
            (void)g_config.reload();
            publishConfigState();
            (void)connectRock();
        }
    }
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(
    const F4SE::QueryInterface* f4se,
    F4SE::PluginInfo* info)
{
    rock_reanimate::logger::init();
    REANIMATE_LOG_INFO(Init, "=== ROCK_Reanimate v{} query ===", Version::NAME);

    info->infoVersion = F4SE::PluginInfo::kVersion;
    info->name = "ROCK_Reanimate";
    info->version =
        static_cast<std::uint32_t>(Version::MAJOR * 10000 +
            Version::MINOR * 100 + Version::PATCH);

    if (f4se->IsEditor()) {
        REANIMATE_LOG_CRITICAL(Init, "Editor runtime is unsupported");
        return false;
    }
    if (!REL::Module::IsVR()) {
        REANIMATE_LOG_CRITICAL(Init, "Fallout 4 VR runtime is required");
        return false;
    }

    const auto requiredRuntime = F4SE::RUNTIME_LATEST_VR;
    if (f4se->RuntimeVersion() < requiredRuntime) {
        REANIMATE_LOG_CRITICAL(
            Init,
            "Unsupported F4SE runtime {} (need >= {})",
            f4se->RuntimeVersion().string(),
            requiredRuntime.string());
        return false;
    }
    return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(
    const F4SE::LoadInterface* f4se)
{
    F4SE::Init(f4se, false);
    rock_reanimate::provider::initialize();

    s_messaging = F4SE::GetMessagingInterface();
    if (!s_messaging || !s_messaging->RegisterListener(onF4SEMessage)) {
        REANIMATE_LOG_CRITICAL(
            Init,
            "F4SE messaging registration failed");
        rock_reanimate::provider::shutdown();
        return false;
    }

    REANIMATE_LOG_INFO(
        Init,
        "Plugin loaded; provider API V{} ready",
        rock_reanimate::api::ROCK_REANIMATE_API_VERSION);
    return true;
}
