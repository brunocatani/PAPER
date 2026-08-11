#include "api/PAPERApi.h"
#include "animation/NativePosePipelinePolicy.h"
#include "animation_evidence/AnimationPreharvestPolicy.h"
#include "animation_evidence/AnimationEvidencePolicy.h"
#include "reload_observation/ReloadObservationPolicy.h"
#include "reload_stages/ReloadStagePolicy.h"
#include "weapon_motion/WeaponMotionPolicy.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
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
    static_assert(PAPER_MOD_VERSION == 500);
    static_assert(PAPER_MAX_RELOAD_EVIDENCE_V1 == 100);
    static_assert(PAPER_MAX_RELOAD_EVIDENCE_POINTS_V1 == 25'200);
    static_assert(
        offsetof(PaperProviderApiV1, getReloadObservationLimitsV1) ==
        PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES);
    static_assert(PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES == 120);
    static_assert(PAPER_PROVIDER_API_V1_TABLE_BYTES == 400);
    static_assert(
        PAPER_PROVIDER_API_V1_RELOAD_OBSERVATION_TABLE_BYTES ==
        176);
    static_assert(
        offsetof(PaperProviderApiV1, getReloadAnimationLimitsV1) ==
        PAPER_PROVIDER_API_V1_RELOAD_OBSERVATION_TABLE_BYTES);
    static_assert(
        PAPER_PROVIDER_API_V1_RELOAD_ANIMATION_TABLE_BYTES ==
        248);
    static_assert(
        PAPER_PROVIDER_API_V1_RELOAD_STAGE_TABLE_BYTES ==
        264);
    static_assert(
        offsetof(PaperProviderApiV1, getNativePoseFrameStateV1) ==
        PAPER_PROVIDER_API_V1_RELOAD_STAGE_TABLE_BYTES);
    static_assert(
        PAPER_PROVIDER_API_V1_NATIVE_POSE_PIPELINE_TABLE_BYTES ==
        280);
    static_assert(
        offsetof(PaperProviderApiV1, getWeaponMotionLimitsV1) ==
        PAPER_PROVIDER_API_V1_NATIVE_POSE_PIPELINE_TABLE_BYTES);
    static_assert(
        PAPER_PROVIDER_API_V1_WEAPON_MOTION_CATALOG_TABLE_BYTES ==
        336);
    static_assert(
        PAPER_PROVIDER_API_V1_WEAPON_MANIPULATION_TABLE_BYTES ==
        368);
    static_assert(
        PAPER_PROVIDER_API_V1_WEAPON_MOTION_DIAGNOSTICS_TABLE_BYTES ==
        376);
    static_assert(
        offsetof(PaperProviderApiV1, setDevelopmentCaptureV1) ==
        PAPER_PROVIDER_API_V1_WEAPON_MOTION_DIAGNOSTICS_TABLE_BYTES);
    static_assert(
        PAPER_PROVIDER_API_V1_DEVELOPMENT_CAPTURE_TABLE_BYTES ==
        sizeof(PaperProviderApiV1));
    static_assert(sizeof(PaperDevelopmentCaptureRequestV1) == 48);
    static_assert(sizeof(PaperDevelopmentCaptureStateV1) == 144);
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
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::ReloadStageIdentification)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::NativePosePipeline)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::DevelopmentCaptureControl)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::WeaponMotionCatalog)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::WeaponMotionDiagnostics)) != 0);
    static_assert(
        (PAPER_PROVIDER_FEATURE_BITS_V1 &
            static_cast<std::uint32_t>(
                PaperProviderFeatureBitV1::
                    WeaponManipulationTelemetry)) != 0);
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
    static_assert(
        static_cast<std::uint32_t>(
            PaperConsumerCapabilityV1::ReloadStageIdentification) ==
        (1u << 9));
    static_assert(
        static_cast<std::uint32_t>(
            PaperConsumerCapabilityV1::NativePosePipeline) ==
        (1u << 10));
    static_assert(
        (static_cast<std::uint32_t>(PaperConsumerCapabilityV1::All) &
            static_cast<std::uint32_t>(
                PaperConsumerCapabilityV1::NativePosePipeline)) != 0);
    static_assert(
        static_cast<std::uint32_t>(
            PaperConsumerCapabilityV1::WeaponMotionCatalog) ==
        (1u << 11));
    static_assert(
        static_cast<std::uint32_t>(
            PaperConsumerCapabilityV1::WeaponMotionDiagnostics) ==
        (1u << 12));
    static_assert(
        static_cast<std::uint32_t>(
            PaperConsumerCapabilityV1::WeaponManipulationTelemetry) ==
        (1u << 13));
    static_assert(sizeof(PaperNativePoseFrameStateV1) == 264);
    static_assert(sizeof(PaperNativeHandSolutionV1) == 2000);
    static_assert(
        static_cast<std::uint32_t>(
            PaperReloadStageFlagV1::SlideForward) ==
        (1u << 5));
    static_assert(
        static_cast<std::uint32_t>(
            PaperReloadStageFlagV1::BoltForward) ==
        (1u << 6));
    static_assert(
        static_cast<std::uint32_t>(
            PaperReloadStageFlagV1::BoltBack) ==
        (1u << 7));
    static_assert(
        static_cast<std::uint32_t>(
            PaperReloadStageStatusFlagV1::BoltObserved) ==
        (1u << 13));
    static_assert(
        static_cast<std::uint32_t>(
            PaperReloadStagePartFlagV1::SlideForward) ==
        (1u << 13));
    static_assert(
        static_cast<std::uint32_t>(
            PaperReloadStagePartFlagV1::BoltBack) ==
        (1u << 16));
    static_assert(
        static_cast<std::uint64_t>(
            PaperWeaponFamilyFlagV1::Shishkebab) ==
        (1ull << 63));
    static_assert(
        static_cast<std::uint32_t>(
            PaperWeaponFamilyEvidenceFlagV1::PartEvidenceIncomplete) ==
        (1u << 6));
    static_assert(sizeof(PaperWeaponClassificationV1) == 64);
    static_assert(
        offsetof(PaperWeaponClassificationV1, familyFlags) == 48);
    static_assert(
        offsetof(PaperWeaponClassificationV1, primaryFamily) == 56);
    static_assert(
        offsetof(PaperWeaponClassificationV1, familyEvidenceFlags) == 60);
    static_assert(sizeof(PaperReloadStageStateV1) == 176);
    static_assert(
        offsetof(PaperReloadStageStateV1, slideForwardCount) == 152);
    static_assert(
        offsetof(PaperReloadStageStateV1, boltBackCount) == 164);
    static_assert(sizeof(PaperReloadQsTransformV1) == 40);
    static_assert(PAPER_MAX_WEAPON_MOTION_EVENTS_V1 == 144);
    static_assert(sizeof(PaperWeaponMotionLimitsV1) == 72);
    static_assert(sizeof(PaperWeaponMotionCatalogStateV1) == 168);
    static_assert(sizeof(PaperWeaponMotionPartV1) == 524);
    static_assert(sizeof(PaperWeaponMotionStageV1) == 208);
    static_assert(sizeof(PaperWeaponMotionFollowerV1) == 128);
    static_assert(
        static_cast<std::uint32_t>(
            PaperWeaponMotionStageFlagV1::HydratedFromCache) ==
        (1u << 7));
    static_assert(
        static_cast<std::uint32_t>(
            PaperWeaponMotionFollowerFlagV1::HydratedFromCache) ==
        (1u << 5));
    static_assert(
        static_cast<std::uint32_t>(
            PaperWeaponMotionStoreFlagV1::PersistentCacheHit) ==
        (1u << 8));
    static_assert(sizeof(PaperWeaponMotionLearningStateV1) == 128);
    static_assert(sizeof(PaperWeaponMotionRecorderV1) == 168);
    static_assert(sizeof(PaperWeaponManipulationFrameStateV1) == 112);
    static_assert(sizeof(PaperWeaponManipulationHandStateV1) == 208);
    static_assert(sizeof(PaperWeaponMotionStoreStateV1) == 136);
    static_assert(
        PAPER_RELOAD_ANIMATION_SAMPLE_BUDGET_BYTES_V1 ==
        128ull * 1024ull * 1024ull);

    PaperTransformV1 identity{};
    identity.rotate[0][0] = 1.0f;
    identity.rotate[1][1] = 1.0f;
    identity.rotate[2][2] = 1.0f;
    PaperTransformV1 translated = identity;
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

    PaperTransformV1 quarterTurn = identity;
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
    const auto identityQs =
        paper::weapon_motion_policy::fromTransform(identity);
    const auto quarterTurnQs =
        paper::weapon_motion_policy::fromTransform(quarterTurn);
    expect(
        std::abs(
            paper::weapon_motion_policy::rotationDistanceRadians(
                identityQs, quarterTurnQs) -
            std::numbers::pi_v<float> * 0.5f) < 0.001f,
        "motion learning must preserve graph-output matrix rotation");

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

    expect(
        paper::weapon_motion_policy::namesMatch(
            "WeaponMagazine", "weaponmagazine:12"),
        "motion paths must map exact tracks to instanced scene names");

    std::array<
        PaperReloadQsTransformV1,
        paper::weapon_motion_policy::kMaximumStrokeSamples> motionSamples{};
    for (std::uint32_t index = 0; index < 5; ++index) {
        motionSamples[index].translate[0] = static_cast<float>(index);
    }
    std::array<
        PaperReloadQsTransformV1,
        PAPER_WEAPON_MOTION_KEY_COUNT_V1> motionKeys{};
    const auto motionLength =
        paper::weapon_motion_policy::resamplePath(
            motionSamples, 0, 4, motionKeys);
    expect(
        std::abs(motionLength - 4.0f) < 0.0001f &&
            std::abs(motionKeys.front().translate[0]) < 0.0001f &&
            std::abs(motionKeys.back().translate[0] - 4.0f) < 0.0001f,
        "learned motion paths must retain endpoints and arc length");

    std::array<
        float,
        PAPER_WEAPON_MOTION_KEY_COUNT_V1> leaderSourcePositions{};
    const auto synchronizedLength =
        paper::weapon_motion_policy::resamplePathWithSourcePositions(
            motionSamples,
            0,
            4,
            motionKeys,
            leaderSourcePositions);
    std::array<
        PaperReloadQsTransformV1,
        paper::weapon_motion_policy::kMaximumStrokeSamples> followerSamples{};
    followerSamples[0].translate[1] = 0.0f;
    followerSamples[1].translate[1] = 0.0f;
    followerSamples[2].translate[1] = 3.0f;
    followerSamples[3].translate[1] = 3.0f;
    followerSamples[4].translate[1] = 6.0f;
    std::array<
        PaperReloadQsTransformV1,
        PAPER_WEAPON_MOTION_KEY_COUNT_V1> synchronizedFollowerKeys{};
    expect(
        synchronizedLength == motionLength &&
            paper::weapon_motion_policy::sampleAtSourcePositions(
                followerSamples,
                5,
                leaderSourcePositions,
                synchronizedFollowerKeys) &&
            synchronizedFollowerKeys.front().translate[1] == 0.0f &&
            synchronizedFollowerKeys.back().translate[1] == 6.0f,
        "follower keys must use the leader's exact source-time positions");

    auto makePose = [](const float x, const float y) {
        PaperReloadQsTransformV1 value{};
        value.translate[0] = x;
        value.translate[1] = y;
        value.rotate[3] = 1.0f;
        value.scale[0] = 1.0f;
        value.scale[1] = 1.0f;
        value.scale[2] = 1.0f;
        return value;
    };
    const auto relativeLeaderStart = makePose(0.0f, 0.0f);
    const auto relativeFollowerStart = makePose(1.0f, 0.0f);
    auto relativeLeaderEnd = makePose(0.0f, 0.0f);
    const auto sine45 = std::sqrt(0.5f);
    relativeLeaderEnd.rotate[2] = sine45;
    relativeLeaderEnd.rotate[3] = sine45;
    const auto relativeFollowerEnd = paper::weapon_motion_policy::compose(
        relativeLeaderEnd, relativeFollowerStart);
    expect(
        paper::weapon_motion_policy::relativeTransformStable(
            relativeLeaderStart,
            relativeFollowerStart,
            relativeLeaderEnd,
            relativeFollowerEnd,
            0.001f,
            0.001f,
            0.001f),
        "rigid followers must preserve their full leader-relative transform");
    expect(
        !paper::weapon_motion_policy::relativeTransformStable(
            relativeLeaderStart,
            relativeFollowerStart,
            relativeLeaderEnd,
            relativeFollowerStart,
            0.001f,
            0.001f,
            0.001f),
        "translation-only coincidence must not qualify as full rigidity");

    std::array<
        PaperReloadQsTransformV1,
        PAPER_WEAPON_MOTION_KEY_COUNT_V1> overlappingMotion{};
    std::array<
        PaperReloadQsTransformV1,
        PAPER_WEAPON_MOTION_KEY_COUNT_V1> disjointMotion{};
    for (std::size_t index = 0; index < motionKeys.size(); ++index) {
        overlappingMotion[index] = makePose(
            static_cast<float>((std::min)(index, motionKeys.size() / 2)),
            0.0f);
        disjointMotion[index] = makePose(
            index < motionKeys.size() / 2 ? 0.0f :
                static_cast<float>(index - motionKeys.size() / 2),
            0.0f);
    }
    expect(
        paper::weapon_motion_policy::temporalMotionOverlap(
            overlappingMotion, overlappingMotion) > 0.99f &&
            paper::weapon_motion_policy::temporalMotionOverlap(
                overlappingMotion, disjointMotion) < 0.05f,
        "co-timed follower admission must reject disjoint motion windows");

    PaperReloadQsTransformV1 projectedInput{};
    projectedInput.translate[0] = 2.0f;
    const auto projection =
        paper::weapon_motion_policy::projectOntoPath(
            projectedInput, motionKeys);
    expect(
        projection.valid &&
            std::abs(projection.normalizedProgress - 0.5f) < 0.01f &&
            projection.residual < 0.001f,
        "manipulation telemetry must project live parts onto motion paths");

    PaperReloadQsTransformV1 authoredRest{};
    authoredRest.translate[0] = 10.0f;
    PaperReloadQsTransformV1 authoredMoved = authoredRest;
    authoredMoved.translate[0] = 13.0f;
    PaperReloadQsTransformV1 liveRest{};
    liveRest.translate[0] = 20.0f;
    const auto rebased = paper::weapon_motion_policy::rebase(
        authoredRest, liveRest, authoredMoved);
    expect(
        std::abs(rebased.translate[0] - 23.0f) < 0.0001f,
        "authored motion must rebase onto the live observation baseline");

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

    PaperTransformV1 baseline{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        baseline.rotate[axis][axis] = 1.0f;
    }
    baseline.scale = 1.0f;
    auto current = baseline;
    auto transformDelta =
        paper::reload_stage_policy::calculateTransformDelta(
            baseline,
            current);
    expect(
        paper::reload_stage_policy::isAtRest(transformDelta),
        "an unchanged weapon-local part transform must be seated at rest");
    current.translate[0] =
        paper::reload_stage_policy::
            kRestTranslationToleranceGameUnits * 2.0f;
    transformDelta =
        paper::reload_stage_policy::calculateTransformDelta(
            baseline,
            current);
    expect(
        !paper::reload_stage_policy::isAtRest(transformDelta),
        "a part beyond the public translation tolerance must be displaced");

    expect(
        !paper::reload_stage_policy::allGroupMembersMatch(0, 0),
        "an absent semantic group must not publish a completed stage");
    expect(
        !paper::reload_stage_policy::allGroupMembersMatch(4, 3),
        "a partially matching semantic group must not publish a completed stage");
    expect(
        paper::reload_stage_policy::allGroupMembersMatch(4, 4),
        "a semantic group must complete only when every member matches");

    using paper::reload_stage_policy::ActivityIdentity;
    using paper::reload_stage_policy::FireCorrelationState;
    std::array<ActivityIdentity, 1> activities{
        ActivityIdentity{ 10 },
    };
    auto fireStep =
        paper::reload_stage_policy::advanceFireCorrelation(
            FireCorrelationState{},
            100,
            4,
            0,
            activities);
    expect(
        !fireStep.active,
        "the first weapon-bound sample must not replay a stale fire event");

    activities[0].activationOrder = 11;
    fireStep = paper::reload_stage_policy::advanceFireCorrelation(
        fireStep.state,
        100,
        5,
        11,
        activities);
    expect(
        fireStep.active && fireStep.newFireEvent &&
            fireStep.activeCorrelatedCount == 1 &&
            fireStep.correlation ==
                PaperReloadFireCorrelationV1::ActivityBound,
        "a fresh handled fire event must bind its exact active clip identity");

    const std::array<ActivityIdentity, 0> noActivities{};
    fireStep = paper::reload_stage_policy::advanceFireCorrelation(
        fireStep.state,
        100,
        5,
        11,
        noActivities);
    expect(
        fireStep.active &&
            fireStep.correlation ==
                PaperReloadFireCorrelationV1::PendingActivity,
        "fire must retain its bounded activity-admission window after the event clip ends");
    for (std::uint32_t frame = 0;
         frame < paper::reload_stage_policy::
             kFireActivityAdmissionFrames;
         ++frame) {
        fireStep = paper::reload_stage_policy::advanceFireCorrelation(
            fireStep.state,
            100,
            5,
            11,
            noActivities);
    }
    expect(
        !fireStep.active,
        "event-only fire correlation must end when its bounded admission window expires");

    auto eventOnly =
        paper::reload_stage_policy::advanceFireCorrelation(
            FireCorrelationState{},
            200,
            9,
            0,
            noActivities);
    eventOnly = paper::reload_stage_policy::advanceFireCorrelation(
        eventOnly.state,
        200,
        10,
        0,
        noActivities);
    expect(
        eventOnly.active && eventOnly.newFireEvent &&
            eventOnly.correlation ==
                PaperReloadFireCorrelationV1::EventOnly,
        "a handled fire event must be visible before a clip identity is admitted");

    if (s_failures == 0) {
        std::cout << "PAPERObservationApiTests passed.\n";
    }
    return s_failures == 0 ? 0 : 1;
}
