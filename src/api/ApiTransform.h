#pragma once

#include "api/RockTypes.h"
#include "api/PAPERApi.h"

#include <RE/NetImmerse/NiTransform.h>

namespace paper::api_transform
{
    [[nodiscard]] inline RE::NiTransform toNi(
        const rock::api::Transform& source)
    {
        RE::NiTransform result{};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                result.rotate.entry[row][column] =
                    source.rotate[row * 3 + column];
            }
        }
        result.translate.x = source.translate[0];
        result.translate.y = source.translate[1];
        result.translate.z = source.translate[2];
        result.scale = source.scale;
        return result;
    }

    inline void fromNi(
        const RE::NiTransform& source,
        rock::api::Transform& target)
    {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                target.rotate[row * 3 + column] =
                    source.rotate.entry[row][column];
            }
        }
        target.translate[0] = source.translate.x;
        target.translate[1] = source.translate.y;
        target.translate[2] = source.translate.z;
        target.scale = source.scale;
    }

    inline void fromNi(
        const RE::NiTransform& source,
        api::PaperTransformV1& target)
    {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                target.rotate[row][column] = source.rotate.entry[row][column];
            }
        }
        target.translate[0] = source.translate.x;
        target.translate[1] = source.translate.y;
        target.translate[2] = source.translate.z;
        target.scale = source.scale;
    }

    inline void fromRock(
        const rock::api::Transform& source,
        api::PaperTransformV1& target)
    {
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                target.rotate[row][column] =
                    source.rotate[row * 3 + column];
            }
        }
        target.translate[0] = source.translate[0];
        target.translate[1] = source.translate[1];
        target.translate[2] = source.translate[2];
        target.scale = source.scale;
    }
}
