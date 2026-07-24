#include "PCH.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation/NativeAnimationAuthorityPolicy.h"
#include "api/ApiTransform.h"
#include "api/PAPERProvider.h"
#include "api/RockApiClient.h"
#include "api/RockVisualAuthorityBridge.h"
#include "debug/NativeAnimationDebugVisualization.h"
#include "PaperConfig.h"
#include "PaperLog.h"
#include "support/TransformMath.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
    using namespace paper;

    const F4SE::MessagingInterface* s_messaging{ nullptr };
    std::atomic<bool> s_gameLoaded{ false };
    std::atomic<bool> s_hookAttempted{ false };
    std::uint64_t s_configRevision{ 0 };
    std::uint32_t s_lastAuthorityPublishFailureFlags{ UINT32_MAX };
    bool s_runtimeOperational{ false };
    rock::provider::RockProviderEquippedWeaponGripStateV1 s_gripState{};

    struct ManualCycleActionPoseSample
    {
        RE::NiTransform local{};
        std::uint32_t bodyId{ 0x7FFF'FFFF };
        std::uint32_t partKind{ 0 };
        std::uint32_t actionRole{ 0 };
        bool sourceParentLocal{ false };
        bool valid{ false };
    };

    struct ManualCycleActionPoseTracker
    {
        std::array<
            ManualCycleActionPoseSample,
            rock::provider::ROCK_PROVIDER_MAX_WEAPON_BODIES>
            samples{};
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint32_t sampleCount{ 0 };
    };

    struct ManualCycleActionEvidence
    {
        bool present{ false };
        bool moved{ false };
    };

    ManualCycleActionPoseTracker s_manualCycleActionPoseTracker{};

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

    [[nodiscard]] bool hasWeaponPartPoseFlag(
        const std::uint32_t flags,
        const rock::provider::RockProviderWeaponPartPoseFlagV1 flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool isSupportGripKind(
        const rock::provider::RockProviderWeaponPartGripKindV1 kind)
    {
        return kind ==
                   rock::provider::RockProviderWeaponPartGripKindV1::
                       SupportFullAuthority ||
               kind ==
                   rock::provider::RockProviderWeaponPartGripKindV1::
                       SupportVisualOnly;
    }

    [[nodiscard]] bool finiteTransform(const RE::NiTransform& transform)
    {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (!std::isfinite(
                        transform.rotate.entry[row][column])) {
                    return false;
                }
            }
        }
        return std::isfinite(transform.translate.x) &&
               std::isfinite(transform.translate.y) &&
               std::isfinite(transform.translate.z) &&
               std::isfinite(transform.scale) &&
               std::abs(transform.scale) > 0.000001f;
    }

    [[nodiscard]] native_animation_authority_policy::
        ManualCycleHandMotionSample measureActionPartMotion(
            const RE::NiTransform& previous,
            const RE::NiTransform& current)
    {
        const auto delta = transform_math::composeTransforms(
            transform_math::invertTransform(previous),
            current);
        if (!finiteTransform(delta)) {
            return {};
        }
        const float translation = std::sqrt(
            delta.translate.x * delta.translate.x +
            delta.translate.y * delta.translate.y +
            delta.translate.z * delta.translate.z);
        const float trace =
            delta.rotate.entry[0][0] +
            delta.rotate.entry[1][1] +
            delta.rotate.entry[2][2];
        const float cosine = (std::clamp)(
            (trace - 1.0f) * 0.5f,
            -1.0f,
            1.0f);
        constexpr float kRadiansToDegrees = 57.29577951308232f;
        return {
            .translationGameUnits = translation,
            .rotationDegrees =
                std::acos(cosine) * kRadiansToDegrees,
        };
    }

    [[nodiscard]] const ManualCycleActionPoseSample*
        findPreviousActionPose(
            const std::uint32_t bodyId,
            const std::uint32_t partKind,
            const std::uint32_t actionRole,
            const bool sourceParentLocal)
    {
        for (std::uint32_t i = 0;
             i < s_manualCycleActionPoseTracker.sampleCount;
             ++i) {
            const auto& sample =
                s_manualCycleActionPoseTracker.samples[i];
            if (sample.valid &&
                sample.bodyId == bodyId &&
                sample.partKind == partKind &&
                sample.actionRole == actionRole &&
                sample.sourceParentLocal == sourceParentLocal) {
                return &sample;
            }
        }
        return nullptr;
    }

    [[nodiscard]] ManualCycleActionEvidence
        refreshManualCycleActionEvidence(
            const std::uint64_t weaponGenerationKey)
    {
        ManualCycleActionEvidence evidence{};
        std::array<
            rock::provider::RockProviderWeaponPartPoseV1,
            rock::provider::ROCK_PROVIDER_MAX_WEAPON_BODIES>
            poses{};
        std::uint32_t poseCount = 0;
        if (weaponGenerationKey == 0 ||
            !rockApiClient().copyWeaponPartPoses(
                poses,
                poseCount)) {
            s_manualCycleActionPoseTracker = {};
            return evidence;
        }

        ManualCycleActionPoseTracker next{};
        next.weaponGenerationKey = weaponGenerationKey;
        const auto boundedPoseCount = (std::min)(
            poseCount,
            static_cast<std::uint32_t>(poses.size()));
        for (std::uint32_t i = 0; i < boundedPoseCount; ++i) {
            const auto& pose = poses[i];
            if (pose.weaponGenerationKey != weaponGenerationKey ||
                !hasWeaponPartPoseFlag(
                    pose.flags,
                    rock::provider::
                        RockProviderWeaponPartPoseFlagV1::Valid) ||
                !native_animation_authority_policy::
                    isManualCycleSemanticActionPart(
                        pose.partKind,
                        pose.actionRole)) {
                continue;
            }
            evidence.present = true;

            const bool sourceParentLocal =
                hasWeaponPartPoseFlag(
                    pose.flags,
                    rock::provider::
                        RockProviderWeaponPartPoseFlagV1::
                            SourceParentLocalValid);
            const bool weaponRootLocal =
                hasWeaponPartPoseFlag(
                    pose.flags,
                    rock::provider::
                        RockProviderWeaponPartPoseFlagV1::
                            WeaponRootLocalValid);
            if (!sourceParentLocal && !weaponRootLocal) {
                continue;
            }

            ManualCycleActionPoseSample sample{};
            sample.local = api_transform::toNi(
                sourceParentLocal ?
                    pose.sourceParentLocal :
                    pose.weaponRootLocal);
            sample.bodyId = pose.bodyId;
            sample.partKind = pose.partKind;
            sample.actionRole = pose.actionRole;
            sample.sourceParentLocal = sourceParentLocal;
            sample.valid = finiteTransform(sample.local);
            if (!sample.valid) {
                continue;
            }

            if (s_manualCycleActionPoseTracker.weaponGenerationKey ==
                    weaponGenerationKey) {
                const auto* previous = findPreviousActionPose(
                    sample.bodyId,
                    sample.partKind,
                    sample.actionRole,
                    sample.sourceParentLocal);
                if (previous &&
                    native_animation_authority_policy::
                        isManualCycleActionPartMotion(
                            measureActionPartMotion(
                                previous->local,
                                sample.local))) {
                    evidence.moved = true;
                }
            }

            if (next.sampleCount < next.samples.size()) {
                next.samples[next.sampleCount++] = sample;
            }
        }
        s_manualCycleActionPoseTracker = next;
        return evidence;
    }

    void publishConfigState()
    {
        api::PaperConfigStateV1 state{};
        state.enabled = g_config.enabled ? 1u : 0u;
        state.nativeReloadAuthorityEnabled =
            g_config.nativeReloadAnimationAuthorityTestEnabled ? 1u : 0u;
        state.partialReloadAuthorityEnabled =
            g_config.nativeReloadAnimationPartialAuthorityTestEnabled ? 1u : 0u;
        state.logLevel = g_config.logLevel;
        state.revision = ++s_configRevision;
        provider::publishConfig(state);
        provider::dispatchEvent(api::PaperEventKindV1::ConfigReloaded);
    }

    void refreshGripState()
    {
        s_gripState = {};
        const bool queried =
            rockApiClient().queryEquippedWeaponGripState(s_gripState);
        const bool valid = queried && hasGripFlag(
            s_gripState.flags,
            rock::provider::RockProviderEquippedWeaponGripStateFlagV1::Valid);

        const auto actionEvidence =
            refreshManualCycleActionEvidence(
                valid ?
                    s_gripState.weaponGenerationKey :
                    0);
        native_animation_authority::
            setManualCycleWeaponEvidence(
                native_animation_authority::
                    ManualCycleWeaponEvidence{
                        .weaponGenerationKey =
                            valid ?
                                s_gripState.weaponGenerationKey :
                                0,
                        .weaponFormId =
                            valid ?
                                s_gripState.weaponFormId :
                                0,
                        .semanticManualCycleActionPresent =
                            actionEvidence.present,
                        .semanticManualCycleActionMoved =
                            actionEvidence.moved,
                    });
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

        native_animation_authority::ManualCycleRockGripSnapshot snapshot{};
        snapshot.weaponGenerationKey =
            valid ? s_gripState.weaponGenerationKey : 0;
        if (valid && hasGripFlag(
                s_gripState.flags,
                rock::provider::RockProviderEquippedWeaponGripStateFlagV1::RightHandInWeaponValid)) {
            snapshot.rightHandInWeapon =
                api_transform::toNi(s_gripState.rightHandInWeapon);
            snapshot.rightValid = true;
        }

        rock::provider::RockProviderWeaponPartGripStateV1 leftPartGrip{};
        const bool leftPartGripStateValid =
            valid &&
            s_gripState.weaponFormId != 0 &&
            s_gripState.weaponGenerationKey != 0 &&
            rockApiClient().queryWeaponPartGripState(
                rock::provider::RockProviderHand::Left,
                leftPartGrip) &&
            leftPartGrip.hand == rock::provider::RockProviderHand::Left;
        snapshot.leftPartGripStateValid = leftPartGripStateValid;
        snapshot.leftPartGripActive =
            leftPartGripStateValid && leftPartGrip.active != 0;

        const bool leftSupportCandidate =
            snapshot.leftPartGripActive &&
            isSupportGripKind(leftPartGrip.gripKind) &&
            leftPartGrip.hasHandPartLocal != 0 &&
            leftPartGrip.handPartLocalSpace ==
                rock::provider::RockProviderWeaponPartGripLocalSpaceV1::
                    WeaponRootLocal &&
            leftPartGrip.weaponGenerationKey ==
                s_gripState.weaponGenerationKey;
        if (leftSupportCandidate) {
            snapshot.leftSupportHandInWeapon =
                api_transform::toNi(leftPartGrip.handPartLocal);
            snapshot.leftSupportGripValid = true;
            snapshot.authoredLeftActive =
                leftPartGrip.authoredSupportGrip != 0;
        }
        native_animation_authority::setManualCycleRockGripSnapshot(snapshot);
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
            PAPER_LOG_ERROR(
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

        api::PaperRuntimeStateV1 state{};
        state.statusFlags = static_cast<std::uint32_t>(
            api::PaperRuntimeStatusFlagV1::ProviderReady);
        if (rockConnected) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::RockConnected);
        }
        if (skeletonReady) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::RockSkeletonReady);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::HookInstalled)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::HookInstalled);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::RuntimeEnabled)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::RuntimeEnabled);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::CaptureValid)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::CaptureValid);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::HookInstallFailed)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::HookInstallFailed);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::ThreadMismatch)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::ThreadMismatch);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::CaptureFault)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::CaptureFault);
        }
        if (native.reloadEventActive) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::ReloadEventActive);
        }
        if (native.localManualCycleLeaseActive) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::PaperRuntimeStatusFlagV1::ManualCycleActive);
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
        state.paperProviderGeneration = provider::generation();
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
            // yield at this same graph sample when Paper owns authority.
            publishRockAuthority(runtimeOperational);
            native_animation_authority::captureNativeGraphOutput();
            break;
        }
        case rock::provider::RockProviderAnimationPhaseV1::BeforeRock: {
            provider::beginFrame(context->frameIndex);

            if (operational &&
                !s_hookAttempted.exchange(true, std::memory_order_acq_rel)) {
                if (!native_animation_authority::installEventHooks()) {
                    PAPER_LOG_CRITICAL(
                        Init,
                        "Native animation lifecycle hooks failed validation; Paper authority remains disabled");
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
            provider::dispatchEvent(api::PaperEventKindV1::FrameComplete);
            provider::completeFrame();
            break;
        default:
            break;
        }
    }

    bool connectRock()
    {
        if (!rockApiClient().initialize()) {
            PAPER_LOG_ERROR(
                Api,
                "ROCK V1 connection unavailable; Paper will retry on the next game-session message");
            return false;
        }
        if (!rockApiClient().registerAnimationPhaseCallback(
                &onRockAnimationPhase,
                nullptr)) {
            PAPER_LOG_ERROR(
                Api,
                "Could not register ROCK animation-phase callback");
            return false;
        }
        PAPER_LOG_INFO(
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
        s_manualCycleActionPoseTracker = {};
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
            PAPER_LOG_INFO(
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
    paper::logger::init();
    PAPER_LOG_INFO(Init, "=== PAPER v{} query ===", Version::NAME);

    info->infoVersion = F4SE::PluginInfo::kVersion;
    info->name = "PAPER";
    info->version =
        static_cast<std::uint32_t>(Version::MAJOR * 10000 +
            Version::MINOR * 100 + Version::PATCH);

    if (f4se->IsEditor()) {
        PAPER_LOG_CRITICAL(Init, "Editor runtime is unsupported");
        return false;
    }
    if (!REL::Module::IsVR()) {
        PAPER_LOG_CRITICAL(Init, "Fallout 4 VR runtime is required");
        return false;
    }

    const auto requiredRuntime = F4SE::RUNTIME_LATEST_VR;
    if (f4se->RuntimeVersion() < requiredRuntime) {
        PAPER_LOG_CRITICAL(
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
    paper::provider::initialize();

    s_messaging = F4SE::GetMessagingInterface();
    if (!s_messaging || !s_messaging->RegisterListener(onF4SEMessage)) {
        PAPER_LOG_CRITICAL(
            Init,
            "F4SE messaging registration failed");
        paper::provider::shutdown();
        return false;
    }

    PAPER_LOG_INFO(
        Init,
        "Plugin loaded; provider API V{} ready",
        paper::api::PAPER_API_VERSION);
    return true;
}
