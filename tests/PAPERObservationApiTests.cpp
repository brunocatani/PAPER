#include <PAPER/PhysicalReload.h>
#include <PAPER/Provider.h>
#include "animation/NativePosePipelinePolicy.h"
#include "animation_evidence/AnimationPreharvestPolicy.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace
{
    int s_failures{ 0 };

    void expect(const bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAILED: " << message << '\n';
            ++s_failures;
        }
    }
}

int main()
{
    using namespace paper::api;
    using namespace paper::animation_preharvest_policy;

    static_assert(kRecordVersion == 1);
    static_assert(provider::kMajor == 2 && provider::kMinor == 0);
    static_assert(physical_reload::kMajor == 2 && physical_reload::kMinor == 0);
    // Provider table slots.
    static_assert(offsetof(provider::Api, getModVersion) == 0);
    static_assert(offsetof(provider::Api, unregisterEventCallback) == 104);
    static_assert(offsetof(provider::Api, getNativePoseFrameState) == 112);
    static_assert(offsetof(provider::Api, getNativeHandSolution) == 120);
    static_assert(sizeof(provider::Api) == 128);
    static_assert(sizeof(physical_reload::Api) == 24);
    static_assert(
        static_cast<std::uint32_t>(ConsumerCapability::All) ==
        (0x1Fu | (1u << 10)));
    static_assert(
        static_cast<std::uint32_t>(
            ConsumerCapability::NativePosePipeline) ==
        (1u << 10));
    static_assert(static_cast<std::uint32_t>(EventKind::ConfigReloaded) == 3);
    static_assert(
        static_cast<std::uint32_t>(EventKind::PresentationComplete) == 11);
    static_assert(sizeof(ConfigState) == 48);
    static_assert(offsetof(ConfigState, revision) == 24);
    static_assert(sizeof(Event) == 136);
    static_assert(offsetof(Event, runtime) == 16);
    static_assert(sizeof(NativePoseFrameState) == 264);
    static_assert(sizeof(NativeHandSolution) == 2000);

    Transform identity{};
    identity.rotate[0][0] = 1.0f;
    identity.rotate[1][1] = 1.0f;
    identity.rotate[2][2] = 1.0f;
    Transform translated = identity;
    translated.translate[0] = 3.0f;
    translated.translate[1] = 4.0f;
    const auto translationResidual =
        paper::native_pose_pipeline_policy::measurePoseResidual(
            identity,
            translated);
    expect(
        translationResidual.valid &&
            std::abs(translationResidual.translationGameUnits - 5.0f) <
                0.0001f &&
            std::abs(translationResidual.rotationDegrees) < 0.0001f,
        "pose residual must preserve world translation distance");

    Transform quarterTurn = identity;
    quarterTurn.rotate[0][0] = 0.0f;
    quarterTurn.rotate[0][1] = -1.0f;
    quarterTurn.rotate[1][0] = 1.0f;
    quarterTurn.rotate[1][1] = 0.0f;
    const auto rotationResidual =
        paper::native_pose_pipeline_policy::measurePoseResidual(
            identity,
            quarterTurn);
    expect(
        rotationResidual.valid &&
            std::abs(rotationResidual.rotationDegrees - 90.0f) < 0.001f,
        "pose residual must preserve target-to-presented rotation angle");

    expect(
        clipSampleCount(0.01f, 120.0f) == 24,
        "short clips must retain the exact sampler minimum");
    expect(
        clipSampleCount(10.0f, 120.0f) == 720,
        "long clips must expose the exact sampler cap");
    expect(
        clipSampleTime(2.0f, 3, 5) == 1.5f,
        "sample timestamps must span the complete clip duration");
    expect(
        canSampleAnotherPose(0, std::chrono::microseconds{ 5000 }),
        "the exact sampler must always make at least one pose of progress");
    expect(
        canSampleAnotherPose(1, kSamplingFrameBudget -
                std::chrono::microseconds{ 1 }),
        "the exact sampler must continue while its frame budget remains");
    expect(
        !canSampleAnotherPose(1, kSamplingFrameBudget),
        "the exact sampler must yield when its frame budget is exhausted");
    expect(
        !canSampleAnotherPose(
            kMaximumSamplesPerFrame, std::chrono::microseconds{ 0 }),
        "the exact sampler must retain a hard per-frame safety ceiling");
    expect(
        withoutSceneInstanceSuffix("WeaponMagazine:12") ==
            "WeaponMagazine",
        "scene instance suffixes must not alter raw rig-name matching");
    expect(
        boneNameMatchesSceneNode("weaponmagazine", "WeaponMagazine:2"),
        "rig and scene-node matching must remain case-insensitive");

    const std::array<std::int16_t, 4> parents{ -1, 0, 1, 2 };
    std::array<std::int16_t, 4> chain{};
    const auto chainCount = buildBoneChainBelowAncestor(
        3,
        1,
        parents,
        chain);
    expect(
        chainCount == 2 && chain[0] == 2 && chain[1] == 3,
        "exact hierarchy reconstruction must exclude the weapon ancestor");

    if (s_failures == 0) {
        std::cout << "PAPERObservationApiTests passed.\n";
    }
    return s_failures == 0 ? 0 : 1;
}
