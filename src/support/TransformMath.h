#pragma once

#include <cmath>
#include <type_traits>

namespace paper::transform_math
{
    namespace detail
    {
        template <class Value>
        using StoredValue = std::remove_cv_t<std::remove_reference_t<Value>>;

        template <class Value>
        inline StoredValue<Value> narrowDouble(const double value)
        {
            return static_cast<StoredValue<Value>>(value);
        }

        template <class Vector>
        inline Vector makeVector(const double x, const double y, const double z)
        {
            Vector result{};
            result.x = narrowDouble<decltype(result.x)>(x);
            result.y = narrowDouble<decltype(result.y)>(y);
            result.z = narrowDouble<decltype(result.z)>(z);
            return result;
        }

        template <class Matrix>
        inline double multiplyRotationEntry(
            const Matrix& lhs,
            const Matrix& rhs,
            const int row,
            const int column)
        {
            return static_cast<double>(lhs.entry[row][0]) * rhs.entry[0][column] +
                   static_cast<double>(lhs.entry[row][1]) * rhs.entry[1][column] +
                   static_cast<double>(lhs.entry[row][2]) * rhs.entry[2][column];
        }
    }

    template <class Matrix>
    inline Matrix identityRotation()
    {
        Matrix result{};
        result.entry[0][0] = 1.0f;
        result.entry[1][1] = 1.0f;
        result.entry[2][2] = 1.0f;
        return result;
    }

    template <class Transform>
    inline Transform identityTransform()
    {
        Transform result{};
        result.rotate = identityRotation<decltype(result.rotate)>();
        result.scale = 1.0f;
        return result;
    }

    template <class Matrix>
    inline Matrix transposeRotation(const Matrix& matrix)
    {
        Matrix result{};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                result.entry[row][column] = matrix.entry[column][row];
            }
        }
        return result;
    }

    template <class Matrix>
    inline Matrix multiplyStoredRotations(const Matrix& lhs, const Matrix& rhs)
    {
        Matrix result{};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                result.entry[row][column] = static_cast<float>(
                    detail::multiplyRotationEntry(lhs, rhs, row, column));
            }
        }
        return result;
    }

    template <class Transform, class Vector>
    inline Vector localPointToWorld(const Transform& transform, const Vector& point)
    {
        const double scale = transform.scale;
        return detail::makeVector<Vector>(
            transform.translate.x +
                (transform.rotate.entry[0][0] * point.x +
                    transform.rotate.entry[1][0] * point.y +
                    transform.rotate.entry[2][0] * point.z) * scale,
            transform.translate.y +
                (transform.rotate.entry[0][1] * point.x +
                    transform.rotate.entry[1][1] * point.y +
                    transform.rotate.entry[2][1] * point.z) * scale,
            transform.translate.z +
                (transform.rotate.entry[0][2] * point.x +
                    transform.rotate.entry[1][2] * point.y +
                    transform.rotate.entry[2][2] * point.z) * scale);
    }

    template <class Transform, class Vector>
    inline Vector worldPointToLocal(const Transform& transform, const Vector& point)
    {
        const double scale = transform.scale;
        const double inverseScale = std::abs(scale) > 0.0001 ? 1.0 / scale : 1.0;
        const double x = point.x - transform.translate.x;
        const double y = point.y - transform.translate.y;
        const double z = point.z - transform.translate.z;
        return detail::makeVector<Vector>(
            (transform.rotate.entry[0][0] * x + transform.rotate.entry[0][1] * y + transform.rotate.entry[0][2] * z) * inverseScale,
            (transform.rotate.entry[1][0] * x + transform.rotate.entry[1][1] * y + transform.rotate.entry[1][2] * z) * inverseScale,
            (transform.rotate.entry[2][0] * x + transform.rotate.entry[2][1] * y + transform.rotate.entry[2][2] * z) * inverseScale);
    }

    template <class Transform>
    inline Transform composeTransforms(const Transform& parent, const Transform& child)
    {
        Transform result = identityTransform<Transform>();
        result.rotate = multiplyStoredRotations(child.rotate, parent.rotate);
        result.translate = localPointToWorld(parent, child.translate);
        result.scale = detail::narrowDouble<decltype(result.scale)>(
            static_cast<double>(parent.scale) * child.scale);
        return result;
    }

    template <class Transform>
    inline Transform invertTransform(const Transform& transform)
    {
        Transform result = identityTransform<Transform>();
        result.rotate = transposeRotation(transform.rotate);
        const double scale = transform.scale;
        result.scale = detail::narrowDouble<decltype(result.scale)>(
            std::abs(scale) > 0.0001 ? 1.0 / scale : 1.0);
        result.translate = worldPointToLocal(transform, decltype(transform.translate){});
        return result;
    }

    template <class Transform>
    inline Transform relativeTransform(
        const Transform& referenceModel,
        const Transform& childModel)
    {
        return composeTransforms(
            invertTransform(referenceModel),
            childModel);
    }

    template <class Matrix>
    inline Matrix havokQuaternionToNiRows(const float quaternion[4])
    {
        Matrix matrix = identityRotation<Matrix>();
        float x = quaternion[0];
        float y = quaternion[1];
        float z = quaternion[2];
        float w = quaternion[3];
        const float length = std::sqrt(x * x + y * y + z * z + w * w);
        if (length <= 0.000001f) {
            return matrix;
        }
        const float inverseLength = 1.0f / length;
        x *= inverseLength;
        y *= inverseLength;
        z *= inverseLength;
        w *= inverseLength;

        matrix.entry[0][0] = 1.0f - 2.0f * (y * y + z * z);
        matrix.entry[0][1] = 2.0f * (x * y - w * z);
        matrix.entry[0][2] = 2.0f * (x * z + w * y);
        matrix.entry[1][0] = 2.0f * (x * y + w * z);
        matrix.entry[1][1] = 1.0f - 2.0f * (x * x + z * z);
        matrix.entry[1][2] = 2.0f * (y * z - w * x);
        matrix.entry[2][0] = 2.0f * (x * z - w * y);
        matrix.entry[2][1] = 2.0f * (y * z + w * x);
        matrix.entry[2][2] = 1.0f - 2.0f * (x * x + y * y);
        return matrix;
    }

    template <class Matrix>
    inline void niRowsToHavokQuaternion(
        const Matrix& matrix,
        float outQuaternion[4])
    {
        const float trace =
            matrix.entry[0][0] + matrix.entry[1][1] + matrix.entry[2][2];
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        if (trace > 0.0f) {
            const float root = std::sqrt(trace + 1.0f);
            const float inverse = 0.5f / root;
            w = root * 0.5f;
            x = (matrix.entry[2][1] - matrix.entry[1][2]) * inverse;
            y = (matrix.entry[0][2] - matrix.entry[2][0]) * inverse;
            z = (matrix.entry[1][0] - matrix.entry[0][1]) * inverse;
        } else if (
            matrix.entry[0][0] > matrix.entry[1][1] &&
            matrix.entry[0][0] > matrix.entry[2][2]) {
            const float root = std::sqrt(
                matrix.entry[0][0] - matrix.entry[1][1] -
                matrix.entry[2][2] + 1.0f);
            const float inverse = 0.5f / root;
            x = root * 0.5f;
            y = (matrix.entry[1][0] + matrix.entry[0][1]) * inverse;
            z = (matrix.entry[0][2] + matrix.entry[2][0]) * inverse;
            w = (matrix.entry[2][1] - matrix.entry[1][2]) * inverse;
        } else if (matrix.entry[1][1] > matrix.entry[2][2]) {
            const float root = std::sqrt(
                matrix.entry[1][1] - matrix.entry[2][2] -
                matrix.entry[0][0] + 1.0f);
            const float inverse = 0.5f / root;
            x = (matrix.entry[1][0] + matrix.entry[0][1]) * inverse;
            y = root * 0.5f;
            z = (matrix.entry[2][1] + matrix.entry[1][2]) * inverse;
            w = (matrix.entry[0][2] - matrix.entry[2][0]) * inverse;
        } else {
            const float root = std::sqrt(
                matrix.entry[2][2] - matrix.entry[0][0] -
                matrix.entry[1][1] + 1.0f);
            const float inverse = 0.5f / root;
            x = (matrix.entry[0][2] + matrix.entry[2][0]) * inverse;
            y = (matrix.entry[2][1] + matrix.entry[1][2]) * inverse;
            z = root * 0.5f;
            w = (matrix.entry[1][0] - matrix.entry[0][1]) * inverse;
        }

        const float length = std::sqrt(x * x + y * y + z * z + w * w);
        if (length > 0.0f) {
            const float inverseLength = 1.0f / length;
            x *= inverseLength;
            y *= inverseLength;
            z *= inverseLength;
            w *= inverseLength;
        }
        outQuaternion[0] = x;
        outQuaternion[1] = y;
        outQuaternion[2] = z;
        outQuaternion[3] = w;
    }
}
