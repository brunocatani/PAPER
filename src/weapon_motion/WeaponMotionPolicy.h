#pragma once

#include "api/PAPERApi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace paper::weapon_motion_policy
{
    using api::PaperReloadQsTransformV1;

    inline constexpr float kTranslationEpsilonGameUnits = 0.10f;
    inline constexpr float kRotationEpsilonRadians = 0.02f;
    inline constexpr float kMinimumExcursionGameUnits = 0.35f;
    inline constexpr float kReplaceRatio = 1.02f;
    inline constexpr float kRotationDistanceWeight = 3.0f;
    inline constexpr std::uint32_t kArmStableFrames = 8;
    inline constexpr std::uint32_t kSettleFrames = 6;
    inline constexpr std::uint32_t kMaximumStrokeSamples = 720;

    [[nodiscard]] inline float clampUnit(const float value)
    {
        return (std::clamp)(value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline bool finite(const PaperReloadQsTransformV1& value)
    {
        for (const auto component : value.translate) {
            if (!std::isfinite(component)) {
                return false;
            }
        }
        for (const auto component : value.rotate) {
            if (!std::isfinite(component)) {
                return false;
            }
        }
        for (const auto component : value.scale) {
            if (!std::isfinite(component) || std::abs(component) < 1.0e-5f) {
                return false;
            }
        }
        return true;
    }

    inline void normalizeQuaternion(float value[4])
    {
        const auto length = std::sqrt(
            value[0] * value[0] + value[1] * value[1] +
            value[2] * value[2] + value[3] * value[3]);
        if (!std::isfinite(length) || length < 1.0e-6f) {
            value[0] = 0.0f;
            value[1] = 0.0f;
            value[2] = 0.0f;
            value[3] = 1.0f;
            return;
        }
        for (auto& component : std::span<float, 4>{ value, 4 }) {
            component /= length;
        }
    }

    [[nodiscard]] inline PaperReloadQsTransformV1 fromTransform(
        const api::PaperTransformV1& source)
    {
        PaperReloadQsTransformV1 result{};
        std::copy_n(source.translate, 3, result.translate);
        std::fill_n(result.scale, 3, source.scale);

        const auto trace = source.rotate[0][0] + source.rotate[1][1] +
            source.rotate[2][2];
        if (trace > 0.0f) {
            const auto s = std::sqrt(trace + 1.0f) * 2.0f;
            result.rotate[3] = 0.25f * s;
            result.rotate[0] =
                (source.rotate[2][1] - source.rotate[1][2]) / s;
            result.rotate[1] =
                (source.rotate[0][2] - source.rotate[2][0]) / s;
            result.rotate[2] =
                (source.rotate[1][0] - source.rotate[0][1]) / s;
        } else if (source.rotate[0][0] > source.rotate[1][1] &&
                   source.rotate[0][0] > source.rotate[2][2]) {
            const auto s = std::sqrt(
                1.0f + source.rotate[0][0] - source.rotate[1][1] -
                source.rotate[2][2]) * 2.0f;
            result.rotate[3] =
                (source.rotate[2][1] - source.rotate[1][2]) / s;
            result.rotate[0] = 0.25f * s;
            result.rotate[1] =
                (source.rotate[0][1] + source.rotate[1][0]) / s;
            result.rotate[2] =
                (source.rotate[0][2] + source.rotate[2][0]) / s;
        } else if (source.rotate[1][1] > source.rotate[2][2]) {
            const auto s = std::sqrt(
                1.0f + source.rotate[1][1] - source.rotate[0][0] -
                source.rotate[2][2]) * 2.0f;
            result.rotate[3] =
                (source.rotate[0][2] - source.rotate[2][0]) / s;
            result.rotate[0] =
                (source.rotate[0][1] + source.rotate[1][0]) / s;
            result.rotate[1] = 0.25f * s;
            result.rotate[2] =
                (source.rotate[1][2] + source.rotate[2][1]) / s;
        } else {
            const auto s = std::sqrt(
                1.0f + source.rotate[2][2] - source.rotate[0][0] -
                source.rotate[1][1]) * 2.0f;
            result.rotate[3] =
                (source.rotate[1][0] - source.rotate[0][1]) / s;
            result.rotate[0] =
                (source.rotate[0][2] + source.rotate[2][0]) / s;
            result.rotate[1] =
                (source.rotate[1][2] + source.rotate[2][1]) / s;
            result.rotate[2] = 0.25f * s;
        }
        normalizeQuaternion(result.rotate);
        return result;
    }

    inline void multiplyQuaternion(
        const float left[4],
        const float right[4],
        float out[4])
    {
        const float result[4]{
            left[3] * right[0] + left[0] * right[3] +
                left[1] * right[2] - left[2] * right[1],
            left[3] * right[1] - left[0] * right[2] +
                left[1] * right[3] + left[2] * right[0],
            left[3] * right[2] + left[0] * right[1] -
                left[1] * right[0] + left[2] * right[3],
            left[3] * right[3] - left[0] * right[0] -
                left[1] * right[1] - left[2] * right[2],
        };
        std::copy_n(result, 4, out);
        normalizeQuaternion(out);
    }

    inline void rotateVector(
        const float rotation[4],
        const float value[3],
        float out[3])
    {
        const float qVector[3]{ rotation[0], rotation[1], rotation[2] };
        const float cross1[3]{
            qVector[1] * value[2] - qVector[2] * value[1],
            qVector[2] * value[0] - qVector[0] * value[2],
            qVector[0] * value[1] - qVector[1] * value[0],
        };
        const float cross2[3]{
            qVector[1] * cross1[2] - qVector[2] * cross1[1],
            qVector[2] * cross1[0] - qVector[0] * cross1[2],
            qVector[0] * cross1[1] - qVector[1] * cross1[0],
        };
        for (std::size_t axis = 0; axis < 3; ++axis) {
            out[axis] = value[axis] +
                2.0f * (rotation[3] * cross1[axis] + cross2[axis]);
        }
    }

    [[nodiscard]] inline PaperReloadQsTransformV1 compose(
        const PaperReloadQsTransformV1& parent,
        const PaperReloadQsTransformV1& local)
    {
        PaperReloadQsTransformV1 result{};
        multiplyQuaternion(parent.rotate, local.rotate, result.rotate);
        float scaled[3]{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            scaled[axis] = local.translate[axis] * parent.scale[axis];
            result.scale[axis] = parent.scale[axis] * local.scale[axis];
        }
        float rotated[3]{};
        rotateVector(parent.rotate, scaled, rotated);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.translate[axis] = parent.translate[axis] + rotated[axis];
        }
        return result;
    }

    [[nodiscard]] inline PaperReloadQsTransformV1 inverse(
        const PaperReloadQsTransformV1& source)
    {
        PaperReloadQsTransformV1 result{};
        result.rotate[0] = -source.rotate[0];
        result.rotate[1] = -source.rotate[1];
        result.rotate[2] = -source.rotate[2];
        result.rotate[3] = source.rotate[3];
        normalizeQuaternion(result.rotate);
        float negative[3]{
            -source.translate[0],
            -source.translate[1],
            -source.translate[2],
        };
        float rotated[3]{};
        rotateVector(result.rotate, negative, rotated);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.scale[axis] = 1.0f / source.scale[axis];
            result.translate[axis] = rotated[axis] * result.scale[axis];
        }
        return result;
    }

    [[nodiscard]] inline PaperReloadQsTransformV1 rebase(
        const PaperReloadQsTransformV1& authoredRest,
        const PaperReloadQsTransformV1& liveRest,
        const PaperReloadQsTransformV1& authoredPose)
    {
        return compose(
            liveRest,
            compose(inverse(authoredRest), authoredPose));
    }

    [[nodiscard]] inline float translationDistance(
        const PaperReloadQsTransformV1& left,
        const PaperReloadQsTransformV1& right)
    {
        const auto x = left.translate[0] - right.translate[0];
        const auto y = left.translate[1] - right.translate[1];
        const auto z = left.translate[2] - right.translate[2];
        return std::sqrt(x * x + y * y + z * z);
    }

    [[nodiscard]] inline float rotationDistanceRadians(
        const PaperReloadQsTransformV1& left,
        const PaperReloadQsTransformV1& right)
    {
        const auto dot = std::abs(
            left.rotate[0] * right.rotate[0] +
            left.rotate[1] * right.rotate[1] +
            left.rotate[2] * right.rotate[2] +
            left.rotate[3] * right.rotate[3]);
        return 2.0f * std::acos((std::clamp)(dot, 0.0f, 1.0f));
    }

    [[nodiscard]] inline float poseDistance(
        const PaperReloadQsTransformV1& left,
        const PaperReloadQsTransformV1& right)
    {
        return translationDistance(left, right) +
            rotationDistanceRadians(left, right) * kRotationDistanceWeight;
    }

    [[nodiscard]] inline bool stable(
        const PaperReloadQsTransformV1& left,
        const PaperReloadQsTransformV1& right)
    {
        return translationDistance(left, right) <=
                   kTranslationEpsilonGameUnits &&
               rotationDistanceRadians(left, right) <=
                   kRotationEpsilonRadians;
    }

    [[nodiscard]] inline PaperReloadQsTransformV1 interpolate(
        const PaperReloadQsTransformV1& left,
        const PaperReloadQsTransformV1& right,
        const float fraction)
    {
        const auto t = clampUnit(fraction);
        PaperReloadQsTransformV1 result{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.translate[axis] =
                left.translate[axis] +
                (right.translate[axis] - left.translate[axis]) * t;
            result.scale[axis] =
                left.scale[axis] +
                (right.scale[axis] - left.scale[axis]) * t;
        }
        auto sign = 1.0f;
        const auto dot = left.rotate[0] * right.rotate[0] +
            left.rotate[1] * right.rotate[1] +
            left.rotate[2] * right.rotate[2] +
            left.rotate[3] * right.rotate[3];
        if (dot < 0.0f) {
            sign = -1.0f;
        }
        for (std::size_t component = 0; component < 4; ++component) {
            result.rotate[component] = left.rotate[component] +
                (right.rotate[component] * sign - left.rotate[component]) * t;
        }
        normalizeQuaternion(result.rotate);
        return result;
    }

    template <std::size_t Capacity>
    [[nodiscard]] inline float resamplePath(
        const std::array<PaperReloadQsTransformV1, Capacity>& samples,
        const std::uint32_t first,
        const std::uint32_t last,
        std::array<
            PaperReloadQsTransformV1,
            api::PAPER_WEAPON_MOTION_KEY_COUNT_V1>& outKeys)
    {
        if (first >= Capacity || last >= Capacity || first >= last) {
            return 0.0f;
        }
        std::array<float, Capacity> arc{};
        auto total = 0.0f;
        for (auto index = first + 1; index <= last; ++index) {
            total += poseDistance(samples[index - 1], samples[index]);
            arc[index] = total;
        }
        if (!std::isfinite(total) || total < kMinimumExcursionGameUnits) {
            return 0.0f;
        }
        for (std::uint32_t key = 0;
             key < api::PAPER_WEAPON_MOTION_KEY_COUNT_V1;
             ++key) {
            const auto target = total * static_cast<float>(key) /
                static_cast<float>(
                    api::PAPER_WEAPON_MOTION_KEY_COUNT_V1 - 1);
            auto upper = first + 1;
            while (upper < last && arc[upper] < target) {
                ++upper;
            }
            const auto lower = upper - 1;
            const auto lowerArc = arc[lower];
            const auto segment = arc[upper] - lowerArc;
            const auto fraction = segment > 1.0e-5f ?
                (target - lowerArc) / segment :
                0.0f;
            outKeys[key] = interpolate(
                samples[lower], samples[upper], fraction);
        }
        return total;
    }

    template <std::size_t Capacity>
    [[nodiscard]] inline std::uint32_t peakIndex(
        const std::array<PaperReloadQsTransformV1, Capacity>& samples,
        const std::uint32_t count)
    {
        if (count == 0) {
            return 0;
        }
        auto peak = 0u;
        auto maximum = 0.0f;
        for (std::uint32_t index = 1; index < count; ++index) {
            const auto distance = poseDistance(samples[0], samples[index]);
            if (distance > maximum) {
                maximum = distance;
                peak = index;
            }
        }
        return peak;
    }

    struct Projection
    {
        bool valid{ false };
        float normalizedProgress{ 0.0f };
        float arcPosition{ 0.0f };
        float residual{ 0.0f };
        PaperReloadQsTransformV1 pose{};
    };

    [[nodiscard]] inline Projection projectOntoPath(
        const PaperReloadQsTransformV1& value,
        const std::array<
            PaperReloadQsTransformV1,
            api::PAPER_WEAPON_MOTION_KEY_COUNT_V1>& keys)
    {
        Projection result{};
        auto totalArc = 0.0f;
        std::array<float, api::PAPER_WEAPON_MOTION_KEY_COUNT_V1> arc{};
        for (std::size_t index = 1; index < keys.size(); ++index) {
            totalArc += poseDistance(keys[index - 1], keys[index]);
            arc[index] = totalArc;
        }
        if (totalArc < kMinimumExcursionGameUnits) {
            return result;
        }
        auto bestResidual = (std::numeric_limits<float>::max)();
        for (std::size_t index = 0; index + 1 < keys.size(); ++index) {
            const float delta[3]{
                keys[index + 1].translate[0] - keys[index].translate[0],
                keys[index + 1].translate[1] - keys[index].translate[1],
                keys[index + 1].translate[2] - keys[index].translate[2],
            };
            const float relative[3]{
                value.translate[0] - keys[index].translate[0],
                value.translate[1] - keys[index].translate[1],
                value.translate[2] - keys[index].translate[2],
            };
            const auto denominator = delta[0] * delta[0] +
                delta[1] * delta[1] + delta[2] * delta[2];
            const auto fraction = denominator > 1.0e-6f ?
                clampUnit((relative[0] * delta[0] + relative[1] * delta[1] +
                    relative[2] * delta[2]) / denominator) :
                0.0f;
            const auto projected = interpolate(
                keys[index], keys[index + 1], fraction);
            const auto residual = poseDistance(value, projected);
            if (residual < bestResidual) {
                bestResidual = residual;
                result.valid = true;
                result.pose = projected;
                result.arcPosition = arc[index] +
                    (arc[index + 1] - arc[index]) * fraction;
            }
        }
        result.residual = bestResidual;
        result.normalizedProgress = clampUnit(result.arcPosition / totalArc);
        return result;
    }

    [[nodiscard]] inline char asciiLower(const char value)
    {
        return value >= 'A' && value <= 'Z' ?
            static_cast<char>(value + ('a' - 'A')) :
            value;
    }

    [[nodiscard]] inline std::string_view withoutInstanceSuffix(
        const std::string_view value)
    {
        const auto colon = value.find_last_of(':');
        if (colon == std::string_view::npos || colon + 1 >= value.size()) {
            return value;
        }
        for (auto index = colon + 1; index < value.size(); ++index) {
            if (value[index] < '0' || value[index] > '9') {
                return value;
            }
        }
        return value.substr(0, colon);
    }

    [[nodiscard]] inline bool namesMatch(
        std::string_view left,
        std::string_view right)
    {
        left = withoutInstanceSuffix(left);
        right = withoutInstanceSuffix(right);
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (asciiLower(left[index]) != asciiLower(right[index])) {
                return false;
            }
        }
        return !left.empty();
    }
}
