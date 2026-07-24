#pragma once

#include <array>
#include <cstdint>

#include <RE/NetImmerse/NiTransform.h>

namespace paper::frik_visual_authority
{
    enum class Hand : std::uint8_t
    {
        Right,
        Left,
    };

    struct HandPoseData
    {};

    struct FingerLocalTransformOverride
    {
        std::uint16_t enabledMask{ 0 };
        std::array<RE::NiTransform, 15> localTransforms{};
    };

    void setSkeletonReadyHint(bool ready);
    [[nodiscard]] bool isSkeletonReadyHint();
    [[nodiscard]] bool clearHandPose(const char* tag, Hand hand);
    [[nodiscard]] bool setHandPoseCustomWithPriority(
        const char* tag,
        Hand hand,
        const HandPoseData& pose,
        int priority);
    [[nodiscard]] bool setHandPoseCustomLocalTransformsWithPriority(
        const char* tag,
        Hand hand,
        const FingerLocalTransformOverride* overrideData,
        int priority);
    [[nodiscard]] bool applyExternalHandWorldTransform(
        const char* tag,
        Hand hand,
        const RE::NiTransform& worldTarget,
        int priority);
    [[nodiscard]] bool clearExternalHandWorldTransform(const char* tag, Hand hand);
    [[nodiscard]] RE::NiTransform getHandWorldTransform(Hand hand);
}
