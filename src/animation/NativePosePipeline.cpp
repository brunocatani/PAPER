#include "animation/NativePosePipeline.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation/NativePosePipelinePolicy.h"
#include "api/ApiTransform.h"
#include "api/PAPERProvider.h"
#include "api/RockApiClient.h"

#include <array>
#include <cstddef>

namespace paper::native_pose_pipeline
{
    namespace
    {
        using namespace api;

        [[nodiscard]] constexpr std::uint32_t bit(
            const PaperNativePoseFrameFlagV1 flag)
        {
            return static_cast<std::uint32_t>(flag);
        }

        [[nodiscard]] constexpr std::uint32_t bit(
            const PaperNativeHandSolutionFlagV1 flag)
        {
            return static_cast<std::uint32_t>(flag);
        }

        [[nodiscard]] constexpr bool hasPresentedFlag(
            const std::uint32_t flags,
            const rock::api::hands::PresentedHandPoseFlagV1 flag)
        {
            return (flags & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] constexpr PaperNativePoseCompatibilityReasonV1
            publicCompatibilityReason(
                const native_animation_authority_policy::
                    NativeAnimationCompatibilityReason reason)
        {
            using Internal = native_animation_authority_policy::
                NativeAnimationCompatibilityReason;
            switch (reason) {
            case Internal::HandlingStateUnavailable:
                return PaperNativePoseCompatibilityReasonV1::
                    HandlingStateUnavailable;
            case Internal::LeftFiringHand:
                return PaperNativePoseCompatibilityReasonV1::LeftFiringHand;
            case Internal::PartCarry:
                return PaperNativePoseCompatibilityReasonV1::PartCarry;
            case Internal::WeaponUnavailable:
                return PaperNativePoseCompatibilityReasonV1::WeaponUnavailable;
            case Internal::WeaponIdentityChanged:
                return PaperNativePoseCompatibilityReasonV1::
                    WeaponIdentityChanged;
            case Internal::AnimationRequestResetRequired:
                return PaperNativePoseCompatibilityReasonV1::
                    AnimationRequestResetRequired;
            case Internal::None:
            default:
                return PaperNativePoseCompatibilityReasonV1::None;
            }
        }

        [[nodiscard]] PaperNativePoseModeV1 resolveMode(
            const native_animation_authority::DebugAuthoritySnapshot& debug,
            const std::uint32_t localAuthorityFlags,
            const std::uint32_t consumerAuthorityFlags)
        {
            if ((localAuthorityFlags | consumerAuthorityFlags) == 0) {
                return PaperNativePoseModeV1::Inactive;
            }
            if (consumerAuthorityFlags != 0 && localAuthorityFlags == 0) {
                return PaperNativePoseModeV1::ConsumerAuthority;
            }
            if (debug.runtime.localManualCycleLeaseActive) {
                return PaperNativePoseModeV1::ManualCycle;
            }
            if (debug.partialReloadExpected) {
                return PaperNativePoseModeV1::PartialReload;
            }
            return PaperNativePoseModeV1::FullReload;
        }

        [[nodiscard]] PaperNativePoseApplicationResultV1
            resolveApplicationResult(
                const native_animation_authority::DebugAuthoritySnapshot& debug,
                const std::uint32_t requestedAuthorityFlags,
                const PaperNativePoseCompatibilityReasonV1 compatibilityReason,
                const bool runtimeOperational)
        {
            if (requestedAuthorityFlags == 0) {
                return PaperNativePoseApplicationResultV1::Inactive;
            }
            if (compatibilityReason !=
                PaperNativePoseCompatibilityReasonV1::None) {
                return PaperNativePoseApplicationResultV1::
                    CompatibilityRejected;
            }
            if (!runtimeOperational) {
                return PaperNativePoseApplicationResultV1::RuntimeUnavailable;
            }
            if (!debug.frameCapturePrepared) {
                return PaperNativePoseApplicationResultV1::WaitingForCapture;
            }
            const bool applicationFailed =
                (debug.beforeRockApplicationAttempted &&
                    !debug.beforeRockApplicationSucceeded) ||
                (debug.afterRockApplicationAttempted &&
                    !debug.afterRockApplicationSucceeded);
            if (applicationFailed) {
                return PaperNativePoseApplicationResultV1::ApplyFailed;
            }
            const bool applied = debug.weaponFixedHandsExpected ?
                debug.afterRockApplicationSucceeded :
                debug.beforeRockApplicationSucceeded &&
                    debug.afterRockApplicationSucceeded;
            return applied ?
                PaperNativePoseApplicationResultV1::Applied :
                PaperNativePoseApplicationResultV1::WaitingForCapture;
        }

        void copyNativeHand(
            const native_animation_authority::DebugHandSnapshot& debug,
            const PaperHandV1 hand,
            const std::uint64_t frameIndex,
            const std::uint64_t captureSequence,
            const std::uint32_t worldGeneration,
            const std::uint32_t skeletonGeneration,
            const std::uint32_t rockProviderGeneration,
            const std::uint32_t paperProviderGeneration,
            PaperNativeHandSolutionV1& solution)
        {
            solution.hand = hand;
            solution.role = hand == PaperHandV1::Left ?
                PaperNativeHandRoleV1::Support :
                PaperNativeHandRoleV1::Primary;
            solution.flags = bit(PaperNativeHandSolutionFlagV1::Valid);
            solution.frameIndex = frameIndex;
            solution.captureSequence = captureSequence;
            solution.worldGeneration = worldGeneration;
            solution.skeletonGeneration = skeletonGeneration;
            solution.rockProviderGeneration = rockProviderGeneration;
            solution.paperProviderGeneration = paperProviderGeneration;
            solution.motionTranslationGameUnits =
                debug.motionTranslationGameUnits;
            solution.motionRotationDegrees = debug.motionRotationDegrees;

            if (debug.nativeHandValid) {
                api_transform::fromNi(
                    debug.nativeHandInWeapon,
                    solution.nativeHandInWeapon);
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::
                        NativeHandInWeaponValid);
            }
            if (debug.nativeBaselineValid) {
                api_transform::fromNi(
                    debug.nativeBaselineHandInWeapon,
                    solution.nativeBaselineHandInWeapon);
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::
                        NativeBaselineHandInWeaponValid);
            }
            if (debug.liveBaselineValid) {
                api_transform::fromNi(
                    debug.liveBaselineHandInWeapon,
                    solution.rockBaselineHandInWeapon);
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::
                        RockBaselineHandInWeaponValid);
            }
            if (debug.resolvedHandInWeaponValid) {
                api_transform::fromNi(
                    debug.resolvedHandInWeapon,
                    solution.resolvedTargetInWeapon);
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::
                        ResolvedTargetInWeaponValid);
            }
            if (debug.resolvedHandWorldValid) {
                api_transform::fromNi(
                    debug.resolvedHandWorld,
                    solution.resolvedTargetWorld);
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::
                        ResolvedTargetWorldValid);
            }
            if (debug.nativeFingerLocalMask != 0) {
                solution.nativeFingerLocalTransformMask =
                    debug.nativeFingerLocalMask;
                for (std::size_t index = 0;
                     index < native_animation_authority::kFingerTransformCount;
                     ++index) {
                    api_transform::fromNi(
                        debug.nativeFingerLocals[index],
                        solution.nativeFingerLocalTransforms[index]);
                }
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::NativeFingerLocalsValid);
            }
            if (debug.motionGateApplicable) {
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::MotionGateApplicable);
            }
            if (debug.motionQualified) {
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::MotionQualified);
            }
            if (debug.visualAuthorityPublished) {
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::TargetPublished);
            } else if (debug.motionGateApplicable &&
                       !debug.motionQualified &&
                       debug.resolvedHandWorldValid) {
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::TargetSuppressed);
            }
            switch (debug.targetMode) {
            case native_animation_authority::DebugHandTargetMode::
                LiveGripDelta:
                solution.targetMode =
                    PaperNativeHandTargetModeV1::LiveGripDelta;
                break;
            case native_animation_authority::DebugHandTargetMode::
                NativeWeaponRelative:
                solution.targetMode =
                    PaperNativeHandTargetModeV1::NativeWeaponRelative;
                break;
            case native_animation_authority::DebugHandTargetMode::None:
            default:
                solution.targetMode = PaperNativeHandTargetModeV1::None;
                break;
            }
        }

        [[nodiscard]] bool addPresentedPose(
            const rock::api::core::AnimationPhaseContextV1& context,
            PaperNativeHandSolutionV1& solution)
        {
            const auto rockHand = solution.hand == PaperHandV1::Left ?
                rock::api::Hand::Left :
                rock::api::Hand::Right;
            rock::api::hands::PresentedHandPoseV1 presented{};
            if (!rockApiClient().queryPresentedHandPose(rockHand, presented) ||
                !hasPresentedFlag(
                    presented.flags,
                    rock::api::hands::PresentedHandPoseFlagV1::Valid) ||
                !hasPresentedFlag(
                    presented.flags,
                    rock::api::hands::PresentedHandPoseFlagV1::HandWorldValid) ||
                presented.frameIndex != context.frameIndex ||
                presented.worldGeneration != context.worldGeneration ||
                presented.skeletonGeneration != context.skeletonGeneration ||
                presented.providerGeneration != context.providerGeneration) {
                return false;
            }

            api_transform::fromRock(
                presented.handWorld,
                solution.presentedHandWorld);
            solution.presentationSequence = presented.presentationSequence;
            solution.flags |=
                bit(PaperNativeHandSolutionFlagV1::PresentedHandWorldValid) |
                bit(PaperNativeHandSolutionFlagV1::PresentedPoseCoherent);
            if (hasPresentedFlag(
                    presented.flags,
                    rock::api::hands::PresentedHandPoseFlagV1::
                            FingerLocalsValid) &&
                presented.fingerLocalTransformMask != 0) {
                solution.presentedFingerLocalTransformMask =
                    presented.fingerLocalTransformMask;
                for (std::size_t index = 0;
                     index < PAPER_FINGER_TRANSFORM_COUNT_V1;
                     ++index) {
                    api_transform::fromRock(
                        presented.fingerLocalTransforms[index],
                        solution.presentedFingerLocalTransforms[index]);
                }
                solution.flags |= bit(
                    PaperNativeHandSolutionFlagV1::
                        PresentedFingerLocalsValid);
            }

            if ((solution.flags & bit(
                    PaperNativeHandSolutionFlagV1::
                        ResolvedTargetWorldValid)) != 0) {
                const auto residual = native_pose_pipeline_policy::
                    measurePoseResidual(
                        solution.resolvedTargetWorld,
                        solution.presentedHandWorld);
                if (residual.valid) {
                    solution.residualTranslationGameUnits =
                        residual.translationGameUnits;
                    solution.residualRotationDegrees =
                        residual.rotationDegrees;
                    solution.flags |= bit(
                        PaperNativeHandSolutionFlagV1::ResidualValid);
                }
            }
            return true;
        }
    }

    void publishFrame(
        const rock::api::core::AnimationPhaseContextV1& context,
        const native_animation_authority_policy::
            NativeAnimationCompatibilityReason compatibilityReason,
        const std::uint32_t localAuthorityFlags,
        const std::uint32_t consumerAuthorityFlags,
        const std::uint32_t weaponFormId,
        const std::uint64_t weaponGenerationKey,
        const bool runtimeOperational)
    {
        native_animation_authority::DebugAuthoritySnapshot debug{};
        if (!native_animation_authority::queryDebugAuthoritySnapshot(debug)) {
            provider::clearNativePosePipeline();
            return;
        }

        const std::uint32_t requestedAuthorityFlags =
            localAuthorityFlags | consumerAuthorityFlags;
        const auto publicReason =
            publicCompatibilityReason(compatibilityReason);
        PaperNativePoseFrameStateV1 frame{};
        frame.flags = bit(PaperNativePoseFrameFlagV1::Valid);
        frame.mode = resolveMode(
            debug,
            localAuthorityFlags,
            consumerAuthorityFlags);
        frame.compatibilityReason = publicReason;
        frame.applicationResult = resolveApplicationResult(
            debug,
            requestedAuthorityFlags,
            publicReason,
            runtimeOperational);
        frame.activeAuthorityFlags = debug.runtime.effectiveFlags;
        frame.localAuthorityFlags = localAuthorityFlags;
        frame.consumerAuthorityFlags = consumerAuthorityFlags;
        frame.weaponFormId = weaponFormId;
        frame.weaponGenerationKey = weaponGenerationKey;
        frame.frameIndex = context.frameIndex;
        frame.captureSequence = debug.frameCaptureSequence;
        frame.worldGeneration = context.worldGeneration;
        frame.skeletonGeneration = context.skeletonGeneration;
        frame.rockProviderGeneration = context.providerGeneration;
        frame.paperProviderGeneration = provider::generation();

        if (requestedAuthorityFlags != 0) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::AuthorityRequested);
        }
        if (runtimeOperational) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::RuntimeOperational);
        }
        if (debug.frameCapturePrepared) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::CapturePrepared);
        }
        if (debug.beforeRockApplicationSucceeded) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::BeforeRockApplied);
        }
        if (debug.afterRockApplicationSucceeded) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::AfterRockApplied);
        }
        if ((debug.runtime.effectiveFlags &
                native_animation_authority_policy::kWeapon) != 0) {
            frame.flags |= bit(PaperNativePoseFrameFlagV1::FullPose);
        }
        if (debug.weaponFixedHandsExpected) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::WeaponFixedHands);
        }
        if (debug.partialReloadExpected) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::PartialReload);
        }
        if (debug.runtime.localManualCycleLeaseActive) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::ManualCycle);
        }
        if (publicReason != PaperNativePoseCompatibilityReasonV1::None &&
            requestedAuthorityFlags != 0) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::CompatibilityRejected);
        }
        if (frame.applicationResult ==
            PaperNativePoseApplicationResultV1::ApplyFailed) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::ApplicationFailed);
        }
        if (debug.controllerWeaponValid) {
            api_transform::fromNi(
                debug.controllerWeaponWorld,
                frame.rockWeaponWorld);
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::RockWeaponWorldValid);
        }
        if (debug.nativeBaselineWeaponValid) {
            api_transform::fromNi(
                debug.nativeBaselineWeaponWorld,
                frame.nativeWeaponBaselineWorld);
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::
                    NativeWeaponBaselineWorldValid);
        }
        if (debug.desiredWeaponValid) {
            api_transform::fromNi(
                debug.desiredWeaponWorld,
                frame.resolvedWeaponWorld);
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::ResolvedWeaponWorldValid);
        }

        PaperNativeHandSolutionV1 rightHand{};
        PaperNativeHandSolutionV1 leftHand{};
        copyNativeHand(
            debug.rightHand,
            PaperHandV1::Right,
            context.frameIndex,
            debug.frameCaptureSequence,
            context.worldGeneration,
            context.skeletonGeneration,
            context.providerGeneration,
            provider::generation(),
            rightHand);
        copyNativeHand(
            debug.leftHand,
            PaperHandV1::Left,
            context.frameIndex,
            debug.frameCaptureSequence,
            context.worldGeneration,
            context.skeletonGeneration,
            context.providerGeneration,
            provider::generation(),
            leftHand);
        const bool rightPresented = addPresentedPose(context, rightHand);
        const bool leftPresented = addPresentedPose(context, leftHand);
        if (rightPresented || leftPresented) {
            frame.flags |= bit(
                PaperNativePoseFrameFlagV1::PresentedReadbackAvailable);
        }
        provider::publishNativePosePipeline(frame, rightHand, leftHand);
    }
}
