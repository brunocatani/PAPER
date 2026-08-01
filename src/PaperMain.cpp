#include "PCH.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation/NativeAnimationAuthorityPolicy.h"
#include "animation_evidence/AnimationEvidence.h"
#include "api/ApiTransform.h"
#include "api/PAPERProvider.h"
#include "api/RockApiClient.h"
#include "api/RockVisualAuthorityBridge.h"
#include "compat/TacticalReloadBridge.h"
#include "debug/NativeAnimationDebugVisualization.h"
#include "PaperConfig.h"
#include "PaperLog.h"
#include "reload_observation/ReloadObservation.h"

#include <atomic>
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
    bool s_reloadObservationDemandActive{ false };
    bool s_animationTelemetryDemandActive{ false };
    rock::provider::RockProviderEquippedWeaponGripStateV1 s_gripState{};
    rock::provider::RockProviderEquippedWeaponHandlingStateV1
        s_handlingState{};
    bool s_gripStateValid{ false };
    bool s_handlingStateValid{ false };
    native_animation_authority_policy::NativeAnimationCompatibilityState
        s_animationCompatibilityState{};

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
        const rock::provider::RockProviderAnimationPhaseV1 phase,
        const std::uint64_t microseconds)
    {
        s_reloadPerformance.currentFrameMicroseconds += microseconds;
        switch (phase) {
        case rock::provider::RockProviderAnimationPhaseV1::NativeGraphOutput:
            s_reloadPerformance.nativeGraphPhase.add(microseconds);
            break;
        case rock::provider::RockProviderAnimationPhaseV1::BeforeRock:
            s_reloadPerformance.beforeRockPhase.add(microseconds);
            break;
        case rock::provider::RockProviderAnimationPhaseV1::AfterRock:
            s_reloadPerformance.afterRockPhase.add(microseconds);
            break;
        case rock::provider::RockProviderAnimationPhaseV1::Complete:
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
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
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
        const bool shouldReport =
            performance.observationDemandSeen ||
            performance.animationDemandSeen ||
            performance.exactAnimationDemandSeen ||
            performance.gameDeltaMillisecondsMaximum >= 20.0 ||
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
        const bool exactAnimationEvidence = provider::hasConsumerCapability(
            api::PaperConsumerCapabilityV1::ReloadAnimationEvidence);
        const bool animationTelemetry = exactAnimationEvidence ||
            provider::hasConsumerCapability(
                api::PaperConsumerCapabilityV1::ReloadAnimationTelemetry);
        const bool reloadEvidenceGeometry =
            provider::hasConsumerCapability(
                api::PaperConsumerCapabilityV1::ReloadEvidenceGeometry);
        const bool reloadObservation = animationTelemetry ||
            provider::hasConsumerCapability(
                api::PaperConsumerCapabilityV1::ReloadObservations) ||
            reloadEvidenceGeometry;

        if (s_animationTelemetryDemandActive && !animationTelemetry) {
            animation_evidence::reset();
        }
        if (s_reloadObservationDemandActive && !reloadObservation) {
            reload_observation::reset();
        }
        s_reloadObservationDemandActive = reloadObservation;
        s_animationTelemetryDemandActive = animationTelemetry;
        return EnrichmentDemand{
            .reloadObservation = reloadObservation,
            .reloadEvidenceGeometry = reloadEvidenceGeometry,
            .animationTelemetry = animationTelemetry,
            .exactAnimationEvidence = exactAnimationEvidence,
        };
    }

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

    [[nodiscard]] bool hasHandlingRuntimeFlag(
        const std::uint32_t flags,
        const rock::provider::
            RockProviderEquippedWeaponHandlingRuntimeFlagV1 flag)
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] bool hasAuthoredGripFlag(
        const std::uint32_t flags,
        const rock::provider::RockProviderAuthoredGripPoseFlagV1 flag)
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

    [[nodiscard]] native_animation_authority_policy::
        NativeAnimationCompatibilityObservation
    refreshWeaponState()
    {
        s_handlingState = {};
        s_handlingStateValid =
            rockApiClient().queryEquippedWeaponHandlingState(
                s_handlingState);
        s_gripState = {};
        const bool queried =
            rockApiClient().queryEquippedWeaponGripState(s_gripState);
        s_gripStateValid = queried && hasGripFlag(
            s_gripState.flags,
            rock::provider::RockProviderEquippedWeaponGripStateFlagV1::Valid);
        const bool gripFiringHandIsLeft =
            s_gripStateValid && hasGripFlag(
                s_gripState.flags,
                rock::provider::
                    RockProviderEquippedWeaponGripStateFlagV1::
                        FiringHandLeft);
        const bool handlingFiringHandIsLeft =
            s_handlingStateValid &&
            (s_handlingState.currentFiringHand ==
                    rock::provider::RockProviderHand::Left ||
                hasHandlingRuntimeFlag(
                    s_handlingState.runtimeFlags,
                    rock::provider::
                        RockProviderEquippedWeaponHandlingRuntimeFlagV1::
                            FiringHandLeft));
        const bool partCarryActive =
            s_handlingStateValid && hasHandlingRuntimeFlag(
                s_handlingState.runtimeFlags,
                rock::provider::
                    RockProviderEquippedWeaponHandlingRuntimeFlagV1::
                        PartCarryActive);
        const bool weaponPresent =
            s_handlingStateValid && hasHandlingRuntimeFlag(
                s_handlingState.runtimeFlags,
                rock::provider::
                    RockProviderEquippedWeaponHandlingRuntimeFlagV1::
                        WeaponPresent);
        const bool weaponIdentityCoherent =
            s_handlingStateValid &&
            (!s_gripStateValid ||
                (s_handlingState.weaponFormId ==
                        s_gripState.weaponFormId &&
                    s_handlingState.weaponGenerationKey ==
                        s_gripState.weaponGenerationKey));

        native_animation_authority::ManualCycleRockGripSnapshot snapshot{};
        snapshot.weaponGenerationKey =
            s_gripStateValid ? s_gripState.weaponGenerationKey : 0;
        if (s_gripStateValid && hasGripFlag(
                s_gripState.flags,
                rock::provider::RockProviderEquippedWeaponGripStateFlagV1::RightHandInWeaponValid)) {
            snapshot.rightHandInWeapon =
                api_transform::toNi(s_gripState.rightHandInWeapon);
            snapshot.rightValid = true;
        }

        rock::provider::RockProviderWeaponPartGripStateV1 leftPartGrip{};
        const bool leftPartGripStateValid =
            s_gripStateValid &&
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
        }

        rock::provider::RockProviderAuthoredGripPoseV1 authoredGrip{};
        const bool authoredSupportPoseValid =
            leftSupportCandidate &&
            rockApiClient().querySelectedAuthoredGripPose(authoredGrip) &&
            hasAuthoredGripFlag(
                authoredGrip.flags,
                rock::provider::RockProviderAuthoredGripPoseFlagV1::Valid) &&
            hasAuthoredGripFlag(
                authoredGrip.flags,
                rock::provider::RockProviderAuthoredGripPoseFlagV1::
                    LeftHandValid) &&
            authoredGrip.weaponGenerationKey ==
                s_gripState.weaponGenerationKey &&
            authoredGrip.weaponFormId == s_gripState.weaponFormId;
        if (authoredSupportPoseValid) {
            snapshot.authoredLeftHandInWeapon =
                api_transform::toNi(authoredGrip.leftHandInWeapon);
            snapshot.authoredLeftValid = true;
        }
        native_animation_authority::setManualCycleRockGripSnapshot(snapshot);
        return native_animation_authority_policy::
            NativeAnimationCompatibilityObservation{
                .weaponGenerationKey =
                    s_handlingStateValid ?
                        s_handlingState.weaponGenerationKey :
                        0,
                .weaponFormId =
                    s_handlingStateValid ?
                        s_handlingState.weaponFormId :
                        0,
                .handlingStateValid = s_handlingStateValid,
                .gripStateValid = s_gripStateValid,
                .firingHandIsLeft =
                    gripFiringHandIsLeft ||
                    handlingFiringHandIsLeft,
                .partCarryActive = partCarryActive,
                .weaponPresent = weaponPresent,
                .weaponIdentityCoherent =
                    weaponIdentityCoherent,
        };
    }

    void configureRuntime(const bool operational)
    {
        tactical_reload_bridge::setRuntimeEnabled(operational);
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

    void observeAnimationCompatibility(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
        const char* observationPoint,
        const native_animation_authority_policy::
            NativeAnimationCompatibilityObservation& observation,
        const native_animation_authority_policy::
            NativeAnimationCompatibilityStep& step,
        const PaperAnimationAuthorityObservation& authority)
    {
        const CompatibilityLogSnapshot snapshot{
            .handlingOwnerToken = s_handlingState.ownerToken,
            .weaponGenerationKey = observation.weaponGenerationKey,
            .gripWeaponGenerationKey =
                s_gripStateValid ? s_gripState.weaponGenerationKey : 0,
            .handlingAuthorityFlags = s_handlingState.authorityFlags,
            .handlingRuntimeFlags = s_handlingState.runtimeFlags,
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
            hasHandlingRuntimeFlag(
                s_handlingState.runtimeFlags,
                rock::provider::
                    RockProviderEquippedWeaponHandlingRuntimeFlagV1::
                        AuthorityActive);
        PAPER_LOG_INFO(
            Animation,
            "Weapon-animation compatibility point={} result={} animationAuthorityActive={} handlingValid={} gripValid={} identityCoherent={} externalHandlingAuthority={} owner={:016X} authorityFlags=0x{:08X} runtimeFlags=0x{:08X} localFlags=0x{:08X} consumerFlags=0x{:08X} weapon={:08X}/{:016X} gripWeapon={:08X}/{:016X} world={} skeleton={} provider={}",
            observationPoint,
            native_animation_authority_policy::
                nativeAnimationCompatibilityReasonName(step.reason),
            authority.requestedFlags() != 0,
            observation.handlingStateValid,
            observation.gripStateValid,
            observation.weaponIdentityCoherent,
            externalHandlingAuthorityActive,
            s_handlingState.ownerToken,
            s_handlingState.authorityFlags,
            s_handlingState.runtimeFlags,
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
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
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
        state.weaponFormId = s_handlingStateValid ?
            s_handlingState.weaponFormId :
            s_gripState.weaponFormId;
        state.captureSequence = native.captureSequence;
        state.reloadStartSequence = native.reloadStartSequence;
        state.reloadEndSequence = native.reloadEndSequence;
        state.frameIndex = context.frameIndex;
        state.weaponGenerationKey = s_handlingStateValid ?
            s_handlingState.weaponGenerationKey :
            s_gripState.weaponGenerationKey;
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
        beginPerformancePhase(context->frameIndex);
        const auto phaseStarted = PerformanceClock::now();

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
        const auto enrichmentDemand = refreshEnrichmentDemand();
        switch (context->phase) {
        case rock::provider::RockProviderAnimationPhaseV1::NativeGraphOutput: {
            const bool runtimeOperational =
                operational && s_runtimeOperational;
            // Publish before capture so ROCK's local authored-grip reader can
            // yield at this same graph sample when Paper owns authority.
            publishRockAuthority(runtimeOperational);
            native_animation_authority::captureNativeGraphOutput();
            rock::provider::RockProviderEquippedWeaponGripStateV1 gripState{};
            if (enrichmentDemand.reloadObservation) {
                const auto observationStarted = PerformanceClock::now();
                if (rockApiClient().queryEquippedWeaponGripState(gripState) &&
                    hasGripFlag(
                        gripState.flags,
                        rock::provider::
                            RockProviderEquippedWeaponGripStateFlagV1::Valid)) {
                    reload_observation::capturePhase(
                        *context,
                        gripState,
                        api::PaperReloadObservationPhaseV1::
                            NativeGraphOutput);
                }
                s_reloadPerformance.observationCapture.add(
                    elapsedPerformanceMicroseconds(observationStarted));
            }
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            break;
        }
        case rock::provider::RockProviderAnimationPhaseV1::BeforeRock: {
            provider::beginFrame(context->frameIndex);

            if (operational &&
                !s_hookAttempted.exchange(true, std::memory_order_acq_rel)) {
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
            const auto weaponObservation = refreshWeaponState();
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
                preLifecycleCompatibility.compatible();
            configureRuntime(runtimeOperational);
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
                (void)native_animation_authority::applyCapturedPose(
                    native_animation_authority::ApplyPhase::BeforeRock);
            }
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            break;
        }
        case rock::provider::RockProviderAnimationPhaseV1::AfterRock: {
            const auto weaponObservation = refreshWeaponState();
            const auto compatibility = evaluateAnimationCompatibility(
                *context,
                "after-rock",
                weaponObservation,
                currentPaperAnimationAuthority());
            if ((!operational || !compatibility.compatible()) &&
                s_runtimeOperational) {
                configureRuntime(false);
                publishRockAuthorityFlags(0);
            }
            applyManualCycleEligibility(weaponObservation);
            if (s_runtimeOperational) {
                (void)native_animation_authority::applyCapturedPose(
                    native_animation_authority::ApplyPhase::AfterRock);
            }
            if (enrichmentDemand.reloadObservation && s_gripStateValid) {
                const auto observationStarted = PerformanceClock::now();
                reload_observation::capturePhase(
                    *context,
                    s_gripState,
                    api::PaperReloadObservationPhaseV1::PostRock);
                s_reloadPerformance.observationCapture.add(
                    elapsedPerformanceMicroseconds(observationStarted));
            }
            debug_visualization::publish(*context, s_gripState);
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            break;
        }
        case rock::provider::RockProviderAnimationPhaseV1::Complete: {
            native_animation_authority::completeRockFrame();
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
            const auto dispatchStarted = PerformanceClock::now();
            provider::dispatchEvent(api::PaperEventKindV1::FrameComplete);
            s_reloadPerformance.consumerDispatch.add(
                elapsedPerformanceMicroseconds(dispatchStarted));
            provider::completeFrame();
            recordPhasePerformance(context->phase,
                elapsedPerformanceMicroseconds(phaseStarted));
            completePerformanceFrame(*context, enrichmentDemand);
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
        tactical_reload_bridge::resetSession();
        debug_visualization::clear();
        rockApiClient().clearNativeAnimationAuthority();
        native_animation_authority::resetTransientState();
        frik_visual_authority::setSkeletonReadyHint(false);
        s_gripState = {};
        s_handlingState = {};
        s_gripStateValid = false;
        s_handlingStateValid = false;
        s_animationCompatibilityState = {};
        s_lastCompatibilityLogSnapshot = {};
        s_hasCompatibilityLogSnapshot = false;
        s_lastAuthorityPublishFailureFlags = UINT32_MAX;
        s_runtimeOperational = false;
        s_reloadObservationDemandActive = false;
        s_animationTelemetryDemandActive = false;
        s_reloadPerformance = {};
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
            resetSession();
            (void)g_config.reload();
            publishConfigState();
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
