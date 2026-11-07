#include "PCH.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation/NativeAnimationAuthorityPolicy.h"
#include "animation/NativePosePipeline.h"
#include "attachment_bindings/AttachmentBindings.h"
#include "animation_evidence/AnimationEvidence.h"
#include "api/ApiTransform.h"
#include "api/PAPERProvider.h"
#include "api/RequiredUpdates.h"
#include "api/RockApiClient.h"
#include "api/RockVisualAuthorityBridge.h"
#include "compat/TacticalReloadBridge.h"
#include "compat/MergedReloadCompatibility.h"
#include "compat/ReloadSelectionTrace.h"
#include "debug/NativeAnimationDebugVisualization.h"
#include "development/DevelopmentCapturePolicy.h"
#include "PaperConfig.h"
#include "PaperLog.h"
#include "reload_control/BoltHold.h"
#include "reload_control/ChamberEjection.h"
#include "reload_control/ManualBolt.h"
#include "reload_control/ManualReloadOnly.h"
#include "reload_observation/ReloadObservation.h"
#include "reload_stages/ReloadStages.h"
#include "support/Fo4VrRuntime.h"
#include "weapon_motion/WeaponMotion.h"

#include <Windows.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
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
    bool s_climbingAnimationSuppressed{ false };
    bool s_automaticReloadBlockSent{ false };
    bool s_reloadObservationDemandActive{ false };
    bool s_animationTelemetryDemandActive{ false };
    bool s_reloadStageDemandActive{ false };
    bool s_nativePosePipelineDemandActive{ false };
    bool s_weaponMotionDemandActive{ false };
    weapon_motion::RuntimeOptions s_weaponMotionOptions{};
    paper::RockWeaponGripState s_gripState{};
    paper::RockWeaponHandling s_handlingState{};
    bool s_gripStateValid{ false };
    bool s_handlingStateValid{ false };
    native_animation_authority_policy::NativeAnimationCompatibilityState
        s_animationCompatibilityState{};
    native_animation_authority_policy::NativeAnimationCompatibilityReason
        s_lastAnimationCompatibilityReason{
            native_animation_authority_policy::
                NativeAnimationCompatibilityReason::None
        };

    struct CompatibilityLogSnapshot
    {
        std::uint64_t handlingOwnerToken{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t gripWeaponGenerationKey{ 0 };
        std::uint32_t handlingAuthorityFlags{ 0 };
        std::uint32_t handlingRuntimeFlags{ 0 };
        std::uint32_t localAuthorityFlags{ 0 };
        std::uint32_t consumerAuthorityFlags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint32_t gripWeaponFormId{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t providerGeneration{ 0 };
        native_animation_authority_policy::
            NativeAnimationCompatibilityReason reason{
                native_animation_authority_policy::
                    NativeAnimationCompatibilityReason::None
            };
        bool handlingStateValid{ false };
        bool gripStateValid{ false };
        bool weaponIdentityCoherent{ false };

        bool operator==(const CompatibilityLogSnapshot&) const = default;
    };
    CompatibilityLogSnapshot s_lastCompatibilityLogSnapshot{};
    bool s_hasCompatibilityLogSnapshot{ false };

    struct PaperAnimationAuthorityObservation
    {
        std::uint32_t localFlags{ 0 };
        std::uint32_t consumerFlags{ 0 };

        [[nodiscard]] std::uint32_t requestedFlags() const
        {
            return localFlags | consumerFlags;
        }
    };

    struct EnrichmentDemand
    {
        bool reloadObservation{ false };
        bool reloadEvidenceGeometry{ false };
        bool animationTelemetry{ false };
        bool exactAnimationEvidence{ false };
        bool reloadStages{ false };
        bool nativePosePipeline{ false };
        bool weaponMotion{ false };
        weapon_motion::RuntimeOptions weaponMotionOptions{};
    };

    using PerformanceClock = std::chrono::steady_clock;

    struct TimingCounter
    {
        std::uint64_t totalMicroseconds{ 0 };
        std::uint64_t maximumMicroseconds{ 0 };
        std::uint64_t calls{ 0 };

        void add(const std::uint64_t microseconds)
        {
            totalMicroseconds += microseconds;
            if (microseconds > maximumMicroseconds) {
                maximumMicroseconds = microseconds;
            }
            ++calls;
        }
    };

    /*
     * This timing-only trace remains dormant on normal frames and reports at
     * most once per second during enrichment or a slow frame. Comparing the
     * engine frame interval with measured PAPER callbacks distinguishes work
     * inside this plugin from engine work that occurs while PAPER retains the
     * off-screen graph manager; the stage split then identifies the local
     * owner without changing capture fidelity or scheduling.
     */
    struct ReloadPerformanceWindow
    {
        PerformanceClock::time_point started{};
        std::uint64_t currentFrameIndex{ 0 };
        std::uint64_t currentFrameMicroseconds{ 0 };
        bool currentFrameActive{ false };
        std::uint64_t frameCount{ 0 };
        double gameDeltaMillisecondsTotal{ 0.0 };
        double gameDeltaMillisecondsMaximum{ 0.0 };
        TimingCounter paperFrame{};
        TimingCounter nativeGraphPhase{};
        TimingCounter beforeRockPhase{};
        TimingCounter afterRockPhase{};
        TimingCounter completePhase{};
        TimingCounter observationCapture{};
        TimingCounter observationAdvance{};
        TimingCounter observationComplete{};
        TimingCounter animationAdvance{};
        TimingCounter animationComplete{};
        TimingCounter consumerDispatch{};
        TimingCounter catalogSetup{};
        TimingCounter passiveDrain{};
        TimingCounter nameCollection{};
        TimingCounter passiveUpdate{};
        TimingCounter exactUpdate{};
        TimingCounter passiveStats{};
        exact_clip_preharvest::RuntimeDiagnostics exact{};
        std::uint64_t exactSamplesCompleted{ 0 };
        std::uint64_t exactSamplingMicroseconds{ 0 };
        std::uint64_t exactBudgetYieldCount{ 0 };
        std::uint64_t exactSampleLimitYieldCount{ 0 };
        std::uint32_t callbackThreadId{ 0 };
        bool mixedCallbackThreads{ false };
        bool observationDemandSeen{ false };
        bool animationDemandSeen{ false };
        bool exactAnimationDemandSeen{ false };
    };

    ReloadPerformanceWindow s_reloadPerformance{};

    [[nodiscard]] std::uint64_t elapsedPerformanceMicroseconds(
        const PerformanceClock::time_point started)
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                PerformanceClock::now() - started)
                .count());
    }

    [[nodiscard]] std::uint64_t averageMicroseconds(
        const TimingCounter& counter)
    {
        return counter.calls > 0 ?
            counter.totalMicroseconds / counter.calls :
            0;
    }

    void beginPerformancePhase(const std::uint64_t frameIndex)
    {
        const auto now = PerformanceClock::now();
        if (s_reloadPerformance.started.time_since_epoch().count() == 0) {
            s_reloadPerformance.started = now;
        }
        if (!s_reloadPerformance.currentFrameActive ||
            s_reloadPerformance.currentFrameIndex != frameIndex) {
            s_reloadPerformance.currentFrameIndex = frameIndex;
            s_reloadPerformance.currentFrameMicroseconds = 0;
            s_reloadPerformance.currentFrameActive = true;
        }
        const auto callbackThreadId = GetCurrentThreadId();
        if (s_reloadPerformance.callbackThreadId == 0) {
            s_reloadPerformance.callbackThreadId = callbackThreadId;
        } else if (s_reloadPerformance.callbackThreadId != callbackThreadId) {
            s_reloadPerformance.mixedCallbackThreads = true;
        }
    }

    void recordPhasePerformance(
        const rock::api::animation::AnimationPhase phase,
        const std::uint64_t microseconds)
    {
        s_reloadPerformance.currentFrameMicroseconds += microseconds;
        switch (phase) {
        case rock::api::animation::AnimationPhase::NativeGraphOutput:
            s_reloadPerformance.nativeGraphPhase.add(microseconds);
            break;
        case rock::api::animation::AnimationPhase::BeforeRock:
            s_reloadPerformance.beforeRockPhase.add(microseconds);
            break;
        case rock::api::animation::AnimationPhase::AfterRock:
            s_reloadPerformance.afterRockPhase.add(microseconds);
            break;
        case rock::api::animation::AnimationPhase::Complete:
            s_reloadPerformance.completePhase.add(microseconds);
            break;
        default:
            break;
        }
    }

    void recordAnimationDiagnostics(
        const animation_evidence::FrameDiagnostics& diagnostics)
    {
        s_reloadPerformance.catalogSetup.add(
            diagnostics.catalogSetupMicroseconds);
        s_reloadPerformance.passiveDrain.add(
            diagnostics.passiveDrainMicroseconds);
        s_reloadPerformance.nameCollection.add(
            diagnostics.nameCollectionMicroseconds);
        s_reloadPerformance.passiveUpdate.add(
            diagnostics.passiveUpdateMicroseconds);
        s_reloadPerformance.exactUpdate.add(
            diagnostics.exactUpdateMicroseconds);
        s_reloadPerformance.passiveStats.add(
            diagnostics.passiveStatsMicroseconds);
        s_reloadPerformance.exact = diagnostics.exact;
        s_reloadPerformance.exactSamplesCompleted +=
            diagnostics.exact.samplesCompletedLastStep;
        s_reloadPerformance.exactSamplingMicroseconds +=
            diagnostics.exact.samplingMicrosecondsLastStep;
        if (diagnostics.exact.samplingBudgetYielded) {
            ++s_reloadPerformance.exactBudgetYieldCount;
        }
        if (diagnostics.exact.sampleLimitYielded) {
            ++s_reloadPerformance.exactSampleLimitYieldCount;
        }
    }

    void completePerformanceFrame(
        const rock::api::animation::AnimationPhaseContext& context,
        const EnrichmentDemand demand)
    {
        auto& performance = s_reloadPerformance;
        performance.paperFrame.add(performance.currentFrameMicroseconds);
        performance.currentFrameMicroseconds = 0;
        performance.currentFrameActive = false;
        ++performance.frameCount;
        const double deltaMilliseconds =
            std::isfinite(context.deltaSeconds) && context.deltaSeconds > 0.0f ?
                static_cast<double>(context.deltaSeconds) * 1000.0 :
                0.0;
        performance.gameDeltaMillisecondsTotal += deltaMilliseconds;
        if (deltaMilliseconds > performance.gameDeltaMillisecondsMaximum) {
            performance.gameDeltaMillisecondsMaximum = deltaMilliseconds;
        }
        performance.observationDemandSeen =
            performance.observationDemandSeen || demand.reloadObservation;
        performance.animationDemandSeen =
            performance.animationDemandSeen || demand.animationTelemetry;
        performance.exactAnimationDemandSeen =
            performance.exactAnimationDemandSeen ||
            demand.exactAnimationEvidence;

        const auto now = PerformanceClock::now();
        const auto wallMilliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - performance.started)
                .count();
        if (wallMilliseconds < 1000) {
            return;
        }

        const double averageGameMilliseconds = performance.frameCount > 0 ?
            performance.gameDeltaMillisecondsTotal /
                static_cast<double>(performance.frameCount) :
            0.0;
        const double observedHz = wallMilliseconds > 0 ?
            static_cast<double>(performance.frameCount) * 1000.0 /
                static_cast<double>(wallMilliseconds) :
            0.0;
        // A game frame over 20 ms occurs nearly every second in VR and is not
        // Paper's cost; report only Paper demand or Paper's own spikes.
        const bool shouldReport =
            performance.observationDemandSeen ||
            performance.animationDemandSeen ||
            performance.exactAnimationDemandSeen ||
            performance.paperFrame.maximumMicroseconds >= 1000;
        if (shouldReport) {
            PAPER_LOG_WARN(Performance,
                "RELOAD-PERF frames={} wallMs={} hz={:.1f} thread={} mixedThreads={} gameMs(avg/max)={:.2f}/{:.2f} paperMs(avg/max)={:.3f}/{:.3f} demand(obs/anim/exact)={}/{}/{} phaseUs(avg/max)=[native {}/{} before {}/{} after {}/{} complete {}/{}]",
                performance.frameCount,
                wallMilliseconds,
                observedHz,
                performance.callbackThreadId,
                performance.mixedCallbackThreads,
                averageGameMilliseconds,
                performance.gameDeltaMillisecondsMaximum,
                static_cast<double>(averageMicroseconds(
                    performance.paperFrame)) / 1000.0,
                static_cast<double>(
                    performance.paperFrame.maximumMicroseconds) / 1000.0,
                performance.observationDemandSeen,
                performance.animationDemandSeen,
                performance.exactAnimationDemandSeen,
                averageMicroseconds(performance.nativeGraphPhase),
                performance.nativeGraphPhase.maximumMicroseconds,
                averageMicroseconds(performance.beforeRockPhase),
                performance.beforeRockPhase.maximumMicroseconds,
                averageMicroseconds(performance.afterRockPhase),
                performance.afterRockPhase.maximumMicroseconds,
                averageMicroseconds(performance.completePhase),
                performance.completePhase.maximumMicroseconds);
            PAPER_LOG_WARN(Performance,
                "RELOAD-PERF detail observationUs(capture/advance/complete avg/max)=[{}/{} {}/{} {}/{}] animationUs(advance/complete/dispatch avg/max)=[{}/{} {}/{} {}/{}] animationStageUs(setup/names/drain/passive/exact/stats avg/max)=[{}/{} {}/{} {}/{} {}/{} {}/{} {}/{}] exact(state={} background={} resource={} path={}/{} sample={}/{} sampled={} sampleUs={} budgetYields={} limitYields={})",
                averageMicroseconds(performance.observationCapture),
                performance.observationCapture.maximumMicroseconds,
                averageMicroseconds(performance.observationAdvance),
                performance.observationAdvance.maximumMicroseconds,
                averageMicroseconds(performance.observationComplete),
                performance.observationComplete.maximumMicroseconds,
                averageMicroseconds(performance.animationAdvance),
                performance.animationAdvance.maximumMicroseconds,
                averageMicroseconds(performance.animationComplete),
                performance.animationComplete.maximumMicroseconds,
                averageMicroseconds(performance.consumerDispatch),
                performance.consumerDispatch.maximumMicroseconds,
                averageMicroseconds(performance.catalogSetup),
                performance.catalogSetup.maximumMicroseconds,
                averageMicroseconds(performance.nameCollection),
                performance.nameCollection.maximumMicroseconds,
                averageMicroseconds(performance.passiveDrain),
                performance.passiveDrain.maximumMicroseconds,
                averageMicroseconds(performance.passiveUpdate),
                performance.passiveUpdate.maximumMicroseconds,
                averageMicroseconds(performance.exactUpdate),
                performance.exactUpdate.maximumMicroseconds,
                averageMicroseconds(performance.passiveStats),
                performance.passiveStats.maximumMicroseconds,
                static_cast<unsigned>(performance.exact.state),
                performance.exact.backgroundGraphActive,
                performance.exact.clipResourceActive,
                performance.exact.animationPathIndex,
                performance.exact.animationPathCount,
                performance.exact.nextSample,
                performance.exact.sampleCount,
                performance.exactSamplesCompleted,
                performance.exactSamplingMicroseconds,
                performance.exactBudgetYieldCount,
                performance.exactSampleLimitYieldCount);
        }
        performance = {};
        performance.started = now;
    }

    [[nodiscard]] EnrichmentDemand refreshEnrichmentDemand()
    {
        const auto scope = [](const api::DevelopmentCaptureScope value) {
            return static_cast<std::uint32_t>(value);
        };
        std::uint32_t legacyScopes = 0;
        const bool catalogConsumer = provider::hasConsumerCapability(
            api::ConsumerCapability::WeaponMotionCatalog);
        const bool diagnosticsConsumer = provider::hasConsumerCapability(
            api::ConsumerCapability::WeaponMotionDiagnostics);
        const bool manipulationConsumer = provider::hasConsumerCapability(
            api::ConsumerCapability::WeaponManipulationTelemetry);
        const bool motionConsumer =
            catalogConsumer || diagnosticsConsumer || manipulationConsumer;
        if (motionConsumer) {
            legacyScopes |=
                scope(api::DevelopmentCaptureScope::
                    PassiveObservation) |
                scope(api::DevelopmentCaptureScope::
                    WeaponMotionCompilation) |
                scope(api::DevelopmentCaptureScope::
                    CompiledCacheRead) |
                scope(api::DevelopmentCaptureScope::
                    CompiledCacheWrite);
        }
        if (diagnosticsConsumer) {
            legacyScopes |= scope(
                api::DevelopmentCaptureScope::LiveMotionLearning);
        }
        const bool reloadStages = provider::hasConsumerCapability(
            api::ConsumerCapability::
                ReloadStageIdentification);
        const bool exactConsumer = provider::hasConsumerCapability(
            api::ConsumerCapability::ReloadAnimationEvidence);
        const bool telemetryConsumer = provider::hasConsumerCapability(
            api::ConsumerCapability::ReloadAnimationTelemetry);
        const bool observationConsumer = provider::hasConsumerCapability(
            api::ConsumerCapability::ReloadObservations);
        const bool geometryConsumer = provider::hasConsumerCapability(
            api::ConsumerCapability::ReloadEvidenceGeometry);
        if (reloadStages || exactConsumer || telemetryConsumer ||
            observationConsumer || geometryConsumer) {
            legacyScopes |= scope(
                api::DevelopmentCaptureScope::PassiveObservation);
        }
        if (exactConsumer) {
            legacyScopes |= scope(
                api::DevelopmentCaptureScope::ExactAnimationHarvest);
        }

        auto captureScopes =
            provider::refreshDevelopmentCaptureDemand(legacyScopes);
        const bool weaponMotion = (captureScopes & scope(
            api::DevelopmentCaptureScope::
                WeaponMotionCompilation)) != 0;
        weapon_motion::RuntimeOptions motionOptions{
            .cacheRead = (captureScopes & scope(
                api::DevelopmentCaptureScope::CompiledCacheRead)) != 0,
            .cacheWrite = (captureScopes & scope(
                api::DevelopmentCaptureScope::CompiledCacheWrite)) != 0,
            .liveMotionLearning = (captureScopes & scope(
                api::DevelopmentCaptureScope::LiveMotionLearning)) != 0,
            .manipulationTelemetry = weaponMotion && manipulationConsumer,
        };
        const bool exactAnimationEvidence =
            (captureScopes & scope(
                api::DevelopmentCaptureScope::
                    ExactAnimationHarvest)) != 0 ||
            (weaponMotion &&
                (provider::allowedDevelopmentCaptureScopes() & scope(
                    api::DevelopmentCaptureScope::
                        ExactAnimationHarvest)) != 0 &&
                weapon_motion::requiresExactAnimationEvidence(
                    motionOptions.cacheRead));
        if (exactAnimationEvidence) {
            captureScopes |= scope(
                api::DevelopmentCaptureScope::ExactAnimationHarvest) |
                scope(api::DevelopmentCaptureScope::PassiveObservation);
        }
        const bool passiveObservation = (captureScopes & scope(
            api::DevelopmentCaptureScope::PassiveObservation)) != 0;
        const bool animationTelemetry = passiveObservation;
        const bool reloadEvidenceGeometry =
            passiveObservation && geometryConsumer;
        const bool reloadObservation = passiveObservation;
        const bool reloadStagesActive = passiveObservation && reloadStages;
        const bool nativePosePipeline = provider::hasConsumerCapability(
            api::ConsumerCapability::NativePosePipeline);

        if (s_weaponMotionDemandActive &&
            (!weaponMotion || s_weaponMotionOptions != motionOptions)) {
            weapon_motion::reset(
                weapon_motion::ResetReason::DemandEnded);
        }
        if (s_animationTelemetryDemandActive && !animationTelemetry) {
            animation_evidence::reset();
        }
        if (s_reloadObservationDemandActive && !reloadObservation) {
            reload_observation::reset();
        }
        if (s_reloadStageDemandActive && !reloadStagesActive) {
            reload_stages::reset();
        }
        if (s_nativePosePipelineDemandActive && !nativePosePipeline) {
            provider::clearNativePosePipeline();
        }
        s_reloadObservationDemandActive = reloadObservation;
        s_animationTelemetryDemandActive = animationTelemetry;
        s_reloadStageDemandActive = reloadStagesActive;
        s_nativePosePipelineDemandActive = nativePosePipeline;
        s_weaponMotionDemandActive = weaponMotion;
        s_weaponMotionOptions = motionOptions;
        provider::publishDevelopmentCaptureScopes(captureScopes);
        return EnrichmentDemand{
            .reloadObservation = reloadObservation,
            .reloadEvidenceGeometry = reloadEvidenceGeometry,
            .animationTelemetry = animationTelemetry,
            .exactAnimationEvidence = exactAnimationEvidence,
            .reloadStages = reloadStagesActive,
            .nativePosePipeline = nativePosePipeline,
            .weaponMotion = weaponMotion,
            .weaponMotionOptions = motionOptions,
        };
    }

    [[nodiscard]] bool hasContextFlag(
        const std::uint32_t flags,
        const rock::api::animation::AnimationPhaseContextFlag flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool hasGripFlag(
        const std::uint32_t flags,
        const rock::api::weapon::GripFlag flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool hasWeaponStateFlag(
        const std::uint32_t flags,
        const rock::api::weapon::WeaponStateFlag flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool hasHandlingStateFlag(
        const std::uint32_t flags,
        const rock::api::weapon::HandlingStateFlag flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool hasHandInteractionFlag(
        const std::uint32_t flags,
        const rock::api::grab::HandInteractionFlag flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool isFixedSurfaceClimb(
        const rock::api::grab::HandInteractionState& state)
    {
        return state.phase ==
                   rock::api::grab::HandInteractionPhase::
                       Holding &&
               hasHandInteractionFlag(
                   state.flags,
                   rock::api::grab::HandInteractionFlag::
                       TouchGrab) &&
               hasHandInteractionFlag(
                   state.flags,
                   rock::api::grab::HandInteractionFlag::
                       FixedSurfaceLatch);
    }

    [[nodiscard]] bool refreshClimbingAnimationSuppression()
    {
        rock::api::grab::HandInteractionState rightState{};
        rock::api::grab::HandInteractionState leftState{};
        const bool rightStateValid =
            rockApiClient().queryHandInteractionState(
                rock::api::Hand::Right,
                rightState) &&
            rightState.hand == rock::api::Hand::Right &&
            hasHandInteractionFlag(
                rightState.flags,
                rock::api::grab::HandInteractionFlag::Valid);
        const bool leftStateValid =
            rockApiClient().queryHandInteractionState(
                rock::api::Hand::Left,
                leftState) &&
            leftState.hand == rock::api::Hand::Left &&
            hasHandInteractionFlag(
                leftState.flags,
                rock::api::grab::HandInteractionFlag::Valid);
        const bool rightFixedSurfaceLatch =
            rightStateValid && isFixedSurfaceClimb(rightState);
        const bool leftFixedSurfaceLatch =
            leftStateValid && isFixedSurfaceClimb(leftState);
        const bool suppress = native_animation_authority_policy::
            shouldSuppressAnimationForClimbing({
                .rightStateValid = rightStateValid,
                .leftStateValid = leftStateValid,
                .rightFixedSurfaceLatch = rightFixedSurfaceLatch,
                .leftFixedSurfaceLatch = leftFixedSurfaceLatch,
            });
        if (suppress != s_climbingAnimationSuppressed) {
            PAPER_LOG_INFO(
                Animation,
                "Climbing animation suppression {} rightState={} rightLatch={} leftState={} leftLatch={}",
                suppress ? "active" : "released",
                rightStateValid ? "valid" : "unavailable",
                rightFixedSurfaceLatch ? "held" : "clear",
                leftStateValid ? "valid" : "unavailable",
                leftFixedSurfaceLatch ? "held" : "clear");
            s_climbingAnimationSuppressed = suppress;
        }
        return suppress;
    }

    [[nodiscard]] bool isSupportGripKind(
        const rock::api::weaponparts::GripKind kind)
    {
        return kind ==
                   rock::api::weaponparts::GripKind::
                       SupportFullAuthority ||
               kind ==
                   rock::api::weaponparts::GripKind::
                       SupportVisualOnly;
    }

    void publishConfigState()
    {
        api::ConfigState state{};
        state.enabled = g_config.enabled ? 1u : 0u;
        state.nativeReloadAuthorityEnabled = 1u;
        state.partialReloadAuthorityEnabled = 1u;
        state.logLevel = g_config.logLevel;
        state.revision = ++s_configRevision;
        state.developmentCaptureMode = g_config.developmentCaptureMode;
        state.weaponMotionCacheAccess = g_config.weaponMotionCacheAccess;
        if (g_config.developmentCaptureAutoStart) {
            state.developmentCaptureFlags |= static_cast<std::uint32_t>(
                api::DevelopmentCaptureConfigFlag::AutoStart);
        }
        if (g_config.developmentCaptureAllowApiActivation) {
            state.developmentCaptureFlags |= static_cast<std::uint32_t>(
                api::DevelopmentCaptureConfigFlag::
                    AllowApiActivation);
        }
        // Hot reload is mandatory, independent of development capture permissions.
        state.developmentCaptureFlags |= static_cast<std::uint32_t>(
            api::DevelopmentCaptureConfigFlag::HotReloadEnabled);
        state.developmentCaptureAllowedScopes =
            development_capture_policy::allowedScopes(
                g_config.developmentCaptureMode,
                g_config.weaponMotionCacheAccess);
        provider::configureDevelopmentCapture(
            state.developmentCaptureMode,
            state.weaponMotionCacheAccess,
            state.developmentCaptureFlags,
            state.revision);
        provider::publishConfig(state);
        provider::dispatchEvent(api::EventKind::ConfigReloaded);
    }

    void configureManualReloadOnlyForSession()
    {
        const bool requested = g_config.enabled && g_config.manualReloadOnly;
        const bool hookReady = manual_reload_only::installHook();
        (void)manual_reload_only::installPhysicalCycleHook();
        manual_reload_only::setRuntimeEnabled(requested && hookReady);
        if (requested && !hookReady) {
            PAPER_LOG_ERROR(
                Reload,
                "bManualReloadOnly was requested, but native validation failed; vanilla automatic reload remains active");
        }
    }

    [[nodiscard]] native_animation_authority_policy::
        NativeAnimationCompatibilityObservation
    refreshWeaponState(
        const rock::api::animation::AnimationPhaseContext& context)
    {
        s_handlingState = {};
        s_handlingStateValid =
            rockApiClient().queryEquippedWeaponHandling(
                s_handlingState);
        const auto& weaponState = s_handlingState.state;
        s_gripState = {};
        const bool queried =
            rockApiClient().queryEquippedWeaponGripState(s_gripState);
        s_gripStateValid = queried && hasGripFlag(
            s_gripState.flags,
            rock::api::weapon::GripFlag::Valid);
        // ROCK publishes the weapon's hands only in its weapon state.
        const bool firingHandIsLeft =
            s_handlingStateValid &&
            weaponState.firingHand == rock::api::Hand::Left;
        const bool partCarryActive =
            s_handlingStateValid && hasHandlingStateFlag(
                weaponState.handlingFlags,
                rock::api::weapon::HandlingStateFlag::PartCarry);
        const bool weaponPresent =
            s_handlingStateValid && hasWeaponStateFlag(
                weaponState.flags,
                rock::api::weapon::WeaponStateFlag::WeaponPresent);
        const bool weaponIdentityCoherent =
            s_handlingStateValid &&
            (!s_gripStateValid ||
                (weaponState.weaponFormId ==
                        s_gripState.weaponFormId &&
                    weaponState.weaponGenerationKey ==
                        s_gripState.weaponGenerationKey));

        // A retained world weapon and the equipped weapon are distinct items.
        // Neither PAPER's reload nor its manual-cycle arm pose can own both.
        bool handStatesValid = true;
        std::uint32_t heldWeapons[2]{};
        for (const auto hand : {rock::api::Hand::Right, rock::api::Hand::Left}) {
            rock::api::grab::HandInteractionState held{};
            const bool valid = rockApiClient().queryHandInteractionState(hand, held) &&
                held.hand == hand && hasHandInteractionFlag(held.flags,
                    rock::api::grab::HandInteractionFlag::Valid) &&
                held.worldGeneration == context.worldGeneration &&
                held.skeletonGeneration == context.skeletonGeneration &&
                held.providerGeneration == context.providerGeneration;
            handStatesValid = handStatesValid && valid;
            if (valid && hasHandInteractionFlag(held.flags,
                    rock::api::grab::HandInteractionFlag::LooseWeapon))
                heldWeapons[hand == rock::api::Hand::Left ? 1 : 0] = held.targetFormId;
        }
        // Native equipment has one record per slot. Two copies of the same
        // base form still own different magazines; do not deduplicate by form.
        // E803D0/E806B0 establish the indexed array contract in FO4VR.
        bool nativeRight = false, nativeLeft = false;
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* middle = player && player->currentProcess ? player->currentProcess->middleHigh : nullptr;
        if (middle && middle->equippedItems.size() <= 16) {
            for (const auto& item : middle->equippedItems) {
                if (!item.item.object || item.item.object->formType != RE::ENUM_FORM_ID::kWEAP || !item.data) continue;
                if (item.equipIndex.index == 0) nativeRight = true;
                if (item.equipIndex.index == 1) nativeLeft = true;
            }
        }
        const bool akimboActive = (nativeRight && nativeLeft) ||
            (weaponPresent && (heldWeapons[0] || heldWeapons[1])) ||
            (heldWeapons[0] && heldWeapons[1] && heldWeapons[0] != heldWeapons[1]);

        native_animation_authority::ManualCycleRockGripSnapshot snapshot{};
        snapshot.weaponNode = s_gripStateValid ? s_gripState.weaponNode : 0;
        snapshot.weaponFormId = s_gripStateValid ? s_gripState.weaponFormId : 0;
        snapshot.weaponGenerationKey =
            s_gripStateValid ? s_gripState.weaponGenerationKey : 0;
        if (s_gripStateValid &&
            weaponIdentityCoherent && !firingHandIsLeft &&
            !partCarryActive && !akimboActive && handStatesValid) {
            // Animation belongs to the weapon's canonical firing seat. A paired
            // grip snapshot may instead describe an arbitrary physical grab,
            // especially while the weapon is resting on its bipod.
            rock::api::weapon::AuthoredGripPose authored{};
            constexpr auto requiredFlags =
                static_cast<std::uint32_t>(
                    rock::api::weapon::AuthoredGripPoseFlag::Valid) |
                static_cast<std::uint32_t>(
                    rock::api::weapon::AuthoredGripPoseFlag::RightHandValid);
            if (rockApiClient().querySelectedAuthoredGripPose(authored) &&
                (authored.flags & requiredFlags) == requiredFlags &&
                authored.weaponFormId == s_gripState.weaponFormId &&
                authored.weaponGenerationKey != 0 &&
                authored.weaponGenerationKey == snapshot.weaponGenerationKey &&
                authored.worldGeneration == context.worldGeneration &&
                authored.skeletonGeneration == context.skeletonGeneration &&
                authored.providerGeneration == context.providerGeneration) {
                snapshot.rightHandInWeapon =
                    api_transform::toNi(authored.rightHandInWeapon);
                snapshot.rightValid = true;
            }
        }

        rock::api::weaponparts::GripState leftPartGrip{};
        const bool leftPartGripStateValid =
            s_gripStateValid &&
            s_gripState.weaponFormId != 0 &&
            s_gripState.weaponGenerationKey != 0 &&
            rockApiClient().queryWeaponPartGripState(
                rock::api::Hand::Left,
                leftPartGrip) &&
            leftPartGrip.hand == rock::api::Hand::Left;
        snapshot.leftPartGripStateValid = leftPartGripStateValid;
        snapshot.leftPartGripActive =
            leftPartGripStateValid && leftPartGrip.active != 0;

        const bool leftSupportCandidate =
            snapshot.leftPartGripActive &&
            isSupportGripKind(leftPartGrip.gripKind) &&
            leftPartGrip.hasHandPartLocal != 0 &&
            leftPartGrip.handPartLocalSpace ==
                rock::api::weaponparts::GripLocalSpace::
                    WeaponRootLocal &&
            leftPartGrip.weaponGenerationKey ==
                s_gripState.weaponGenerationKey;
        if (leftSupportCandidate) {
            snapshot.leftSupportHandInWeapon =
                api_transform::toNi(leftPartGrip.handPartLocal);
            snapshot.leftSupportGripValid = true;
            snapshot.authoredLeftSupportGripActive =
                leftPartGrip.authoredSupportGrip != 0;
        }
        native_animation_authority::setManualCycleRockGripSnapshot(snapshot);
        return native_animation_authority_policy::
            NativeAnimationCompatibilityObservation{
                .weaponGenerationKey =
                    s_handlingStateValid ?
                        weaponState.weaponGenerationKey :
                        0,
                .weaponNode = s_gripStateValid ? s_gripState.weaponNode : 0,
                .weaponFormId =
                    s_handlingStateValid ?
                        weaponState.weaponFormId :
                        0,
                .handlingStateValid = s_handlingStateValid && handStatesValid,
                .gripStateValid = s_gripStateValid,
                .firingHandIsLeft = firingHandIsLeft,
                .partCarryActive = partCarryActive,
                .weaponPresent = weaponPresent,
                .weaponIdentityCoherent =
                    weaponIdentityCoherent,
                .akimboActive = akimboActive,
                .userWeaponOffset =
                    s_handlingStateValid && hasWeaponStateFlag(
                        weaponState.flags,
                        rock::api::weapon::WeaponStateFlag::UserWeaponOffset),
        };
    }

    void configureRuntime(const bool operational)
    {
        native_animation_authority::setRuntimeEnabled(operational);
        native_animation_authority::setLocalManualCycleTestEnabled(
            operational);
        native_animation_authority::setLocalReloadTestEnabled(
            operational);
        s_runtimeOperational = operational;
    }

    // Separate from hand authority: a left firing hand, akimbo or a climbing latch
    // leaves a reload native, and a native reload still needs its keyword and clip.
    void configureReloadCompatibility(const bool enabled)
    {
        tactical_reload_bridge::setRuntimeEnabled(enabled);
        merged_reload_compatibility::setRuntimeEnabled(enabled);
        reload_selection_trace::setRuntimeEnabled(enabled);
    }

    void observeAnimationCompatibility(
        const rock::api::animation::AnimationPhaseContext& context,
        const char* observationPoint,
        const native_animation_authority_policy::
            NativeAnimationCompatibilityObservation& observation,
        const native_animation_authority_policy::
            NativeAnimationCompatibilityStep& step,
        const PaperAnimationAuthorityObservation& authority)
    {
        const CompatibilityLogSnapshot snapshot{
            .handlingOwnerToken = s_handlingState.authority.owner,
            .weaponGenerationKey = observation.weaponGenerationKey,
            .gripWeaponGenerationKey =
                s_gripStateValid ? s_gripState.weaponGenerationKey : 0,
            .handlingAuthorityFlags = s_handlingState.authority.features,
            .handlingRuntimeFlags = s_handlingState.state.handlingFlags,
            .localAuthorityFlags = authority.localFlags,
            .consumerAuthorityFlags = authority.consumerFlags,
            .weaponFormId = observation.weaponFormId,
            .gripWeaponFormId =
                s_gripStateValid ? s_gripState.weaponFormId : 0,
            .worldGeneration = context.worldGeneration,
            .skeletonGeneration = context.skeletonGeneration,
            .providerGeneration = context.providerGeneration,
            .reason = step.reason,
            .handlingStateValid = observation.handlingStateValid,
            .gripStateValid = observation.gripStateValid,
            .weaponIdentityCoherent =
                observation.weaponIdentityCoherent,
        };
        if (s_hasCompatibilityLogSnapshot &&
            snapshot == s_lastCompatibilityLogSnapshot) {
            return;
        }
        s_lastCompatibilityLogSnapshot = snapshot;
        s_hasCompatibilityLogSnapshot = true;

        const bool externalHandlingAuthorityActive =
            observation.handlingStateValid &&
            s_handlingState.authority.active != 0;
        PAPER_LOG_INFO(
            Animation,
            "Weapon-animation compatibility point={} result={} animationAuthorityActive={} handlingValid={} gripValid={} identityCoherent={} externalHandlingAuthority={} owner={:016X} authorityFeatures=0x{:08X} handlingFlags=0x{:08X} localFlags=0x{:08X} consumerFlags=0x{:08X} weapon={:08X}/{:016X} gripWeapon={:08X}/{:016X} world={} skeleton={} provider={}",
            observationPoint,
            native_animation_authority_policy::
                nativeAnimationCompatibilityReasonName(step.reason),
            authority.requestedFlags() != 0,
            observation.handlingStateValid,
            observation.gripStateValid,
            observation.weaponIdentityCoherent,
            externalHandlingAuthorityActive,
            s_handlingState.authority.owner,
            s_handlingState.authority.features,
            s_handlingState.state.handlingFlags,
            authority.localFlags,
            authority.consumerFlags,
            observation.weaponFormId,
            observation.weaponGenerationKey,
            s_gripStateValid ? s_gripState.weaponFormId : 0,
            s_gripStateValid ? s_gripState.weaponGenerationKey : 0,
            context.worldGeneration,
            context.skeletonGeneration,
            context.providerGeneration);
    }

    [[nodiscard]] PaperAnimationAuthorityObservation
    currentPaperAnimationAuthority()
    {
        return {
            .localFlags =
                native_animation_authority::currentLocalAuthorityFlags(),
            .consumerFlags = provider::currentConsumerAuthorityFlags(),
        };
    }

    [[nodiscard]] native_animation_authority_policy::
        NativeAnimationCompatibilityStep
    evaluateAnimationCompatibility(
        const rock::api::animation::AnimationPhaseContext& context,
        const char* observationPoint,
        const native_animation_authority_policy::
            NativeAnimationCompatibilityObservation& observation,
        const PaperAnimationAuthorityObservation& authority)
    {
        const auto step = native_animation_authority_policy::
            advanceNativeAnimationCompatibility(
                s_animationCompatibilityState,
                observation,
                authority.requestedFlags() != 0);
        s_animationCompatibilityState = step.state;
        s_lastAnimationCompatibilityReason = step.reason;
        observeAnimationCompatibility(
            context,
            observationPoint,
            observation,
            step,
            authority);
        return step;
    }

    void applyManualCycleEligibility(
        const native_animation_authority_policy::
            NativeAnimationCompatibilityObservation& observation)
    {
        native_animation_authority::setManualCycleHandAnimationEligible(
            native_animation_authority_policy::
                canApplyManualCycleHandAnimation(observation));
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
        const rock::api::animation::AnimationPhaseContext& context,
        const bool rockConnected,
        const bool skeletonReady)
    {
        const auto native = native_animation_authority::queryRuntimeStatus();
        const auto localFlags =
            native_animation_authority::currentLocalAuthorityFlags();
        const auto consumerFlags = provider::currentConsumerAuthorityFlags();

        rock::api::animation::NativeAnimationRuntimePublication rockState{};
        rockState.statusFlags = native.statusFlags;
        rockState.capturedTransformCount = native.capturedTransformCount;
        rockState.captureSequence = native.captureSequence;
        rockState.worldGeneration = context.worldGeneration;
        rockState.skeletonGeneration = context.skeletonGeneration;
        rockState.providerGeneration = context.providerGeneration;
        if (rockConnected) {
            (void)rockApiClient().publishNativeAnimationRuntime(rockState);
        }

        api::RuntimeState state{};
        state.statusFlags = static_cast<std::uint32_t>(
            api::RuntimeStatusFlag::ProviderReady);
        if (rockConnected) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::RockConnected);
        }
        if (skeletonReady) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::RockSkeletonReady);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::HookInstalled)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::HookInstalled);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::RuntimeEnabled)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::RuntimeEnabled);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::CaptureValid)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::CaptureValid);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::HookInstallFailed)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::HookInstallFailed);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::ThreadMismatch)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::ThreadMismatch);
        }
        if ((native.statusFlags & static_cast<std::uint32_t>(
                native_animation_authority::RuntimeStatusFlag::CaptureFault)) != 0) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::CaptureFault);
        }
        if (native.reloadEventActive) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::ReloadEventActive);
        }
        if (native.localManualCycleLeaseActive) {
            state.statusFlags |= static_cast<std::uint32_t>(
                api::RuntimeStatusFlag::ManualCycleActive);
        }
        state.activeAuthorityFlags = native.effectiveFlags;
        state.localAuthorityFlags = localFlags;
        state.consumerAuthorityFlags = consumerFlags;
        state.capturedTransformCount = native.capturedTransformCount;
        state.weaponFormId = s_handlingStateValid ?
            s_handlingState.state.weaponFormId :
            s_gripState.weaponFormId;
        state.captureSequence = native.captureSequence;
        state.reloadStartSequence = native.reloadStartSequence;
        state.reloadEndSequence = native.reloadEndSequence;
        state.frameIndex = context.frameIndex;
        state.weaponGenerationKey = s_handlingStateValid ?
            s_handlingState.state.weaponGenerationKey :
            s_gripState.weaponGenerationKey;
        state.worldGeneration = context.worldGeneration;
        state.skeletonGeneration = context.skeletonGeneration;
        state.rockProviderGeneration = context.providerGeneration;
        state.paperProviderGeneration = provider::generation();
        provider::publishRuntime(state);
    }

    void ROCK_CALL onRockAnimationPhase(
        const rock::api::animation::AnimationPhaseContext* context,
        void*)
    {
        if (!context || !s_gameLoaded.load(std::memory_order_acquire)) {
            return;
        }
        rockApiClient().setFrameContext(*context);
        beginPerformancePhase(context->frameIndex);
        const auto phaseStarted = PerformanceClock::now();

        if (context->phase ==
                rock::api::animation::AnimationPhase::BeforeRock &&
            g_config.processPendingReload()) {
            weapon_motion::setCacheAccess(g_config.weaponMotionCacheAccess);
            publishConfigState();
            configureManualReloadOnlyForSession();
        }

        const bool rockEnabled = hasContextFlag(
            context->flags,
            rock::api::animation::AnimationPhaseContextFlag::RockEnabled);
        const bool rockReady = hasContextFlag(
            context->flags,
            rock::api::animation::AnimationPhaseContextFlag::ProviderReady);
        const bool skeletonReady = hasContextFlag(
            context->flags,
            rock::api::animation::AnimationPhaseContextFlag::SkeletonReady);
        frik_visual_authority::setSkeletonReadyHint(skeletonReady);

        const bool operational =
            g_config.enabled && rockEnabled && rockReady && skeletonReady;
        const auto enrichmentDemand = refreshEnrichmentDemand();
        switch (context->phase) {
        case rock::api::animation::AnimationPhase::NativeGraphOutput: {
            const bool climbingAnimationSuppressed =
                operational && refreshClimbingAnimationSuppression();
            const bool runtimeOperational =
                operational && s_runtimeOperational &&
                !climbingAnimationSuppressed;
            // Publish before capture so ROCK's local authored-grip reader can
            // yield at this same graph sample when Paper owns authority.
            publishRockAuthority(runtimeOperational);
            native_animation_authority::captureNativeGraphOutput();
            paper::RockWeaponGripState gripState{};
            if (enrichmentDemand.reloadObservation) {
                const auto observationStarted = PerformanceClock::now();
                if (rockApiClient().queryEquippedWeaponGripState(gripState) &&
                    hasGripFlag(
                        gripState.flags,
                        rock::api::weapon::GripFlag::Valid)) {
                    reload_observation::capturePhase(
                        *context,
                        gripState,
                        api::ReloadObservationPhase::
                            NativeGraphOutput);
                }
                s_reloadPerformance.observationCapture.add(
                    elapsedPerformanceMicroseconds(observationStarted));
            }
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            break;
        }
        case rock::api::animation::AnimationPhase::BeforeRock: {
            tactical_reload_bridge::prepareFrame();
            provider::beginFrame(context->frameIndex);

            if (operational &&
                !s_hookAttempted.exchange(true, std::memory_order_acq_rel)) {
                (void)merged_reload_compatibility::installHook();
                (void)reload_selection_trace::installHook();
                const bool lifecycleHooksReady =
                    native_animation_authority::installEventHooks();
                if (!lifecycleHooksReady) {
                    PAPER_LOG_CRITICAL(
                        Init,
                        "Native animation lifecycle hooks failed validation; Paper authority remains disabled");
                } else if (
                    tactical_reload_bridge::contractReady() &&
                    !tactical_reload_bridge::installInputHook()) {
                    PAPER_LOG_ERROR(
                        Init,
                        "Tactical Reload compatibility contract was found, but its validated ReadyWeapon bridge could not be installed");
                }
            }

            // Remove this provider's preceding aggregate before advancing the
            // local lifecycle. Re-publish only consumer requests first so a
            // just-ended local lease cannot keep itself alive through ROCK's
            // aggregate state.
            rockApiClient().clearNativeAnimationAuthority();
            const bool climbingAnimationSuppressed =
                operational && refreshClimbingAnimationSuppression();
            const auto weaponObservation = refreshWeaponState(*context);
            // Manual-only reaches the second weapon through ROCK, whose own
            // empty-trigger reload never passes the hooks above.
            const bool automaticReloadBlocked = manual_reload_only::runtimeEnabled();
            if (automaticReloadBlocked != s_automaticReloadBlockSent &&
                rockApiClient().setAutomaticReloadBlocked(automaticReloadBlocked)) {
                s_automaticReloadBlockSent = automaticReloadBlocked;
            }
            bolt_hold::update(
                rockApiClient(),
                bolt_hold::FrameInput{
                    .deltaSeconds = context->deltaSeconds,
                    .closeSeconds = g_config.boltHoldCloseSeconds,
                    .enabled = operational && g_config.boltHoldOpen,
                    .manualBoltEnabled = operational && g_config.boltAppreciation,
                    .playerReloadActive = native_animation_authority::playerReloadActive(),
                    .physicalReloadActive = provider::physicalReloadActive(),
                    .clipSamplerAvailable = !enrichmentDemand.exactAnimationEvidence,
                });
            manual_bolt::update(
                rockApiClient(),
                manual_bolt::FrameInput{
                    .chamberEnabled = operational && g_config.chamberRound,
                    .boltEnabled = operational && g_config.boltAppreciation,
                    .pullDistance = g_config.boltPullDistance,
                    .handFromAnimation = g_config.boltHandFromAnimation,
                    .cycleAllowed = !climbingAnimationSuppressed &&
                        native_animation_authority_policy::canWorkBoltByHand(weaponObservation),
                    .playerReloadActive = native_animation_authority::playerReloadActive(),
                    .physicalReloadActive = provider::physicalReloadActive(),
                    .handBlendSeconds = g_config.handBlendSeconds,
                    .deltaSeconds = context->deltaSeconds,
                });
            if (enrichmentDemand.reloadObservation) {
                const auto observationStarted = PerformanceClock::now();
                reload_observation::advanceFrame(
                    *context,
                    s_gripStateValid ? std::addressof(s_gripState) : nullptr,
                    provider::generation(),
                    enrichmentDemand.reloadEvidenceGeometry);
                s_reloadPerformance.observationAdvance.add(
                    elapsedPerformanceMicroseconds(observationStarted));
            }
            if (enrichmentDemand.animationTelemetry) {
                const auto animationStarted = PerformanceClock::now();
                animation_evidence::advanceFrame(
                    *context,
                    s_gripStateValid ? std::addressof(s_gripState) : nullptr,
                    provider::generation(),
                    enrichmentDemand.exactAnimationEvidence);
                s_reloadPerformance.animationAdvance.add(
                    elapsedPerformanceMicroseconds(animationStarted));
                recordAnimationDiagnostics(
                    animation_evidence::snapshotFrameDiagnostics());
            }
            const auto preLifecycleCompatibility =
                evaluateAnimationCompatibility(
                    *context,
                    "before-rock/pre-lifecycle",
                    weaponObservation,
                    currentPaperAnimationAuthority());
            bool runtimeOperational =
                operational && native_animation_authority::isHookInstalled() &&
                !climbingAnimationSuppressed &&
                preLifecycleCompatibility.compatible();
            configureRuntime(runtimeOperational);
            configureReloadCompatibility(
                operational && native_animation_authority::isHookInstalled());
            applyManualCycleEligibility(weaponObservation);
            publishRockAuthorityFlags(runtimeOperational ?
                provider::currentConsumerAuthorityFlags() :
                0);
            tactical_reload_bridge::beginFrame(context->deltaSeconds);
            native_animation_authority::beginRockFrame(context->deltaSeconds);
            if (runtimeOperational) {
                const auto postLifecycleCompatibility =
                    evaluateAnimationCompatibility(
                        *context,
                        "before-rock/post-lifecycle",
                        weaponObservation,
                        currentPaperAnimationAuthority());
                if (!postLifecycleCompatibility.compatible()) {
                    configureRuntime(false);
                    publishRockAuthorityFlags(0);
                    runtimeOperational = false;
                }
            }
            publishRockAuthority(runtimeOperational);
            if (runtimeOperational) {
                if (s_gripStateValid && weaponObservation.weaponIdentityCoherent) {
                    auto* equipped = f4vr::getEquippedItem();
                    auto* weapon = equipped && equipped->item.object ? equipped->item.object->As<RE::TESObjectWEAP>() : nullptr;
                    if (weapon && weapon->formID == s_gripState.weaponFormId) {
                        const auto* instance = equipped->item.instanceData ? equipped->item.instanceData.get() : &weapon->weaponData;
                        attachment_bindings::reconcile({s_gripState.weaponNode, reinterpret_cast<std::uintptr_t>(instance),
                            s_gripState.weaponGenerationKey, s_gripState.weaponFormId, context->worldGeneration, context->skeletonGeneration});
                    }
                }
                (void)native_animation_authority::applyCapturedPose(
                    native_animation_authority::ApplyPhase::BeforeRock);
            }
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            break;
        }
        case rock::api::animation::AnimationPhase::AfterRock: {
            const bool climbingAnimationSuppressed =
                operational && refreshClimbingAnimationSuppression();
            const auto weaponObservation = refreshWeaponState(*context);
            const auto compatibility = evaluateAnimationCompatibility(
                *context,
                "after-rock",
                weaponObservation,
                currentPaperAnimationAuthority());
            if ((!operational || climbingAnimationSuppressed ||
                    !compatibility.compatible()) &&
                s_runtimeOperational) {
                configureRuntime(false);
                publishRockAuthorityFlags(0);
            }
            if (!operational) {
                configureReloadCompatibility(false);
            }
            if (climbingAnimationSuppressed) {
                publishRockAuthorityFlags(0);
            }
            applyManualCycleEligibility(weaponObservation);
            if (s_runtimeOperational) {
                (void)native_animation_authority::applyCapturedPose(
                    native_animation_authority::ApplyPhase::AfterRock);
            }
            manual_bolt::presentHand(rockApiClient());
            chamber_ejection::update(rockApiClient(), *context);
            if (enrichmentDemand.reloadObservation && s_gripStateValid) {
                const auto observationStarted = PerformanceClock::now();
                reload_observation::capturePhase(
                    *context,
                    s_gripState,
                    api::ReloadObservationPhase::PostRock);
                s_reloadPerformance.observationCapture.add(
                    elapsedPerformanceMicroseconds(observationStarted));
            }
            debug_visualization::publish(*context, s_gripState);
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            break;
        }
        case rock::api::animation::AnimationPhase::Complete: {
            if (enrichmentDemand.nativePosePipeline) {
                const auto authority = currentPaperAnimationAuthority();
                native_pose_pipeline::publishFrame(
                    *context,
                    s_lastAnimationCompatibilityReason,
                    authority.localFlags,
                    authority.consumerFlags,
                    s_handlingStateValid ?
                        s_handlingState.state.weaponFormId :
                        s_gripState.weaponFormId,
                    s_handlingStateValid ?
                        s_handlingState.state.weaponGenerationKey :
                        s_gripState.weaponGenerationKey,
                    s_runtimeOperational);
            }
            native_animation_authority::completeRockFrame(context->frameIndex);
            publishRuntimeState(*context, rockApiClient().ready(), skeletonReady);
            if (enrichmentDemand.reloadObservation) {
                const auto observationStarted = PerformanceClock::now();
                reload_observation::completeFrame(
                    *context,
                    provider::generation());
                s_reloadPerformance.observationComplete.add(
                    elapsedPerformanceMicroseconds(observationStarted));
            }
            if (enrichmentDemand.animationTelemetry) {
                const auto animationStarted = PerformanceClock::now();
                animation_evidence::completeFrame(*context);
                s_reloadPerformance.animationComplete.add(
                    elapsedPerformanceMicroseconds(animationStarted));
            }
            if (enrichmentDemand.weaponMotion) {
                weapon_motion::completeFrame(
                    *context,
                    rockApiClient(),
                    provider::generation(),
                    enrichmentDemand.weaponMotionOptions);
            }
            if (enrichmentDemand.reloadStages) {
                reload_stages::completeFrame(*context);
            }
            const auto dispatchStarted = PerformanceClock::now();
            std::array<
                api::Event,
                api::kMaxWeaponMotionEvents> motionEvents{};
            const auto motionEventCount = weapon_motion::drainEvents(
                motionEvents.data(),
                static_cast<std::uint32_t>(motionEvents.size()));
            for (std::uint32_t index = 0;
                 index < motionEventCount;
                 ++index) {
                provider::dispatchEvent(motionEvents[index]);
            }
            provider::dispatchEvent(api::EventKind::FrameComplete);
            s_reloadPerformance.consumerDispatch.add(
                elapsedPerformanceMicroseconds(dispatchStarted));
            provider::completeFrame();
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            completePerformanceFrame(*context, enrichmentDemand);
            break;
        }
        case rock::api::animation::AnimationPhase::Presented: {
            native_animation_authority::capturePresentedHandBaselines(context->frameIndex);
            if (enrichmentDemand.nativePosePipeline) {
                const auto authority = currentPaperAnimationAuthority();
                native_pose_pipeline::publishFrame(*context, s_lastAnimationCompatibilityReason,
                    authority.localFlags, authority.consumerFlags,
                    s_handlingStateValid ? s_handlingState.state.weaponFormId : s_gripState.weaponFormId,
                    s_handlingStateValid ? s_handlingState.state.weaponGenerationKey : s_gripState.weaponGenerationKey,
                    s_runtimeOperational);
                provider::dispatchEvent(api::EventKind::PresentationComplete);
            }
            break;
        }
        default:
            break;
        }
    }

    bool connectRock()
    {
        if (!rockApiClient().initialize()) {
            PAPER_LOG_ERROR(
                Api,
                "ROCK provider API 2.0 connection unavailable; PAPER retries on the next game-session message");
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
            "Connected to ROCK provider API 2.0 animation phases");
        return true;
    }

    void resetSession()
    {
        manual_reload_only::resetSession();
        tactical_reload_bridge::resetSession();
        merged_reload_compatibility::setRuntimeEnabled(false);
        reload_selection_trace::setRuntimeEnabled(false);
        debug_visualization::clear();
        manual_bolt::reset(rockApiClient());
        chamber_ejection::reset();
        bolt_hold::reset(rockApiClient());
        (void)rockApiClient().setAutomaticReloadBlocked(false);
        s_automaticReloadBlockSent = false;
        rockApiClient().clearNativeAnimationAuthority();
        native_animation_authority::resetTransientState();
        attachment_bindings::reset();
        frik_visual_authority::setSkeletonReadyHint(false);
        s_gripState = {};
        s_handlingState = {};
        s_gripStateValid = false;
        s_handlingStateValid = false;
        s_animationCompatibilityState = {};
        s_lastAnimationCompatibilityReason =
            native_animation_authority_policy::
                NativeAnimationCompatibilityReason::None;
        s_lastCompatibilityLogSnapshot = {};
        s_hasCompatibilityLogSnapshot = false;
        s_lastAuthorityPublishFailureFlags = UINT32_MAX;
        s_runtimeOperational = false;
        s_climbingAnimationSuppressed = false;
        s_reloadObservationDemandActive = false;
        s_animationTelemetryDemandActive = false;
        s_reloadStageDemandActive = false;
        s_nativePosePipelineDemandActive = false;
        s_weaponMotionDemandActive = false;
        s_weaponMotionOptions = {};
        weapon_motion::reset(weapon_motion::ResetReason::RuntimeReset);
        s_reloadPerformance = {};
        provider::resetRuntime();
    }

    void onF4SEMessage(F4SE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        if (message->type == F4SE::MessagingInterface::kGameLoaded) {
            // Before the ROCK connection below can find a mismatch.
            required_updates::watch();
            (void)g_config.reload();
            native_animation_authority::initializeCycleTrace();
            weapon_motion::configureCache(g_config);
            publishConfigState();
            g_config.startWatching();
            configureManualReloadOnlyForSession();
            tactical_reload_bridge::initializeSession();
            s_gameLoaded.store(true, std::memory_order_release);
            (void)connectRock();
            PAPER_LOG_INFO(
                Init,
                "GameLoaded complete; waiting for ROCK skeleton-ready phases");
            return;
        }

        if (message->type == F4SE::MessagingInterface::kPreLoadGame) {
            s_gameLoaded.store(false, std::memory_order_release);
            resetSession();
            return;
        }

        if (message->type == F4SE::MessagingInterface::kPostLoadGame ||
            message->type == F4SE::MessagingInterface::kNewGame) {
            s_gameLoaded.store(false, std::memory_order_release);
            resetSession();
            (void)g_config.reload();
            weapon_motion::configureCache(g_config);
            publishConfigState();
            g_config.startWatching();
            configureManualReloadOnlyForSession();
            tactical_reload_bridge::initializeSession();
            s_gameLoaded.store(true, std::memory_order_release);
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

    const auto executableVersion = REL::Module::get().version();
    if (executableVersion != F4SE::RUNTIME_VR_1_2_72) {
        PAPER_LOG_CRITICAL(
            Init,
            "Unsupported Fallout4VR executable version {} (need {})",
            executableVersion.string(),
            F4SE::RUNTIME_VR_1_2_72.string());
        return false;
    }
    PAPER_LOG_INFO(
        Init,
        "FO4VR query compatibility passed F4SE={} loaderRuntime={} executable={}",
        f4se->F4SEVersion().string(),
        f4se->RuntimeVersion().string(),
        executableVersion.string());
    return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(
    const F4SE::LoadInterface* f4se)
{
    F4SE::Init(f4se, false);
    F4SE::AllocTrampoline(112); // four reload relays, opt-in physical bolt control, two for live rounds
    paper::provider::initialize();
    paper::manual_bolt::installSerialization();

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
        paper::api::kRecordVersion);
    return true;
}
