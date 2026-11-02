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
            const NativePoseFrameFlag flag)
        {
            return static_cast<std::uint32_t>(flag);
        }

        [[nodiscard]] constexpr std::uint32_t bit(
            const NativeHandSolutionFlag flag)
        {
            return static_cast<std::uint32_t>(flag);
        }

        [[nodiscard]] constexpr bool hasPresentedFlag(
            const std::uint32_t flags,
            const rock::api::hands::PresentedHandPoseFlag flag)
        {
            return (flags & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] constexpr NativePoseCompatibilityReason
            publicCompatibilityReason(
                const native_animation_authority_policy::
                    NativeAnimationCompatibilityReason reason)
        {
            using Internal = native_animation_authority_policy::
                NativeAnimationCompatibilityReason;
            switch (reason) {
            case Internal::HandlingStateUnavailable:
                return NativePoseCompatibilityReason::
                    HandlingStateUnavailable;
            case Internal::LeftFiringHand:
                return NativePoseCompatibilityReason::LeftFiringHand;
            case Internal::PartCarry:
                return NativePoseCompatibilityReason::PartCarry;
            case Internal::WeaponUnavailable:
                return NativePoseCompatibilityReason::WeaponUnavailable;
            case Internal::WeaponIdentityChanged:
                return NativePoseCompatibilityReason::
                    WeaponIdentityChanged;
            case Internal::AnimationRequestResetRequired:
                return NativePoseCompatibilityReason::
                    AnimationRequestResetRequired;
            case Internal::None:
            default:
                return NativePoseCompatibilityReason::None;
            }
        }

        [[nodiscard]] NativePoseMode resolveMode(
            const native_animation_authority::DebugAuthoritySnapshot& debug,
            const std::uint32_t localAuthorityFlags,
            const std::uint32_t consumerAuthorityFlags)
        {
            if ((localAuthorityFlags | consumerAuthorityFlags) == 0) {
                return NativePoseMode::Inactive;
            }
            if (consumerAuthorityFlags != 0 && localAuthorityFlags == 0) {
                return NativePoseMode::ConsumerAuthority;
            }
            if (debug.runtime.localManualCycleLeaseActive) {
                return NativePoseMode::ManualCycle;
            }
            if (debug.partialReloadExpected) {
                return NativePoseMode::PartialReload;
            }
            return NativePoseMode::FullReload;
        }

        [[nodiscard]] NativePoseApplicationResult
            resolveApplicationResult(
                const native_animation_authority::DebugAuthoritySnapshot& debug,
                const std::uint32_t requestedAuthorityFlags,
                const NativePoseCompatibilityReason compatibilityReason,
                const bool runtimeOperational)
        {
            if (requestedAuthorityFlags == 0) {
                return NativePoseApplicationResult::Inactive;
            }
            if (compatibilityReason !=
                NativePoseCompatibilityReason::None) {
                return NativePoseApplicationResult::
                    CompatibilityRejected;
            }
            if (!runtimeOperational) {
                return NativePoseApplicationResult::RuntimeUnavailable;
            }
            if (!debug.frameCapturePrepared) {
                return NativePoseApplicationResult::WaitingForCapture;
            }
            const bool applicationFailed =
                (debug.beforeRockApplicationAttempted &&
                    !debug.beforeRockApplicationSucceeded) ||
                (debug.afterRockApplicationAttempted &&
                    !debug.afterRockApplicationSucceeded);
            if (applicationFailed) {
                return NativePoseApplicationResult::ApplyFailed;
            }
            const bool applied = debug.weaponFixedHandsExpected ?
                debug.afterRockApplicationSucceeded :
                debug.beforeRockApplicationSucceeded &&
                    debug.afterRockApplicationSucceeded;
            return applied ?
                NativePoseApplicationResult::Applied :
                NativePoseApplicationResult::WaitingForCapture;
        }

        void copyNativeHand(
            const native_animation_authority::DebugHandSnapshot& debug,
            const Hand hand,
            const std::uint64_t frameIndex,
            const std::uint64_t captureSequence,
            const std::uint32_t worldGeneration,
            const std::uint32_t skeletonGeneration,
            const std::uint32_t rockProviderGeneration,
            const std::uint32_t paperProviderGeneration,
            NativeHandSolution& solution)
        {
            solution.hand = hand;
            solution.role = hand == Hand::Left ?
                NativeHandRole::Support :
                NativeHandRole::Primary;
            solution.flags = bit(NativeHandSolutionFlag::Valid);
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
                    NativeHandSolutionFlag::
                        NativeHandInWeaponValid);
            }
            if (debug.nativeBaselineValid) {
                api_transform::fromNi(
                    debug.nativeBaselineHandInWeapon,
                    solution.nativeBaselineHandInWeapon);
                solution.flags |= bit(
                    NativeHandSolutionFlag::
                        NativeBaselineHandInWeaponValid);
            }
            if (debug.liveBaselineValid) {
                api_transform::fromNi(
                    debug.liveBaselineHandInWeapon,
                    solution.rockBaselineHandInWeapon);
                solution.flags |= bit(
                    NativeHandSolutionFlag::
                        RockBaselineHandInWeaponValid);
            }
            if (debug.resolvedHandInWeaponValid) {
                api_transform::fromNi(
                    debug.resolvedHandInWeapon,
                    solution.resolvedTargetInWeapon);
                solution.flags |= bit(
                    NativeHandSolutionFlag::
                        ResolvedTargetInWeaponValid);
            }
            if (debug.resolvedHandWorldValid) {
                api_transform::fromNi(
                    debug.resolvedHandWorld,
                    solution.resolvedTargetWorld);
                solution.flags |= bit(
                    NativeHandSolutionFlag::
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
                    NativeHandSolutionFlag::NativeFingerLocalsValid);
            }
            if (debug.motionGateApplicable) {
                solution.flags |= bit(
                    NativeHandSolutionFlag::MotionGateApplicable);
            }
            if (debug.motionQualified) {
                solution.flags |= bit(
                    NativeHandSolutionFlag::MotionQualified);
            }
            if (debug.visualAuthorityPublished) {
                solution.flags |= bit(
                    NativeHandSolutionFlag::TargetPublished);
            } else if (debug.motionGateApplicable &&
                       !debug.motionQualified &&
                       debug.resolvedHandWorldValid) {
                solution.flags |= bit(
                    NativeHandSolutionFlag::TargetSuppressed);
            }
            switch (debug.targetMode) {
            case native_animation_authority::DebugHandTargetMode::
                LiveGripDelta:
                solution.targetMode =
                    NativeHandTargetMode::LiveGripDelta;
                break;
            case native_animation_authority::DebugHandTargetMode::
                NativeWeaponRelative:
                solution.targetMode =
                    NativeHandTargetMode::NativeWeaponRelative;
                break;
            case native_animation_authority::DebugHandTargetMode::None:
            default:
                solution.targetMode = NativeHandTargetMode::None;
                break;
            }
        }

        [[nodiscard]] bool addPresentedPose(
            const rock::api::animation::AnimationPhaseContext& context,
            NativeHandSolution& solution)
        {
            if (context.phase != rock::api::animation::AnimationPhase::Presented) {
                return false;
            }
            const auto rockHand = solution.hand == Hand::Left ?
                rock::api::Hand::Left :
                rock::api::Hand::Right;
            rock::api::hands::PresentedHandPose presented{};
            if (!rockApiClient().queryPresentedHandPose(rockHand, presented) ||
                !hasPresentedFlag(
                    presented.flags,
                    rock::api::hands::PresentedHandPoseFlag::Valid) ||
                !hasPresentedFlag(
                    presented.flags,
                    rock::api::hands::PresentedHandPoseFlag::HandWorldValid) ||
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
                bit(NativeHandSolutionFlag::PresentedHandWorldValid) |
                bit(NativeHandSolutionFlag::PresentedPoseCoherent);
            if (hasPresentedFlag(
                    presented.flags,
                    rock::api::hands::PresentedHandPoseFlag::
                            FingerLocalsValid) &&
                presented.fingerLocalTransformMask != 0) {
                // ROCK's mask is 32-bit; only the 15 finger joints exist.
                solution.presentedFingerLocalTransformMask =
                    static_cast<std::uint16_t>(presented.fingerLocalTransformMask &
                        ((1u << rock::api::hands::kFingerLocalTransformCount) - 1u));
                for (std::size_t index = 0;
                     index < kFingerTransformCount;
                     ++index) {
                    api_transform::fromRock(
                        presented.fingerLocalTransforms[index],
                        solution.presentedFingerLocalTransforms[index]);
                }
                solution.flags |= bit(
                    NativeHandSolutionFlag::
                        PresentedFingerLocalsValid);
            }

            if ((solution.flags & bit(
                    NativeHandSolutionFlag::
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
                        NativeHandSolutionFlag::ResidualValid);
                }
            }
            return true;
        }
    }

    void publishFrame(
        const rock::api::animation::AnimationPhaseContext& context,
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
            paper::provider::clearNativePosePipeline();
            return;
        }

        const std::uint32_t requestedAuthorityFlags =
            localAuthorityFlags | consumerAuthorityFlags;
        const auto publicReason =
            publicCompatibilityReason(compatibilityReason);
        NativePoseFrameState frame{};
        frame.flags = bit(NativePoseFrameFlag::Valid);
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
        frame.paperProviderGeneration = paper::provider::generation();

        if (requestedAuthorityFlags != 0) {
            frame.flags |= bit(
                NativePoseFrameFlag::AuthorityRequested);
        }
        if (runtimeOperational) {
            frame.flags |= bit(
                NativePoseFrameFlag::RuntimeOperational);
        }
        if (debug.frameCapturePrepared) {
            frame.flags |= bit(
                NativePoseFrameFlag::CapturePrepared);
        }
        if (debug.beforeRockApplicationSucceeded) {
            frame.flags |= bit(
                NativePoseFrameFlag::BeforeRockApplied);
        }
        if (debug.afterRockApplicationSucceeded) {
            frame.flags |= bit(
                NativePoseFrameFlag::AfterRockApplied);
        }
        if ((debug.runtime.effectiveFlags &
                native_animation_authority_policy::kWeapon) != 0) {
            frame.flags |= bit(NativePoseFrameFlag::FullPose);
        }
        if (debug.weaponFixedHandsExpected) {
            frame.flags |= bit(
                NativePoseFrameFlag::WeaponFixedHands);
        }
        if (debug.partialReloadExpected) {
            frame.flags |= bit(
                NativePoseFrameFlag::PartialReload);
        }
        if (debug.runtime.localManualCycleLeaseActive) {
            frame.flags |= bit(
                NativePoseFrameFlag::ManualCycle);
        }
        if (publicReason != NativePoseCompatibilityReason::None &&
            requestedAuthorityFlags != 0) {
            frame.flags |= bit(
                NativePoseFrameFlag::CompatibilityRejected);
        }
        if (frame.applicationResult ==
            NativePoseApplicationResult::ApplyFailed) {
            frame.flags |= bit(
                NativePoseFrameFlag::ApplicationFailed);
        }
        if (debug.controllerWeaponValid) {
            api_transform::fromNi(
                debug.controllerWeaponWorld,
                frame.rockWeaponWorld);
            frame.flags |= bit(
                NativePoseFrameFlag::RockWeaponWorldValid);
        }
        if (debug.nativeBaselineWeaponValid) {
            api_transform::fromNi(
                debug.nativeBaselineWeaponWorld,
                frame.nativeWeaponBaselineWorld);
            frame.flags |= bit(
                NativePoseFrameFlag::
                    NativeWeaponBaselineWorldValid);
        }
        if (debug.desiredWeaponValid) {
            api_transform::fromNi(
                debug.desiredWeaponWorld,
                frame.resolvedWeaponWorld);
            frame.flags |= bit(
                NativePoseFrameFlag::ResolvedWeaponWorldValid);
        }

        NativeHandSolution rightHand{};
        NativeHandSolution leftHand{};
        copyNativeHand(
            debug.rightHand,
            Hand::Right,
            context.frameIndex,
            debug.frameCaptureSequence,
            context.worldGeneration,
            context.skeletonGeneration,
            context.providerGeneration,
            paper::provider::generation(),
            rightHand);
        copyNativeHand(
            debug.leftHand,
            Hand::Left,
            context.frameIndex,
            debug.frameCaptureSequence,
            context.worldGeneration,
            context.skeletonGeneration,
            context.providerGeneration,
            paper::provider::generation(),
            leftHand);
        const bool rightPresented = addPresentedPose(context, rightHand);
        const bool leftPresented = addPresentedPose(context, leftHand);
        if (rightPresented || leftPresented) {
            frame.flags |= bit(
                NativePoseFrameFlag::PresentedReadbackAvailable);
        }
        paper::provider::publishNativePosePipeline(frame, rightHand, leftHand);
    }
}
