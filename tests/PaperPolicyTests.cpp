#include "animation/NativeAnimationAuthorityPolicy.h"

#include <cassert>

int main()
{
    using namespace paper::native_animation_authority_policy;

    struct AffineTransform
    {
        float scale;
        float translate;
    };
    constexpr auto compose = [](
                                 const AffineTransform parent,
                                 const AffineTransform child) {
        return AffineTransform{
            parent.scale * child.scale,
            parent.translate + parent.scale * child.translate,
        };
    };
    constexpr auto invert = [](const AffineTransform value) {
        return AffineTransform{
            1.0f / value.scale,
            -value.translate / value.scale,
        };
    };

    static_assert(resolveLocalReloadAuthorityFlags(
                      LocalReloadAuthoritySelection{}) == 0);
    static_assert(resolveLocalReloadAuthorityFlags(
                      LocalReloadAuthoritySelection{
                          .leaseActive = true,
                      }) == kReloadPose);
    static_assert(resolveLocalReloadAuthorityFlags(
                      LocalReloadAuthoritySelection{
                          .leaseActive = true,
                          .partialAuthorityEnabled = true,
                      }) == kWeaponFixedHandsPose);
    static_assert((kWeaponFixedHandsPose & kWeapon) == 0);

    static_assert(resolveWeaponFixedHandTargetMode(
                      false,
                      WeaponFixedHandRole::Primary) ==
                  WeaponFixedHandTargetMode::LiveGripDelta);
    static_assert(resolveWeaponFixedHandTargetMode(
                      false,
                      WeaponFixedHandRole::Support) ==
                  WeaponFixedHandTargetMode::LiveGripDelta);
    static_assert(resolveWeaponFixedHandTargetMode(
                      true,
                      WeaponFixedHandRole::Primary) ==
                  WeaponFixedHandTargetMode::LiveGripDelta);
    static_assert(resolveWeaponFixedHandTargetMode(
                      true,
                      WeaponFixedHandRole::Support) ==
                  WeaponFixedHandTargetMode::NativeWeaponRelative);
    static_assert(!shouldPublishWeaponFixedSupportHand(false, false));
    static_assert(shouldPublishWeaponFixedSupportHand(false, true));
    static_assert(shouldPublishWeaponFixedSupportHand(true, false));
    static_assert(shouldPublishWeaponFixedSupportHand(true, true));

    static_assert(!isManualCycleFireAnimationAllowed(
        ManualCycleWeaponEligibility{}));
    static_assert(isManualCycleFireAnimationAllowed(
        ManualCycleWeaponEligibility{
            .boltAction = true,
        }));
    static_assert(isManualCycleFireAnimationAllowed(
        ManualCycleWeaponEligibility{
            .revolverAnimation = true,
        }));
    static_assert(isManualCycleFireAnimationAllowed(
        ManualCycleWeaponEligibility{
            .shotgun = true,
        }));
    static_assert(isManualCycleFireAnimationAllowed(
        ManualCycleWeaponEligibility{
            .rifle = true,
        }));
    static_assert(isManualCycleFireAnimationAllowed(
        ManualCycleWeaponEligibility{
            .manualCycleAnimationKeyword = true,
        }));
    static_assert(isManualCycleFireAnimationAllowed(
        ManualCycleWeaponEligibility{
            .semanticActionPart = true,
        }));
    static_assert(isManualCycleSemanticActionPart(4, 0));
    static_assert(isManualCycleSemanticActionPart(9, 0));
    static_assert(isManualCycleSemanticActionPart(12, 0));
    static_assert(isManualCycleSemanticActionPart(13, 0));
    static_assert(isManualCycleSemanticActionPart(18, 0));
    static_assert(isManualCycleSemanticActionPart(22, 1));
    static_assert(isManualCycleSemanticActionPart(22, 4));
    static_assert(isManualCycleSemanticActionPart(22, 7));
    static_assert(!isManualCycleSemanticActionPart(10, 2));
    static_assert(!isManualCycleSemanticActionPart(11, 3));
    static_assert(!isManualCycleSemanticActionPart(22, 0));

    constexpr ManualCycleHandAnimationEligibility eligible{
        .gripStateValid = true,
        .firingHandIsLeft = false,
    };
    static_assert(canApplyManualCycleHandAnimation(eligible));
    static_assert([=] {
        auto input = eligible;
        input.gripStateValid = false;
        return !canApplyManualCycleHandAnimation(input);
    }());
    static_assert([=] {
        auto input = eligible;
        input.firingHandIsLeft = true;
        return !canApplyManualCycleHandAnimation(input);
    }());

    constexpr auto authoredSupportGripLatch =
        advanceManualCycleAuthoredSupportGripLatch(
            {},
            ManualCycleAuthoredSupportGripLatchObservation{
                .observedWeaponGenerationKey = 42,
                .leaseActive = true,
                .leaseStarted = true,
                .authoredSupportGripActive = true,
            });
    static_assert(authoredSupportGripLatch.active);
    static_assert(
        authoredSupportGripLatch.weaponGenerationKey == 42);
    static_assert(
        advanceManualCycleAuthoredSupportGripLatch(
            authoredSupportGripLatch,
            ManualCycleAuthoredSupportGripLatchObservation{
                .observedWeaponGenerationKey = 42,
                .leaseActive = true,
            }).active);
    static_assert(
        !advanceManualCycleAuthoredSupportGripLatch(
            {},
            ManualCycleAuthoredSupportGripLatchObservation{
                .observedWeaponGenerationKey = 42,
                .leaseActive = true,
                .leaseStarted = true,
                .authoredSupportGripActive = false,
            }).active);
    static_assert(
        !advanceManualCycleAuthoredSupportGripLatch(
            authoredSupportGripLatch,
            ManualCycleAuthoredSupportGripLatchObservation{
                .observedWeaponGenerationKey = 42,
                .leaseActive = true,
                .activeNonAuthoredGripObserved = true,
            }).active);
    static_assert(
        !advanceManualCycleAuthoredSupportGripLatch(
            authoredSupportGripLatch,
            ManualCycleAuthoredSupportGripLatchObservation{
                .observedWeaponGenerationKey = 43,
                .leaseActive = true,
            }).active);
    static_assert(
        !advanceManualCycleAuthoredSupportGripLatch(
            authoredSupportGripLatch,
            ManualCycleAuthoredSupportGripLatchObservation{
                .observedWeaponGenerationKey = 42,
                .leaseActive = false,
            }).active);

    static_assert(!updateManualCycleHandMotionQualification(
        false,
        ManualCycleHandMotionSample{
            .translationGameUnits = 1.499f,
            .rotationDegrees = 9.999f,
        }));
    static_assert(updateManualCycleHandMotionQualification(
        false,
        ManualCycleHandMotionSample{
            .translationGameUnits =
                kManualCycleHandMotionTranslationThresholdGameUnits,
        }));
    static_assert(updateManualCycleHandMotionQualification(
        false,
        ManualCycleHandMotionSample{
            .rotationDegrees =
                kManualCycleHandMotionRotationThresholdDegrees,
        }));
    static_assert(updateManualCycleHandMotionQualification(
        true,
        ManualCycleHandMotionSample{}));
    static_assert(!isManualCycleActionPartMotion(
        ManualCycleHandMotionSample{
            .translationGameUnits = 0.049f,
            .rotationDegrees = 0.499f,
        }));
    static_assert(isManualCycleActionPartMotion(
        ManualCycleHandMotionSample{
            .translationGameUnits =
                kManualCycleActionPartTranslationThresholdGameUnits,
        }));
    static_assert(isManualCycleActionPartMotion(
        ManualCycleHandMotionSample{
            .rotationDegrees =
                kManualCycleActionPartRotationThresholdDegrees,
        }));

    constexpr LocalManualCycleCandidateState cycleCandidate{
        .watchdogSecondsRemaining = 4.0f,
        .reloadStartSequenceAtArm = 7,
        .reloadEndSequenceAtArm = 20,
        .actionPartMotionSequenceAtArm = 11,
        .weaponGenerationKey = 42,
        .weaponFormId = 0x1234,
    };
    constexpr LocalManualCycleCandidateSignal stableCandidateSignal{
        .reloadStartSequence = 7,
        .reloadEndSequence = 20,
        .actionPartMotionSequence = 11,
        .weaponGenerationKey = 42,
        .weaponFormId = 0x1234,
        .deltaSeconds = 0.01f,
    };
    static_assert(advanceLocalManualCycleCandidate(
                      cycleCandidate,
                      stableCandidateSignal)
                      .pending());
    static_assert([=] {
        auto signal = stableCandidateSignal;
        ++signal.reloadEndSequence;
        const auto step = advanceLocalManualCycleCandidate(
            cycleCandidate,
            signal);
        return step.activate() &&
               step.result ==
                   LocalManualCycleCandidateResult::
                       ActivateReloadEnd;
    }());
    static_assert([=] {
        auto signal = stableCandidateSignal;
        ++signal.actionPartMotionSequence;
        const auto step = advanceLocalManualCycleCandidate(
            cycleCandidate,
            signal);
        return step.activate() &&
               step.result ==
                   LocalManualCycleCandidateResult::
                       ActivateActionPartMotion;
    }());
    static_assert([=] {
        auto signal = stableCandidateSignal;
        ++signal.weaponGenerationKey;
        return advanceLocalManualCycleCandidate(
                   cycleCandidate,
                   signal)
                   .result ==
               LocalManualCycleCandidateResult::
                   WeaponIdentityChanged;
    }());
    static_assert([=] {
        auto signal = stableCandidateSignal;
        ++signal.reloadStartSequence;
        return advanceLocalManualCycleCandidate(
                   cycleCandidate,
                   signal)
                   .result ==
               LocalManualCycleCandidateResult::ReloadStarted;
    }());

    constexpr AffineTransform nativeWeaponModel{ 2.0f, 40.0f };
    constexpr AffineTransform nativeHandModel{ 4.0f, 80.0f };
    constexpr auto nativeHandInWeapon = resolveNativeHandInWeapon(
        nativeWeaponModel,
        nativeHandModel,
        compose,
        invert);
    constexpr AffineTransform liveWeaponWorld{ 4.0f, 200.0f };
    constexpr auto nativeHandWorld = resolveNativeHandWorld(
        liveWeaponWorld,
        nativeHandInWeapon,
        compose);
    static_assert(nativeHandInWeapon.scale == 2.0f);
    static_assert(nativeHandInWeapon.translate == 20.0f);
    static_assert(nativeHandWorld.scale == 8.0f);
    static_assert(nativeHandWorld.translate == 280.0f);

    constexpr auto controllerCorrection =
        resolveControllerAnchoredPoseCorrection(
            AffineTransform{ 5.0f, 100.0f },
            AffineTransform{ 2.0f, 10.0f },
            AffineTransform{ 6.0f, 18.0f },
            compose,
            invert);
    constexpr auto corrected = compose(
        controllerCorrection,
        AffineTransform{ 6.0f, 18.0f });
    static_assert(corrected.scale == 15.0f);
    static_assert(corrected.translate == 120.0f);

    constexpr auto sharedTargetCorrection =
        resolvePoseCorrectionToWorldTarget(
            AffineTransform{ 5.0f, 100.0f },
            AffineTransform{ 2.0f, 10.0f },
            compose,
            invert);
    constexpr auto sharedTarget = compose(
        sharedTargetCorrection,
        AffineTransform{ 2.0f, 10.0f });
    static_assert(sharedTarget.scale == 5.0f);
    static_assert(sharedTarget.translate == 100.0f);

    constexpr LocalReloadLeaseState waiting{
        .watchdogFramesRemaining = 600,
        .startSequenceAtArm = 10,
        .endSequenceAtArm = 4,
    };
    constexpr auto waitingStep = advanceLocalReloadLease(
        waiting,
        LocalReloadLifecycleSignal{ 10, 4, false });
    static_assert(waitingStep.active());
    static_assert(!waitingStep.state.observedReloadStart);
    constexpr auto startedStep = advanceLocalReloadLease(
        waitingStep.state,
        LocalReloadLifecycleSignal{ 11, 4, true });
    static_assert(startedStep.active());
    static_assert(startedStep.state.observedReloadStart);
    constexpr auto endedStep = advanceLocalReloadLease(
        startedStep.state,
        LocalReloadLifecycleSignal{ 11, 5, false });
    static_assert(!endedStep.active());
    static_assert(
        endedStep.endReason == LocalReloadLeaseEndReason::ReloadEnded);

    constexpr auto reloadWatchdog = advanceLocalReloadLease(
        LocalReloadLeaseState{
            .watchdogFramesRemaining = 1,
            .startSequenceAtArm = 11,
            .endSequenceAtArm = 5,
            .observedReloadStart = true,
        },
        LocalReloadLifecycleSignal{ 11, 5, true });
    static_assert(!reloadWatchdog.active());
    static_assert(
        reloadWatchdog.endReason ==
        LocalReloadLeaseEndReason::WatchdogExpired);

    constexpr LocalManualCycleLeaseState cycleArmed{
        .watchdogSecondsRemaining = 4.0f,
        .reloadStartSequenceAtArm = 7,
        .lastReloadEndSequence = 20,
    };
    constexpr auto cycleEntered = advanceLocalManualCycleLease(
        cycleArmed,
        LocalManualCycleLifecycleSignal{ 7, 21, 0.0625f });
    static_assert(cycleEntered.active());
    static_assert(cycleEntered.state.observedReloadEndEvents == 1);
    constexpr auto cycleCompleted = advanceLocalManualCycleLease(
        cycleEntered.state,
        LocalManualCycleLifecycleSignal{ 7, 22, 0.25f });
    static_assert(!cycleCompleted.active());
    static_assert(
        cycleCompleted.endReason ==
        LocalManualCycleLeaseEndReason::CycleBracketEnded);

    constexpr auto cyclePreempted = advanceLocalManualCycleLease(
        cycleArmed,
        LocalManualCycleLifecycleSignal{ 8, 20, 0.25f });
    static_assert(!cyclePreempted.active());
    static_assert(
        cyclePreempted.endReason ==
        LocalManualCycleLeaseEndReason::ReloadStarted);

    constexpr auto cycleWatchdog = advanceLocalManualCycleLease(
        LocalManualCycleLeaseState{
            .watchdogSecondsRemaining = 0.01f,
            .reloadStartSequenceAtArm = 7,
            .lastReloadEndSequence = 20,
        },
        LocalManualCycleLifecycleSignal{ 7, 20, 0.02f });
    static_assert(!cycleWatchdog.active());
    static_assert(
        cycleWatchdog.endReason ==
        LocalManualCycleLeaseEndReason::WatchdogExpired);

    static_assert(kManualCyclePose == (kArms | kHands));
    static_assert((kManualCyclePose & kWeapon) == 0);
    static_assert(classifyBone("RArm_Collarbone") == kArms);
    static_assert(classifyBone("LArm_ForeArm3") == kArms);
    static_assert(classifyBone("RArm_Hand") == (kArms | kHands));
    static_assert(classifyBone("LArm_Finger23") == kHands);
    static_assert(classifyBone("RArm_Thumb1") == kHands);
    static_assert(classifyBone("Weapon") == kWeapon);
    static_assert(classifyBone("weaponleft") == kWeapon);
    static_assert(classifyBone("Root") == 0);
    static_assert(classifyBone("COM") == 0);
    static_assert(classifyBone("SPINE1") == 0);
    static_assert(classifyBone("WeaponMagazine") == 0);
    static_assert(containsIgnoreCase(
        "AnimsDakVintageRepeater",
        "repeater"));
    static_assert(containsIgnoreCase(
        "DLC03_ma_LeverGun",
        "LEVER"));
    static_assert(!containsIgnoreCase(
        "WeaponTypeRifle",
        "lever"));

    assert(isRequested(classifyBone("LArm_Hand"), kArms));
    assert(isRequested(classifyBone("LArm_Hand"), kHands));
    assert(!isRequested(classifyBone("LArm_Finger11"), kArms));
    assert(!isRequested(classifyBone("RArm_UpperArm"), kHands));
    assert(isRequested(classifyBone("WeaponLeft"), kReloadPose));
    return 0;
}
