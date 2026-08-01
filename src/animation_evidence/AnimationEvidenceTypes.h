#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace paper::animation_evidence
{
    inline constexpr std::uint32_t kLiveClipSampleCount = 64;
    inline constexpr std::uint32_t kMaxLiveTracksPerClip = 64;
    inline constexpr std::uint32_t kMaxExactTracksPerClip = 128;
    inline constexpr std::uint32_t kMaxExactClipSamples = 720;
    inline constexpr std::uint32_t kMaxSkeletonBones = 768;
    inline constexpr std::uint32_t kMaxAnimationFiles = 512;
    inline constexpr std::uint32_t kBoneNameCapacity = 64;
    inline constexpr std::uint32_t kAnimationPathCapacity = 260;
    inline constexpr std::uint32_t kMarkerTextCapacity = 96;
    inline constexpr std::uint32_t kMaxAnnotationsPerClip = 128;
    inline constexpr std::uint32_t kMaxTriggersPerClip = 128;

    enum class ClipAcquisition : std::uint32_t
    {
        LoadedGraphBinding = 1,
        LiveClipActivation = 2,
        ExactWeaponPreharvest = 3,
    };

    enum class TrackSpace : std::uint32_t
    {
        RigBoneLocal = 1,
        WeaponRootLocal = 2,
    };

    struct Vec3
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    // Internal quaternion storage is scalar first (w, x, y, z). The public
    // PAPER API explicitly exports x/y/z/w.
    struct Quat
    {
        float w{ 1.0f };
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    struct PoseSample
    {
        Vec3 translate{};
        Quat rotate{};
    };

    struct LiveTrackSamples
    {
        std::array<char, kBoneNameCapacity> boneName{};
        std::uint32_t sampleCount{ 0 };
        std::array<PoseSample, kLiveClipSampleCount> samples{};
        std::array<Vec3, kLiveClipSampleCount> scales{};
    };

    struct ExactTrackSamples
    {
        std::array<char, kBoneNameCapacity> boneName{};
        std::int16_t boneIndex{ -1 };
        std::int16_t parentBoneIndex{ -1 };
        std::int16_t transformTrackIndex{ -1 };
        std::uint16_t chainDepth{ 0 };
        PoseSample referenceLocal{};
        Vec3 referenceScale{ 1.0f, 1.0f, 1.0f };
        std::uint32_t sampleCount{ 0 };
        std::array<PoseSample, kMaxExactClipSamples> samples{};
        std::array<Vec3, kMaxExactClipSamples> scales{};
    };

    struct ExactSkeletonBone
    {
        std::array<char, kBoneNameCapacity> name{};
        std::int16_t boneIndex{ -1 };
        std::int16_t parentBoneIndex{ -1 };
        std::int16_t transformTrackIndex{ -1 };
        std::uint16_t flags{ 0 };
        PoseSample referenceLocal{};
        Vec3 referenceScale{ 1.0f, 1.0f, 1.0f };
    };

    [[nodiscard]] inline Quat normalizedQuaternionOrIdentity(const Quat value)
    {
        const float lengthSquared = value.w * value.w + value.x * value.x +
                                    value.y * value.y + value.z * value.z;
        if (!(lengthSquared > 0.000001f)) {
            return {};
        }
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        return Quat{
            value.w * inverseLength,
            value.x * inverseLength,
            value.y * inverseLength,
            value.z * inverseLength,
        };
    }
}
