#pragma once

#include "api/PAPERApi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace paper::reload_stage_policy
{
    inline constexpr float kRestTranslationToleranceGameUnits = 0.05f;
    inline constexpr float kRestRotationToleranceDegrees = 0.25f;
    inline constexpr float kRestScaleTolerance = 0.001f;
    inline constexpr std::uint32_t kFireActivityAdmissionFrames = 6;
    inline constexpr std::size_t kMaximumCorrelatedFireActivities = 8;

    struct TransformDelta
    {
        float translationGameUnits{ 0.0f };
        float rotationDegrees{ 0.0f };
        float scale{ 0.0f };
        bool valid{ false };
    };

    [[nodiscard]] inline TransformDelta calculateTransformDelta(
        const api::PaperTransformV1& baseline,
        const api::PaperTransformV1& current)
    {
        const float dx = current.translate[0] - baseline.translate[0];
        const float dy = current.translate[1] - baseline.translate[1];
        const float dz = current.translate[2] - baseline.translate[2];
        float rotationTrace = 0.0f;
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                rotationTrace += baseline.rotate[row][column] *
                    current.rotate[row][column];
            }
        }
        const float cosine = (std::clamp)(
            (rotationTrace - 1.0f) * 0.5f,
            -1.0f,
            1.0f);
        constexpr float radiansToDegrees =
            57.2957795130823208768f;
        TransformDelta result{
            .translationGameUnits = std::sqrt(dx * dx + dy * dy + dz * dz),
            .rotationDegrees = std::acos(cosine) * radiansToDegrees,
            .scale = std::fabs(current.scale - baseline.scale),
        };
        result.valid =
            std::isfinite(result.translationGameUnits) &&
            std::isfinite(result.rotationDegrees) &&
            std::isfinite(result.scale);
        return result;
    }

    [[nodiscard]] inline bool isAtRest(const TransformDelta& delta)
    {
        return delta.valid &&
               delta.translationGameUnits <=
                   kRestTranslationToleranceGameUnits &&
               delta.rotationDegrees <=
                   kRestRotationToleranceDegrees &&
               delta.scale <= kRestScaleTolerance;
    }

    struct ActivityIdentity
    {
        std::uint64_t activationOrder{ 0 };
    };

    struct FireCorrelationState
    {
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t lastFireSequence{ 0 };
        std::uint64_t highestObservedActivationOrder{ 0 };
        std::array<
            std::uint64_t,
            kMaximumCorrelatedFireActivities>
            correlatedActivationOrders{};
        std::uint32_t correlatedCount{ 0 };
        std::uint32_t admissionFramesRemaining{ 0 };
        bool initialized{ false };
    };

    struct FireCorrelationStep
    {
        FireCorrelationState state{};
        api::PaperReloadFireCorrelationV1 correlation{
            api::PaperReloadFireCorrelationV1::None
        };
        std::uint32_t activeCorrelatedCount{ 0 };
        bool active{ false };
        bool newFireEvent{ false };
    };

    [[nodiscard]] inline bool activityActive(
        const std::span<const ActivityIdentity> activities,
        const std::uint64_t activationOrder)
    {
        return activationOrder != 0 &&
               std::ranges::any_of(
                   activities,
                   [activationOrder](const ActivityIdentity& activity) {
                       return activity.activationOrder == activationOrder;
                   });
    }

    [[nodiscard]] inline FireCorrelationStep advanceFireCorrelation(
        FireCorrelationState state,
        const std::uint64_t weaponGenerationKey,
        const std::uint64_t fireSequence,
        const std::uint64_t fireActivityOrderAtEvent,
        const std::span<const ActivityIdentity> activities)
    {
        FireCorrelationStep result{};
        if (!state.initialized ||
            state.weaponGenerationKey != weaponGenerationKey) {
            state = {};
            state.initialized = true;
            state.weaponGenerationKey = weaponGenerationKey;
            state.lastFireSequence = fireSequence;
            for (const auto& activity : activities) {
                state.highestObservedActivationOrder = (std::max)(
                    state.highestObservedActivationOrder,
                    activity.activationOrder);
            }
            result.state = state;
            return result;
        }

        std::array<
            std::uint64_t,
            kMaximumCorrelatedFireActivities>
            retained{};
        std::uint32_t retainedCount = 0;
        for (std::uint32_t index = 0;
             index < state.correlatedCount;
             ++index) {
            const auto order = state.correlatedActivationOrders[index];
            if (activityActive(activities, order) &&
                retainedCount < retained.size()) {
                retained[retainedCount++] = order;
            }
        }
        state.correlatedActivationOrders = retained;
        state.correlatedCount = retainedCount;

        const auto addActivity = [&](const std::uint64_t order) {
            if (!activityActive(activities, order) ||
                state.correlatedCount >=
                    state.correlatedActivationOrders.size()) {
                return;
            }
            for (std::uint32_t index = 0;
                 index < state.correlatedCount;
                 ++index) {
                if (state.correlatedActivationOrders[index] == order) {
                    return;
                }
            }
            state.correlatedActivationOrders[
                state.correlatedCount++] = order;
        };

        result.newFireEvent = fireSequence != state.lastFireSequence;
        if (result.newFireEvent) {
            state.lastFireSequence = fireSequence;
            state.admissionFramesRemaining =
                kFireActivityAdmissionFrames;
            addActivity(fireActivityOrderAtEvent);
        } else if (state.admissionFramesRemaining > 0) {
            --state.admissionFramesRemaining;
        }

        if (result.newFireEvent ||
            state.admissionFramesRemaining > 0) {
            for (const auto& activity : activities) {
                if (activity.activationOrder >
                    state.highestObservedActivationOrder) {
                    addActivity(activity.activationOrder);
                }
            }
        }

        for (const auto& activity : activities) {
            state.highestObservedActivationOrder = (std::max)(
                state.highestObservedActivationOrder,
                activity.activationOrder);
        }
        result.activeCorrelatedCount = state.correlatedCount;
        result.active = result.newFireEvent ||
            state.admissionFramesRemaining > 0 ||
            state.correlatedCount > 0;
        if (state.correlatedCount > 0) {
            result.correlation =
                api::PaperReloadFireCorrelationV1::ActivityBound;
        } else if (result.newFireEvent) {
            result.correlation =
                api::PaperReloadFireCorrelationV1::EventOnly;
        } else if (state.admissionFramesRemaining > 0) {
            result.correlation =
                api::PaperReloadFireCorrelationV1::PendingActivity;
        }
        result.state = state;
        return result;
    }
}
