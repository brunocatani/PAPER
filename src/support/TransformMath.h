#pragma once

#include <cmath>
#include <type_traits>

namespace rock_reanimate::transform_math
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
}
