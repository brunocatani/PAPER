#pragma once

#include <cstdint>

#include <RE/Fallout.h>

namespace paper::fo4vr
{
    class BSFlattenedBoneTree : public RE::NiNode
    {
    public:
        struct BoneTransforms
        {
            RE::NiTransform local;
            RE::NiTransform world;
            std::int16_t parPos;
            std::int16_t childPos;
            std::uint32_t unk8c;
            RE::NiNode* refNode;
            RE::BSFixedString name;
            std::uint64_t unk98;
        };

        struct BoneNodePosition
        {
            RE::BSFixedString name;
            std::int32_t position;
            std::uint32_t pad;
            std::uintptr_t unk;
        };

        std::int32_t numTransforms;
        std::uint32_t pad0;
        BoneTransforms* transforms;
        std::uint64_t unk190;
        std::uint64_t unk198;
        std::uint64_t unk1a0;
        std::uint64_t unk1a8;
        std::uint64_t unk1b0;
        BoneNodePosition* bonePositions;
    };
    static_assert(sizeof(BSFlattenedBoneTree::BoneTransforms) == 0xA0);
    static_assert(sizeof(BSFlattenedBoneTree) == 0x1C0);

    [[nodiscard]] RE::PlayerCharacter* getPlayer() noexcept;
    [[nodiscard]] BSFlattenedBoneTree* getFlattenedBoneTree() noexcept;
    [[nodiscard]] BSFlattenedBoneTree* getFirstPersonBoneTree() noexcept;
    [[nodiscard]] RE::EquippedItem* getEquippedItem() noexcept;
    void updateTransforms(RE::NiAVObject* node) noexcept;
    void updateTransformsDown(RE::NiAVObject* node, bool updateSelf) noexcept;
}

namespace f4vr = paper::fo4vr;
