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
    inline constexpr float kManualCycleHandMotionTranslationThresholdGameUnits = 1.5f;
    inline constexpr float kManualCycleHandMotionRotationThresholdDegrees = 10.0f;
    inline constexpr float kAuthoredSupportGripTranslationToleranceGameUnits = 0.05f;
    inline constexpr float kAuthoredSupportGripRotationToleranceDegrees = 0.5f;
    inline constexpr float kAuthoredSupportGripScaleTolerance = 0.001f;

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

    struct LocalReloadAuthoritySelection
    {
        bool leaseActive{ false };
        bool partialAuthorityEnabled{ false };
    };

    /*
     * Full authority preserves the existing arms/hands/Weapon composition.
     * Partial reload authority excludes Weapon regardless of support-grip
     * topology. The post-ROCK publication path restores the visible Weapon to
     * its controller-owned world after moving either hand, so pistols and
     * one-hand reloads do not need the manual-cycle two-hand solver gate.
     */
    [[nodiscard]] inline constexpr std::uint32_t resolveLocalReloadAuthorityFlags(
        const LocalReloadAuthoritySelection& selection)
    {
        if (!selection.leaseActive) {
            return 0;
        }
        if (!selection.partialAuthorityEnabled) {
            return kReloadPose;
        }
        return kWeaponFixedHandsPose;
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

    /*
     * Bolt/lever/revolver hammer motion remains rebased onto each exact live
     * ROCK grip. During a partial reload, however, the native support hand
     * must target its authored Weapon-relative pose directly. Rebasing that
     * hand onto an arbitrary dynamic support grab carries the grab offset all
     * the way to the magazine and bolt nodes.
     */
    [[nodiscard]] inline constexpr WeaponFixedHandTargetMode
        resolveWeaponFixedHandTargetMode(
            const bool partialReload,
            const WeaponFixedHandRole role)
    {
        return partialReload && role == WeaponFixedHandRole::Support ?
            WeaponFixedHandTargetMode::NativeWeaponRelative :
            WeaponFixedHandTargetMode::LiveGripDelta;
    }

    [[nodiscard]] inline constexpr bool shouldPublishWeaponFixedSupportHand(
        const bool partialReload,
        const bool authoredSupportGripActive)
    {
        return partialReload || authoredSupportGripActive;
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

    struct ManualCycleWeaponEligibility
    {
        bool boltAction{ false };
        bool revolverAnimation{ false };
        bool shotgun{ false };
        bool rifle{ false };
        bool manualCycleAnimationKeyword{ false };
    };

    [[nodiscard]] inline constexpr bool isManualCycleFireAnimationAllowed(
        const ManualCycleWeaponEligibility& eligibility)
    {
        return eligibility.boltAction ||
               eligibility.revolverAnimation ||
               eligibility.shotgun ||
               eligibility.rifle ||
               eligibility.manualCycleAnimationKeyword;
    }

    struct ManualCycleHandAnimationEligibility
    {
        bool gripStateValid{ false };
        bool firingHandIsLeft{ false };
    };

    [[nodiscard]] inline constexpr bool canApplyManualCycleHandAnimation(
        const ManualCycleHandAnimationEligibility& eligibility)
    {
        return eligibility.gripStateValid &&
               !eligibility.firingHandIsLeft;
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

    struct AuthoredSupportGripMatchSample
    {
        ManualCycleHandMotionSample transformDelta{};
        float scaleDelta{ 0.0f };
        bool supportGripValid{ false };
        bool authoredGripValid{ false };
    };

    [[nodiscard]] inline constexpr bool isAuthoredSupportGripMatch(
        const AuthoredSupportGripMatchSample& sample)
    {
        return sample.supportGripValid &&
               sample.authoredGripValid &&
               sample.transformDelta.translationGameUnits <=
                   kAuthoredSupportGripTranslationToleranceGameUnits &&
               sample.transformDelta.rotationDegrees <=
                   kAuthoredSupportGripRotationToleranceDegrees &&
               sample.scaleDelta <= kAuthoredSupportGripScaleTolerance;
    }

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

    [[nodiscard]] constexpr bool containsIgnoreCase(
        const std::string_view value,
        const std::string_view token)
    {
        if (token.empty()) {
            return true;
        }
        if (token.size() > value.size()) {
            return false;
        }
        for (std::size_t i = 0; i <= value.size() - token.size(); ++i) {
            if (equalsIgnoreCase(value.substr(i, token.size()), token)) {
                return true;
            }
        }
        return false;
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
