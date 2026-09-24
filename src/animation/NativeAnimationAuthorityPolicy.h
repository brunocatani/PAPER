#pragma once

#include <cstdint>
#include <string_view>

namespace paper::native_animation_authority_policy
{
    inline constexpr std::uint32_t kArms = 1u << 0;
    inline constexpr std::uint32_t kHands = 1u << 1;
    inline constexpr std::uint32_t kWeapon = 1u << 2;
    inline constexpr std::uint32_t kWeaponFixedHandsPose = kArms | kHands;
    inline constexpr std::uint32_t kManualCyclePose = kWeaponFixedHandsPose;
    inline constexpr std::uint32_t kReloadPose = kArms | kHands | kWeapon;
    inline constexpr float kManualCycleHandMotionTranslationThresholdGameUnits = 0.75f;
    inline constexpr float kManualCycleHandMotionRotationThresholdDegrees = 5.0f;

    struct ClimbingAnimationSuppressionObservation
    {
        bool rightStateValid{ false };
        bool leftStateValid{ false };
        bool rightFixedSurfaceLatch{ false };
        bool leftFixedSurfaceLatch{ false };
    };

    /*
     * A fixed-surface latch owns the physical hand while the player climbs.
     * Missing ROCK hand state cannot prove that the latch ended, so animation
     * authority remains suppressed until both hand snapshots are valid.
     */
    [[nodiscard]] inline constexpr bool shouldSuppressAnimationForClimbing(
        const ClimbingAnimationSuppressionObservation& observation)
    {
        return !observation.rightStateValid ||
               !observation.leftStateValid ||
               observation.rightFixedSurfaceLatch ||
               observation.leftFixedSurfaceLatch;
    }

    enum class LocalReloadLeaseEndReason : std::uint32_t
    {
        None = 0,
        ReloadEnded,
        WatchdogExpired,
    };

    struct LocalReloadLifecycleSignal
    {
        std::uint64_t startSequence{ 0 };
        std::uint64_t endSequence{ 0 };
        bool reloadActive{ false };
    };

    struct LocalReloadLeaseState
    {
        std::uint32_t watchdogFramesRemaining{ 0 };
        std::uint64_t startSequenceAtArm{ 0 };
        std::uint64_t endSequenceAtArm{ 0 };
        bool observedReloadStart{ false };
    };

    struct LocalReloadLeaseStep
    {
        LocalReloadLeaseState state{};
        LocalReloadLeaseEndReason endReason{ LocalReloadLeaseEndReason::None };

        [[nodiscard]] constexpr bool active() const
        {
            return endReason == LocalReloadLeaseEndReason::None && state.watchdogFramesRemaining > 0;
        }
    };

    /*
     * Local reload authority always excludes Weapon regardless of support-grip
     * topology. The post-ROCK publication path restores the visible Weapon to
     * its controller-owned world after moving either hand. Reloads and manual
     * cycles do not require a two-hand grip.
     */
    [[nodiscard]] inline constexpr std::uint32_t resolveLocalReloadAuthorityFlags(
        const bool leaseActive)
    {
        return leaseActive ? kWeaponFixedHandsPose : 0;
    }

    enum class WeaponFixedHandRole : std::uint8_t
    {
        Primary,
        Support,
    };

    enum class WeaponFixedHandTargetMode : std::uint8_t
    {
        LiveGripDelta,
        NativeWeaponRelative,
    };

    // Without a calibrated firing seat, the native trajectory is already in
    // Weapon space. A controller or presented wrist is never a model origin.
    template <class Transform>
    [[nodiscard]] constexpr Transform resolvePrimaryAnimationBaseline(
        const bool canonicalValid, const Transform& canonical,
        const Transform& nativeAtEntry)
    {
        return canonicalValid ? canonical : nativeAtEntry;
    }

    // The vanilla SMG's model registration is absent from native animation
    // coordinates. Its stationary primary baseline provides the translation
    // into ROCK's corrected model frame. Do not use an arbitrary support grab
    // as that origin, and do not rotate or scale the authored reload trajectory.
    template <class Transform>
    [[nodiscard]] constexpr Transform resolveNativeWeaponRelativeHand(
        const std::uint32_t formId, const Transform& animatedHand,
        const Transform& livePrimaryBaseline, const Transform& nativePrimaryBaseline)
    {
        auto result = animatedHand;
        if (formId == 0x0015B043) {
            result.translate.x += livePrimaryBaseline.translate.x - nativePrimaryBaseline.translate.x;
            result.translate.y += livePrimaryBaseline.translate.y - nativePrimaryBaseline.translate.y;
            result.translate.z += livePrimaryBaseline.translate.z - nativePrimaryBaseline.translate.z;
        }
        return result;
    }

    /*
     * Fire-triggered motion retains an existing authored ROCK grip as its
     * baseline. A free or dynamically grabbed support hand instead follows
     * the native Weapon-relative trajectory, as it does during a reload.
     * Applying animation deltas at an arbitrary controller/grab position would
     * carry that offset to the bolt instead of reaching the weapon.
     */
    [[nodiscard]] inline constexpr WeaponFixedHandTargetMode
        resolveWeaponFixedHandTargetMode(
            const bool partialReload,
            const WeaponFixedHandRole role,
            const bool authoredSupportGripActive)
    {
        return role == WeaponFixedHandRole::Support &&
                       (partialReload || !authoredSupportGripActive) ?
            WeaponFixedHandTargetMode::NativeWeaponRelative :
            WeaponFixedHandTargetMode::LiveGripDelta;
    }

    enum class LocalManualCycleLeaseEndReason : std::uint32_t
    {
        None = 0,
        CycleBracketEnded,
        ReloadStarted,
        WatchdogExpired,
    };

    struct LocalManualCycleLifecycleSignal
    {
        std::uint64_t reloadStartSequence{ 0 };
        std::uint64_t reloadEndSequence{ 0 };
        float deltaSeconds{ 0.0f };
    };

    struct LocalManualCycleLeaseState
    {
        float watchdogSecondsRemaining{ 0.0f };
        std::uint64_t reloadStartSequenceAtArm{ 0 };
        std::uint64_t lastReloadEndSequence{ 0 };
        std::uint32_t observedReloadEndEvents{ 0 };
    };

    struct LocalManualCycleLeaseStep
    {
        LocalManualCycleLeaseState state{};
        LocalManualCycleLeaseEndReason endReason{ LocalManualCycleLeaseEndReason::None };

        [[nodiscard]] constexpr bool active() const
        {
            return endReason == LocalManualCycleLeaseEndReason::None &&
                   state.watchdogSecondsRemaining > 0.0f;
        }
    };

    enum class NativeAnimationCompatibilityReason : std::uint8_t
    {
        None = 0,
        HandlingStateUnavailable,
        LeftFiringHand,
        PartCarry,
        WeaponUnavailable,
        WeaponIdentityChanged,
        AnimationRequestResetRequired,
        Akimbo,
    };

    struct NativeAnimationCompatibilityObservation
    {
        std::uint64_t weaponGenerationKey{ 0 };
        // Scene identity remains available while ROCK builds collision.
        std::uintptr_t weaponNode{ 0 };
        std::uint32_t weaponFormId{ 0 };
        bool handlingStateValid{ false };
        bool gripStateValid{ false };
        bool firingHandIsLeft{ false };
        bool partCarryActive{ false };
        bool weaponPresent{ false };
        bool weaponIdentityCoherent{ false };
        bool akimboActive{ false };
    };

    struct NativeAnimationCompatibilityState
    {
        std::uint64_t weaponGenerationKey{ 0 };
        std::uintptr_t weaponNode{ 0 };
        std::uint32_t weaponFormId{ 0 };
        bool weaponBound{ false };
        bool animationRequestResetRequired{ false };
    };

    struct NativeAnimationCompatibilityStep
    {
        NativeAnimationCompatibilityState state{};
        NativeAnimationCompatibilityReason reason{
            NativeAnimationCompatibilityReason::None
        };

        [[nodiscard]] constexpr bool compatible() const
        {
            return reason == NativeAnimationCompatibilityReason::None;
        }
    };

    [[nodiscard]] inline constexpr std::string_view
        nativeAnimationCompatibilityReasonName(
            const NativeAnimationCompatibilityReason reason)
    {
        switch (reason) {
        case NativeAnimationCompatibilityReason::Akimbo:
            return "akimbo";
        case NativeAnimationCompatibilityReason::HandlingStateUnavailable:
            return "handling-state-unavailable";
        case NativeAnimationCompatibilityReason::LeftFiringHand:
            return "left-firing-hand";
        case NativeAnimationCompatibilityReason::PartCarry:
            return "part-carry";
        case NativeAnimationCompatibilityReason::WeaponUnavailable:
            return "weapon-unavailable";
        case NativeAnimationCompatibilityReason::WeaponIdentityChanged:
            return "weapon-identity-changed";
        case NativeAnimationCompatibilityReason::
            AnimationRequestResetRequired:
            return "animation-request-reset-required";
        case NativeAnimationCompatibilityReason::None:
        default:
            return "compatible";
        }
    }

    /*
     * PAPER's captured graph topology is Bethesda's physical-right primary
     * arm and physical-left support arm. ROCK's handling snapshot is the
     * source of truth for physical topology, independent of which addon (if
     * any) currently owns the rolling handling lease.
     *
     * Active local or consumer animation authority is additionally bound to
     * one concrete equipped weapon. Its scene node can bind the first shot
     * before collision is ready; the first nonzero generation is adopted only
     * for that same node and form. A physical drop, stash,
     * unequip, replacement, or collision-generation change cancels that
     * session instead of allowing a stale captured pose to attach to a
     * different weapon. The request must clear before a new session can bind.
     * With no animation request, an absent weapon is normal and does not
     * disable PAPER's hooks for the next equip.
     */
    [[nodiscard]] inline constexpr NativeAnimationCompatibilityStep
        advanceNativeAnimationCompatibility(
            NativeAnimationCompatibilityState state,
            const NativeAnimationCompatibilityObservation& observation,
            const bool animationAuthorityRequested)
    {
        const NativeAnimationCompatibilityState canceledState{
            .animationRequestResetRequired =
                animationAuthorityRequested,
        };
        if (!observation.handlingStateValid) {
            return {
                canceledState,
                NativeAnimationCompatibilityReason::
                    HandlingStateUnavailable,
            };
        }
        if (observation.akimboActive) {
            return { canceledState, NativeAnimationCompatibilityReason::Akimbo };
        }
        if (observation.firingHandIsLeft) {
            return {
                canceledState,
                NativeAnimationCompatibilityReason::LeftFiringHand,
            };
        }
        if (observation.partCarryActive) {
            return {
                canceledState,
                NativeAnimationCompatibilityReason::PartCarry,
            };
        }
        if (state.animationRequestResetRequired) {
            if (animationAuthorityRequested) {
                return {
                    state,
                    NativeAnimationCompatibilityReason::
                        AnimationRequestResetRequired,
                };
            }
            state = {};
        }
        if (!animationAuthorityRequested) {
            return {};
        }
        if (!observation.weaponPresent ||
            observation.weaponFormId == 0 ||
            (observation.weaponGenerationKey == 0 && observation.weaponNode == 0)) {
            return {
                canceledState,
                NativeAnimationCompatibilityReason::WeaponUnavailable,
            };
        }
        if (!observation.weaponIdentityCoherent) {
            return {
                canceledState,
                NativeAnimationCompatibilityReason::WeaponIdentityChanged,
            };
        }
        if (!state.weaponBound) {
            state.weaponGenerationKey = observation.weaponGenerationKey;
            state.weaponNode = observation.weaponNode;
            state.weaponFormId = observation.weaponFormId;
            state.weaponBound = true;
            return { state, NativeAnimationCompatibilityReason::None };
        }
        if (state.weaponFormId != observation.weaponFormId ||
            state.weaponNode != observation.weaponNode) {
            return {
                canceledState,
                NativeAnimationCompatibilityReason::WeaponIdentityChanged,
            };
        }
        if (state.weaponGenerationKey == 0 && state.weaponNode != 0) {
            state.weaponGenerationKey = observation.weaponGenerationKey;
        } else if (state.weaponGenerationKey != observation.weaponGenerationKey) {
            return {
                canceledState,
                NativeAnimationCompatibilityReason::WeaponIdentityChanged,
            };
        }
        return { state, NativeAnimationCompatibilityReason::None };
    }

    [[nodiscard]] inline constexpr bool canApplyManualCycleHandAnimation(
        const NativeAnimationCompatibilityObservation& observation)
    {
        return observation.handlingStateValid &&
               observation.gripStateValid &&
               observation.weaponPresent &&
               observation.weaponIdentityCoherent &&
               observation.weaponFormId != 0 &&
               (observation.weaponGenerationKey != 0 || observation.weaponNode != 0) &&
               !observation.firingHandIsLeft &&
               !observation.partCarryActive &&
               !observation.akimboActive;
    }

    struct ManualCycleAuthoredSupportGripLatchState
    {
        std::uint64_t weaponGenerationKey{ 0 };
        bool active{ false };
    };

    struct ManualCycleAuthoredSupportGripLatchObservation
    {
        std::uint64_t observedWeaponGenerationKey{ 0 };
        bool leaseActive{ false };
        bool leaseStarted{ false };
        bool authoredSupportGripActive{ false };
        bool activeNonAuthoredGripObserved{ false };
    };

    /*
     * ROCK can clear its support-grip report after Paper publishes the
     * higher-priority hand authority needed for a native manual cycle. Latch
     * only the authored grip observed at that cycle's entry, then retain it
     * across an absent post-ROCK report. A weapon change, lease end, or a
     * positively observed non-authored grip invalidates the latch.
     */
    [[nodiscard]] inline constexpr ManualCycleAuthoredSupportGripLatchState
        advanceManualCycleAuthoredSupportGripLatch(
            const ManualCycleAuthoredSupportGripLatchState state,
            const ManualCycleAuthoredSupportGripLatchObservation& observation)
    {
        if (!observation.leaseActive) {
            return {};
        }
        if (observation.leaseStarted) {
            if (!observation.authoredSupportGripActive ||
                observation.observedWeaponGenerationKey == 0) {
                return {};
            }
            return {
                .weaponGenerationKey =
                    observation.observedWeaponGenerationKey,
                .active = true,
            };
        }
        if (!state.active ||
            observation.observedWeaponGenerationKey == 0 ||
            observation.observedWeaponGenerationKey !=
                state.weaponGenerationKey ||
            observation.activeNonAuthoredGripObserved) {
            return {};
        }
        return state;
    }

    struct ManualCycleHandMotionSample
    {
        float translationGameUnits{ 0.0f };
        float rotationDegrees{ 0.0f };
    };

    [[nodiscard]] inline constexpr bool updateManualCycleHandMotionQualification(
        const bool alreadyQualified,
        const ManualCycleHandMotionSample& sample)
    {
        return alreadyQualified ||
               sample.translationGameUnits >=
                   kManualCycleHandMotionTranslationThresholdGameUnits ||
               sample.rotationDegrees >=
                   kManualCycleHandMotionRotationThresholdDegrees;
    }

    [[nodiscard]] constexpr LocalReloadLeaseStep advanceLocalReloadLease(
        LocalReloadLeaseState state,
        LocalReloadLifecycleSignal signal)
    {
        if (state.watchdogFramesRemaining == 0) {
            return { state, LocalReloadLeaseEndReason::WatchdogExpired };
        }

        --state.watchdogFramesRemaining;
        if (signal.reloadActive || signal.startSequence != state.startSequenceAtArm) {
            state.observedReloadStart = true;
        }
        if (state.observedReloadStart && signal.endSequence != state.endSequenceAtArm) {
            return { state, LocalReloadLeaseEndReason::ReloadEnded };
        }
        if (state.watchdogFramesRemaining == 0) {
            return { state, LocalReloadLeaseEndReason::WatchdogExpired };
        }
        return { state, LocalReloadLeaseEndReason::None };
    }

    /*
     * Bethesda's manual-cycle action is bracketed by two ReloadEnd graph
     * events: one at the start of the bolt/lever or revolver hammer clip and
     * one at its end. A
     * player WeaponFire event arms this state with the event sequence sampled
     * at that exact boundary. A real reload start wins immediately, while the
     * duration derived from the equipped weapon's live animation data remains
     * a bounded fallback for non-conforming replacement clips.
     */
    [[nodiscard]] constexpr LocalManualCycleLeaseStep advanceLocalManualCycleLease(
        LocalManualCycleLeaseState state,
        LocalManualCycleLifecycleSignal signal)
    {
        if (state.watchdogSecondsRemaining <= 0.0f) {
            state.watchdogSecondsRemaining = 0.0f;
            return { state, LocalManualCycleLeaseEndReason::WatchdogExpired };
        }

        if (signal.reloadStartSequence != state.reloadStartSequenceAtArm) {
            state.watchdogSecondsRemaining = 0.0f;
            return { state, LocalManualCycleLeaseEndReason::ReloadStarted };
        }

        if (signal.reloadEndSequence != state.lastReloadEndSequence) {
            const std::uint64_t eventCount =
                signal.reloadEndSequence > state.lastReloadEndSequence ?
                signal.reloadEndSequence - state.lastReloadEndSequence :
                1;
            state.lastReloadEndSequence = signal.reloadEndSequence;
            const std::uint64_t totalEvents =
                static_cast<std::uint64_t>(state.observedReloadEndEvents) + eventCount;
            constexpr std::uint64_t maxEventCount = 0xFFFFFFFFull;
            state.observedReloadEndEvents = totalEvents > maxEventCount ?
                static_cast<std::uint32_t>(maxEventCount) :
                static_cast<std::uint32_t>(totalEvents);
            if (state.observedReloadEndEvents >= 2) {
                state.watchdogSecondsRemaining = 0.0f;
                return { state, LocalManualCycleLeaseEndReason::CycleBracketEnded };
            }
        }

        const float deltaSeconds =
            signal.deltaSeconds > 0.0f && signal.deltaSeconds <= 0.1f ?
            signal.deltaSeconds :
            (1.0f / 90.0f);
        state.watchdogSecondsRemaining -= deltaSeconds;
        if (state.watchdogSecondsRemaining <= 0.0f) {
            state.watchdogSecondsRemaining = 0.0f;
            return { state, LocalManualCycleLeaseEndReason::WatchdogExpired };
        }
        return { state, LocalManualCycleLeaseEndReason::None };
    }

    /*
     * Resolve one rigid world correction for the complete authored pose:
     *
     *   authoredDelta = inverse(authoredBaseline) * authoredCurrent
     *   desiredAnchor = liveControl * authoredDelta
     *   correction = desiredAnchor * inverse(authoredCurrent)
     *
     * Applying correction to every selected hierarchy root keeps the authored
     * arms/hands/weapon relationship intact while the live controller replaces
     * the flat game's mouse-aim frame.
     */
    template <class Transform, class Compose, class Invert>
    [[nodiscard]] constexpr Transform resolveControllerAnchoredPoseCorrection(
        const Transform& liveControl,
        const Transform& authoredBaseline,
        const Transform& authoredCurrent,
        Compose&& compose,
        Invert&& invert)
    {
        const Transform authoredDelta = compose(invert(authoredBaseline), authoredCurrent);
        const Transform desiredAnchor = compose(liveControl, authoredDelta);
        return compose(desiredAnchor, invert(authoredCurrent));
    }

    /*
     * Align an authored pose from any native tree space to one already-resolved
     * world target. The visible first-person weapon supplies that target; the
     * full-body tree must not derive a second target from its hidden Weapon
     * node because that node follows the current FRIK arm pose.
     */
    template <class Transform, class Compose, class Invert>
    [[nodiscard]] constexpr Transform resolvePoseCorrectionToWorldTarget(
        const Transform& worldTarget,
        const Transform& authoredCurrent,
        Compose&& compose,
        Invert&& invert)
    {
        return compose(worldTarget, invert(authoredCurrent));
    }

    /*
     * Recover the animated hand in Weapon space from two graph-local model
     * transforms captured in the same native hierarchy. This relation is the
     * datum needed by hand-only cycle authority: the visible Weapon remains in
     * ROCK's controller world, while hFRIK solves the hand to W * handInWeapon.
     */
    template <class Transform, class Compose, class Invert>
    [[nodiscard]] constexpr Transform resolveNativeHandInWeapon(
        const Transform& nativeWeaponModel,
        const Transform& nativeHandModel,
        Compose&& compose,
        Invert&& invert)
    {
        return compose(invert(nativeWeaponModel), nativeHandModel);
    }

    /*
     * Bethesda's native primary-arm pass runs before hFRIK replaces the
     * weapon basis. Capture the resulting hand in that native weapon frame.
     * The forward composition remains useful for measuring the visible
     * mismatch before the inverse solve is applied.
     */
    template <class Transform, class Compose>
    [[nodiscard]] constexpr Transform resolveNativeHandWorld(
        const Transform& liveWeaponWorld,
        const Transform& authoredHandInWeapon,
        Compose&& compose)
    {
        return compose(liveWeaponWorld, authoredHandInWeapon);
    }

    [[nodiscard]] constexpr char asciiLower(char value)
    {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    }

    [[nodiscard]] constexpr bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size()) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (asciiLower(lhs[i]) != asciiLower(rhs[i])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool startsWithIgnoreCase(std::string_view value, std::string_view prefix)
    {
        return value.size() >= prefix.size() && equalsIgnoreCase(value.substr(0, prefix.size()), prefix);
    }

    /*
     * Only the two arm chains and the two weapon roots are eligible. The hand
     * root participates in both Arms and Hands so either partial request has a
     * stable hierarchy boundary. Finger/thumb descendants belong to Hands;
     * no root, COM, spine, head, or leg transform can enter this authority.
     */
    [[nodiscard]] constexpr std::uint32_t classifyBone(std::string_view name)
    {
        if (equalsIgnoreCase(name, "Weapon") || equalsIgnoreCase(name, "WeaponLeft")) {
            return kWeapon;
        }

        constexpr std::string_view leftArmPrefix = "LArm_";
        constexpr std::string_view rightArmPrefix = "RArm_";
        std::string_view suffix;
        if (startsWithIgnoreCase(name, leftArmPrefix)) {
            suffix = name.substr(leftArmPrefix.size());
        } else if (startsWithIgnoreCase(name, rightArmPrefix)) {
            suffix = name.substr(rightArmPrefix.size());
        } else {
            return 0;
        }

        if (equalsIgnoreCase(suffix, "Hand")) {
            return kArms | kHands;
        }
        if (startsWithIgnoreCase(suffix, "Finger") || startsWithIgnoreCase(suffix, "Thumb")) {
            return kHands;
        }
        return kArms;
    }

    [[nodiscard]] constexpr bool isRequested(std::uint32_t boneFlags, std::uint32_t requestedFlags)
    {
        return (boneFlags & requestedFlags & kReloadPose) != 0;
    }
}
