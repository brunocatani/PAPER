#pragma once

#include "api/PAPERApi.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace paper::native_pose_pipeline_policy
{
    struct PoseResidual
    {
        float translationGameUnits{ 0.0f };
        float rotationDegrees{ 0.0f };
        bool valid{ false };
    };

    [[nodiscard]] inline bool finiteTransform(
        const api::PaperTransformV1& transform)
    {
        for (const auto& row : transform.rotate) {
            for (const float value : row) {
                if (!std::isfinite(value)) {
                    return false;
                }
            }
        }
        return std::isfinite(transform.translate[0]) &&
               std::isfinite(transform.translate[1]) &&
               std::isfinite(transform.translate[2]) &&
               std::isfinite(transform.scale);
    }

    [[nodiscard]] inline PoseResidual measurePoseResidual(
        const api::PaperTransformV1& target,
        const api::PaperTransformV1& presented)
    {
        if (!finiteTransform(target) || !finiteTransform(presented)) {
            return {};
        }

        const float x = presented.translate[0] - target.translate[0];
        const float y = presented.translate[1] - target.translate[1];
        const float z = presented.translate[2] - target.translate[2];
        float relativeTrace = 0.0f;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                relativeTrace += target.rotate[row][column] *
                    presented.rotate[row][column];
            }
        }
        const float cosine = std::clamp(
            (relativeTrace - 1.0f) * 0.5f,
            -1.0f,
            1.0f);
        const float radians = std::acos(cosine);
        const float degrees = radians *
            (180.0f / std::numbers::pi_v<float>);
        if (!std::isfinite(degrees)) {
            return {};
        }
        return {
            .translationGameUnits = std::sqrt(x * x + y * y + z * z),
            .rotationDegrees = degrees,
            .valid = true,
        };
    }
}
