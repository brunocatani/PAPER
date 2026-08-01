#include "api/PAPERApi.h"
#include "animation_evidence/AnimationPreharvestPolicy.h"
#include "animation_evidence/AnimationEvidencePolicy.h"
#include "reload_observation/ReloadObservationPolicy.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

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
    using namespace paper::animation_evidence_policy;
    using namespace paper::reload_observation_policy;

    static_assert(PAPER_API_VERSION == 1);
    static_assert(PAPER_MOD_VERSION == 300);
    static_assert(PAPER_MAX_RELOAD_EVIDENCE_V1 == 100);
    static_assert(PAPER_MAX_RELOAD_EVIDENCE_POINTS_V1 == 25'200);
    static_assert(
        offsetof(PaperProviderApiV1, getReloadObservationLimitsV1) ==
        PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES);
    static_assert(PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES == 120);
    static_assert(PAPER_PROVIDER_API_V1_TABLE_BYTES == 248);
    static_assert(
        PAPER_PROVIDER_API_V1_RELOAD_OBSERVATION_TABLE_BYTES ==
        176);
    static_assert(
        offsetof(PaperProviderApiV1, getReloadAnimationLimitsV1) ==
        PAPER_PROVIDER_API_V1_RELOAD_OBSERVATION_TABLE_BYTES);
    static_assert(
        PAPER_PROVIDER_API_V1_RELOAD_ANIMATION_TABLE_BYTES ==
        sizeof(PaperProviderApiV1));
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::ReloadObservations)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::ReloadEvidenceGeometry)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::ReloadAnimationEvidence)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::ReloadAnimationTelemetry)) != 0);
    static_assert(
        (static_cast<std::uint32_t>(PaperConsumerCapabilityV1::All) &
            static_cast<std::uint32_t>(
                PaperConsumerCapabilityV1::ReloadAnimationEvidence)) != 0);
    static_assert(
        static_cast<std::uint32_t>(
            PaperConsumerCapabilityV1::ReloadAnimationEvidence) ==
        (1u << 7));
    static_assert(
        static_cast<std::uint32_t>(
            PaperConsumerCapabilityV1::ReloadAnimationTelemetry) ==
        (1u << 8));
    static_assert(
        (static_cast<std::uint32_t>(PaperConsumerCapabilityV1::All) &
            static_cast<std::uint32_t>(
                PaperConsumerCapabilityV1::ReloadAnimationTelemetry)) != 0);
    static_assert(sizeof(PaperReloadQsTransformV1) == 40);
    static_assert(
        PAPER_RELOAD_ANIMATION_SAMPLE_BUDGET_BYTES_V1 ==
        128ull * 1024ull * 1024ull);

    expect(
        clipSampleCount(0.01f) == 24,
        "short clips must retain the exact sampler minimum");
    expect(
        clipSampleCount(10.0f) == 720,
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
    expect(
        boundedSampleCopyCount(10, 5 * sizeof(PaperReloadQsTransformV1),
            sizeof(PaperReloadQsTransformV1)) == 5,
        "sample storage must truncate at the explicit byte budget");
    expect(
        sampleTime(2.0f, 3, 5) == 1.5f,
        "published samples must retain authored full-clip timestamps");

    const std::array<EvidenceNodeCandidate, 3> evidence{
        EvidenceNodeCandidate{ 3, 4 },
        EvidenceNodeCandidate{ 3, 5 },
        EvidenceNodeCandidate{ -1, 2 },
    };
    std::array<std::uint8_t, 8> namedNodes{};
    namedNodes[0] = 1;
    namedNodes[1] = 1;
    namedNodes[2] = 1;
    namedNodes[6] = 1;
    const auto prioritized = selectObservationTargets(
        static_cast<std::uint32_t>(namedNodes.size()),
        evidence,
        namedNodes);
    const std::array<std::uint32_t, 7> expectedOrder{
        3,
        4,
        5,
        2,
        0,
        1,
        6,
    };
    expect(
        prioritized.count == expectedOrder.size(),
        "evidence and named targets should be selected once");
    for (std::size_t index = 0; index < expectedOrder.size(); ++index) {
        expect(
            prioritized.nodeIds[index] == expectedOrder[index],
            "evidence nodes must precede remaining named nodes");
    }
    expect(
        prioritized.candidateCount == expectedOrder.size(),
        "duplicate and invalid evidence IDs must not count as candidates");
    expect(
        prioritized.omittedCount == 0,
        "bounded selection should report no omitted targets");

    std::array<std::uint8_t, 200> overflowNodes{};
    overflowNodes.fill(1);
    const auto overflow = selectObservationTargets(
        static_cast<std::uint32_t>(overflowNodes.size()),
        std::span<const EvidenceNodeCandidate>{},
        overflowNodes);
    expect(
        overflow.count == PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1,
        "selection must respect the public observation target limit");
    expect(
        overflow.candidateCount == overflowNodes.size(),
        "selection must count all bounded unique candidates");
    expect(
        overflow.omittedCount ==
            overflowNodes.size() -
                PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1,
        "selection must expose omitted target count");
    expect(
        overflow.nodeIds.front() == 0 &&
            overflow.nodeIds.back() ==
                PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1 - 1,
        "named-node fallback must retain catalog order");

    const std::array<EvidenceNodeCandidate, 2> invalidEvidence{
        EvidenceNodeCandidate{ -1, -1 },
        EvidenceNodeCandidate{ 2048, 4096 },
    };
    const auto invalid = selectObservationTargets(
        8,
        invalidEvidence,
        std::span<const std::uint8_t>{});
    expect(
        invalid.count == 0 && invalid.candidateCount == 0,
        "out-of-range evidence IDs must be ignored");

    if (s_failures == 0) {
        std::cout << "PAPERObservationApiTests passed.\n";
    }
    return s_failures == 0 ? 0 : 1;
}
