#pragma once

#include <cstddef>
#include <cstdint>

namespace paper::tactical_reload_bridge_policy
{
    inline constexpr std::size_t kActorStateStorageOffset = 0x0C;
    inline constexpr std::uint32_t kWeaponStateShift = 2;
    inline constexpr std::uint32_t kWeaponStateMask = 0x7;
    inline constexpr std::uint32_t kGunStateShift = 15;
    inline constexpr std::uint32_t kGunStateMask = 0xF;
    inline constexpr std::uint32_t kWeaponStateDrawn = 3;
    inline constexpr std::uint32_t kGunStateReloading = 4;
    inline constexpr float kMaximumFrameDeltaSeconds = 0.25f;
    inline constexpr float kPressHoldWatchdogSeconds = 15.0f;
    inline constexpr float kReloadStartWatchdogSeconds = 1.0f;
    inline constexpr float kReloadActiveWatchdogSeconds = 30.0f;

    enum class Phase : std::uint8_t
    {
        Idle,
        PressArmed,
        AwaitingReloadStart,
        ReloadActive,
    };

    enum class StepReason : std::uint8_t
    {
        None,
        ManualIntentArmed,
        ButtonReleased,
        ReloadStarted,
        ReloadEnded,
        PressHoldExpired,
        ReloadStartExpired,
        ReloadActiveExpired,
        RuntimeDisabled,
        SessionReset,
    };

    struct State
    {
        Phase phase{ Phase::Idle };
        float elapsedSeconds{ 0.0f };
        std::uint64_t transactionSequence{ 0 };
        bool keywordPresentAtArm{ false };
        bool keywordAddedByPaper{ false };
        bool transactionOwnsKeywordCleanup{ false };
    };

    struct Step
    {
        State state{};
        StepReason reason{ StepReason::None };
        bool addKeyword{ false };
        bool removeKeyword{ false };
    };

    [[nodiscard]] inline constexpr std::uint32_t decodeWeaponState(
        const std::uint32_t actorStateStorage) noexcept
    {
        return (actorStateStorage >> kWeaponStateShift) &
               kWeaponStateMask;
    }

    [[nodiscard]] inline constexpr std::uint32_t decodeGunState(
        const std::uint32_t actorStateStorage) noexcept
    {
        return (actorStateStorage >> kGunStateShift) &
               kGunStateMask;
    }

    [[nodiscard]] inline constexpr bool isWeaponReadyForManualReload(
        const std::uint32_t weaponState) noexcept
    {
        return weaponState == kWeaponStateDrawn;
    }

    [[nodiscard]] inline constexpr bool isReloading(
        const std::uint32_t gunState) noexcept
    {
        return gunState == kGunStateReloading;
    }

    [[nodiscard]] inline constexpr bool isJustPressed(
        const float value,
        const float heldDownSeconds) noexcept
    {
        return value == value && heldDownSeconds == heldDownSeconds &&
               value != 0.0f && heldDownSeconds == 0.0f;
    }

    [[nodiscard]] inline constexpr bool isReleased(
        const float value,
        const float heldDownSeconds) noexcept
    {
        return value == 0.0f && heldDownSeconds == heldDownSeconds &&
               heldDownSeconds >= 0.0f;
    }

    [[nodiscard]] inline constexpr Step armManualIntent(
        const State current,
        const bool weaponReady,
        const bool keywordAlreadyPresent) noexcept
    {
        if (!weaponReady || current.phase == Phase::ReloadActive ||
            current.phase == Phase::PressArmed) {
            return { current };
        }

        auto next = current;
        next.phase = Phase::PressArmed;
        next.elapsedSeconds = 0.0f;
        ++next.transactionSequence;
        next.keywordPresentAtArm = keywordAlreadyPresent;
        next.keywordAddedByPaper = !keywordAlreadyPresent;

        // Session initialization establishes a clean baseline. A keyword
        // observed on this exact accepted input edge is therefore either the
        // value PAPER adds or the shipped Papyrus callback's duplicate value.
        // In both cases this bounded transaction owns its eventual cleanup.
        next.transactionOwnsKeywordCleanup = true;
        return {
            .state = next,
            .reason = StepReason::ManualIntentArmed,
            .addKeyword = !keywordAlreadyPresent,
        };
    }

    [[nodiscard]] inline constexpr Step releaseManualIntent(
        const State current) noexcept
    {
        if (current.phase != Phase::PressArmed) {
            return { current };
        }

        auto next = current;
        next.phase = Phase::AwaitingReloadStart;
        next.elapsedSeconds = 0.0f;
        return {
            .state = next,
            .reason = StepReason::ButtonReleased,
        };
    }

    [[nodiscard]] inline constexpr Step observeReloadStart(
        const State current) noexcept
    {
        auto next = current;
        next.phase = Phase::ReloadActive;
        next.elapsedSeconds = 0.0f;
        next.keywordPresentAtArm = false;
        next.keywordAddedByPaper = false;
        next.transactionOwnsKeywordCleanup = false;
        return {
            .state = next,
            .reason = StepReason::ReloadStarted,
            .removeKeyword = current.transactionOwnsKeywordCleanup,
        };
    }

    [[nodiscard]] inline constexpr Step observeReloadEnd(
        const State current) noexcept
    {
        auto next = current;
        next.phase = Phase::Idle;
        next.elapsedSeconds = 0.0f;
        next.keywordPresentAtArm = false;
        next.keywordAddedByPaper = false;
        next.transactionOwnsKeywordCleanup = false;
        return {
            .state = next,
            .reason = StepReason::ReloadEnded,
            .removeKeyword = current.transactionOwnsKeywordCleanup,
        };
    }

    [[nodiscard]] inline constexpr Step reset(
        const State current,
        const StepReason reason) noexcept
    {
        auto next = current;
        next.phase = Phase::Idle;
        next.elapsedSeconds = 0.0f;
        next.keywordPresentAtArm = false;
        next.keywordAddedByPaper = false;
        next.transactionOwnsKeywordCleanup = false;
        return {
            .state = next,
            .reason = reason,
            .removeKeyword = current.transactionOwnsKeywordCleanup,
        };
    }

    [[nodiscard]] inline constexpr Step advance(
        const State current,
        const float deltaSeconds,
        const bool runtimeEnabled) noexcept
    {
        if (!runtimeEnabled) {
            return reset(current, StepReason::RuntimeDisabled);
        }
        if (current.phase == Phase::Idle) {
            return { current };
        }

        const float boundedDelta =
            deltaSeconds > 0.0f ?
            (deltaSeconds < kMaximumFrameDeltaSeconds ?
                    deltaSeconds :
                    kMaximumFrameDeltaSeconds) :
            0.0f;
        auto next = current;
        next.elapsedSeconds += boundedDelta;

        switch (current.phase) {
        case Phase::PressArmed:
            if (next.elapsedSeconds >= kPressHoldWatchdogSeconds) {
                return reset(next, StepReason::PressHoldExpired);
            }
            break;
        case Phase::AwaitingReloadStart:
            if (next.elapsedSeconds >= kReloadStartWatchdogSeconds) {
                return reset(next, StepReason::ReloadStartExpired);
            }
            break;
        case Phase::ReloadActive:
            if (next.elapsedSeconds >= kReloadActiveWatchdogSeconds) {
                return reset(next, StepReason::ReloadActiveExpired);
            }
            break;
        case Phase::Idle:
            break;
        }
        return {
            .state = next,
        };
    }

    [[nodiscard]] inline constexpr const char* stepReasonName(
        const StepReason reason) noexcept
    {
        switch (reason) {
        case StepReason::None:
            return "none";
        case StepReason::ManualIntentArmed:
            return "manual-intent-armed";
        case StepReason::ButtonReleased:
            return "button-released";
        case StepReason::ReloadStarted:
            return "reload-started";
        case StepReason::ReloadEnded:
            return "reload-ended";
        case StepReason::PressHoldExpired:
            return "press-hold-watchdog";
        case StepReason::ReloadStartExpired:
            return "reload-start-watchdog";
        case StepReason::ReloadActiveExpired:
            return "reload-active-watchdog";
        case StepReason::RuntimeDisabled:
            return "runtime-disabled";
        case StepReason::SessionReset:
            return "session-reset";
        }
        return "unknown";
    }
}
