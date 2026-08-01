#pragma once

#include "animation_evidence/AnimationEvidenceTypes.h"

#include <array>
#include <cstdint>

namespace paper::clip_telemetry
{
    enum class StepResult : std::uint32_t
    {
        Pending = 0,
        Completed = 1,
    };

    [[nodiscard]] const void* managerFromWeaponHolder(
        const void* weaponGraphHolder);
    StepResult stepCapture(
        const void* graphManager,
        std::uint32_t weaponFormId,
        std::uint64_t weaponGenerationKey,
        const char* const* allowedNodeNames,
        std::uint32_t allowedNodeNameCount);
    void resetWalk();
    void restartWalkPass();
    [[nodiscard]] const char* lastResolvePoint();
    [[nodiscard]] bool probeBindings(const void* graphManager);

    [[nodiscard]] bool ensureHooksInstalled();
    void setTargets(
        const void* const* graphManagers,
        std::uint32_t managerCount,
        const char* const* allowedNodeNames,
        std::uint32_t allowedNodeNameCount,
        std::uint32_t weaponFormId,
        std::uint64_t weaponGenerationKey);
    void clearTargets();

    struct CapturedAnnotation
    {
        float timeSeconds{ 0.0f };
        std::array<char, animation_evidence::kBoneNameCapacity> trackName{};
        std::array<char, animation_evidence::kMarkerTextCapacity> text{};
    };

    struct CapturedTrigger
    {
        float localTimeSeconds{ 0.0f };
        std::int32_t eventId{ -1 };
        std::array<char, animation_evidence::kMarkerTextCapacity> eventName{};
    };

    struct CapturePacket
    {
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t activityId{ 0 };
        animation_evidence::ClipAcquisition acquisition{
            animation_evidence::ClipAcquisition::LoadedGraphBinding
        };
        std::array<char, animation_evidence::kBoneNameCapacity> animationName{};
        float durationSeconds{ 0.0f };
        std::uint32_t rawTransformTrackCount{ 0 };
        std::uint32_t capturedWeaponTrackCount{ 0 };
        bool weaponTracksTruncated{ false };
        std::array<
            animation_evidence::LiveTrackSamples,
            animation_evidence::kMaxLiveTracksPerClip>
            weaponTracks{};
        std::int32_t rawAnnotationTrackCount{ 0 };
        std::int32_t rawTriggerCount{ 0 };
        std::int32_t graphEventNameCount{ 0 };
        std::uint32_t annotationCount{ 0 };
        std::uint32_t triggerCount{ 0 };
        bool annotationsTruncated{ false };
        bool triggersTruncated{ false };
        std::array<
            CapturedAnnotation,
            animation_evidence::kMaxAnnotationsPerClip>
            annotations{};
        std::array<
            CapturedTrigger,
            animation_evidence::kMaxTriggersPerClip>
            triggers{};
    };
    static_assert(sizeof(CapturePacket) < 512 * 1024);

    void setCaptureEnabled(bool enabled);
    void clearActivities();
    [[nodiscard]] std::uint32_t drainCaptures(
        CapturePacket* outPackets,
        std::uint32_t maxPackets);

    struct DropInfo
    {
        std::uint64_t count{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
    };
    [[nodiscard]] DropInfo drainDropInfo();

    struct ActivityState
    {
        bool active{ false };
        std::uint32_t concurrentActivityCount{ 0 };
        std::uint64_t activityId{ 0 };
        std::uint64_t activationOrder{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::array<char, animation_evidence::kBoneNameCapacity> animationName{};
        float durationSeconds{ 0.0f };
        float cropStartSeconds{ 0.0f };
        float croppedDurationSeconds{ 0.0f };
        float localTimeSeconds{ 0.0f };
        float fraction{ 0.0f };
    };
    [[nodiscard]] ActivityState activityState();
    [[nodiscard]] std::uint32_t copyActivityStates(
        ActivityState* outStates,
        std::uint32_t maxStates);

    struct Stats
    {
        std::uint64_t bindingsSeen{ 0 };
        std::uint64_t bindingsCaptured{ 0 };
        std::uint64_t bindingsWithoutTargets{ 0 };
        std::uint64_t skippedNonSpline{ 0 };
        std::uint64_t walksCompleted{ 0 };
        std::uint64_t rejectedAnimationPointer{ 0 };
        std::uint64_t rejectedClipParameters{ 0 };
        std::uint64_t rejectedTrackMap{ 0 };
        std::uint64_t rejectedBoneCount{ 0 };
        std::uint64_t rejectedSampler{ 0 };
        std::uint64_t rejectedSplineData{ 0 };
        std::uint64_t hookCalls{ 0 };
        std::uint64_t hookMatches{ 0 };
    };
    [[nodiscard]] Stats snapshotStats();
}
