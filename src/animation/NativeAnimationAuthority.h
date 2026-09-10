#pragma once

#include "RE/NetImmerse/NiTransform.h"

#include <array>
#include <cstdint>

namespace paper::native_animation_authority
{
    inline constexpr std::size_t kMaxCapturedTransforms = 192;
    inline constexpr std::size_t kFingerTransformCount = 15;
    inline constexpr std::size_t kCapturedTransformNameCapacity = 64;

    enum class ApplyPhase : std::uint8_t
    {
        BeforeRock,
        AfterRock,
    };

    enum class RuntimeStatusFlag : std::uint32_t
    {
        None = 0,
        HookInstalled = 1u << 0,
        RuntimeEnabled = 1u << 1,
        CaptureValid = 1u << 2,
        LocalReloadTestLeaseActive = 1u << 3,
        HookInstallFailed = 1u << 4,
        ThreadMismatch = 1u << 5,
        CaptureFault = 1u << 6,
    };

    struct RuntimeStatus
    {
        std::uint32_t effectiveFlags{ 0 };
        std::uint32_t statusFlags{ 0 };
        std::uint32_t capturedTransformCount{ 0 };
        std::uint64_t captureSequence{ 0 };
        std::uint64_t reloadStartSequence{ 0 };
        std::uint64_t reloadEndSequence{ 0 };
        std::uint64_t fireSequence{ 0 };
        std::uint64_t fireActivityOrderAtEvent{ 0 };
        bool reloadEventActive{ false };
        bool localManualCycleLeaseActive{ false };
        bool weaponFireHookReady{ false };
    };

    struct ManualCycleRockGripSnapshot
    {
        RE::NiTransform rightHandInWeapon{};
        RE::NiTransform leftSupportHandInWeapon{};
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint32_t weaponFormId{ 0 };
        bool rightValid{ false };
        bool leftPartGripStateValid{ false };
        bool leftPartGripActive{ false };
        bool leftSupportGripValid{ false };
        bool authoredLeftSupportGripActive{ false };
    };

    struct CapturedTransform
    {
        char name[kCapturedTransformNameCapacity]{};
        RE::NiTransform local{};
        std::uint32_t flags{ 0 };
        std::int32_t sourceIndex{ -1 };
        std::int32_t destinationIndex{ -1 };
    };

    struct NativeHandPose
    {
        RE::NiTransform handInWeapon{};
        std::array<RE::NiTransform, kFingerTransformCount> fingerLocals{};
        std::uint16_t fingerLocalMask{ 0 };
        std::uint64_t captureSequence{ 0 };
        bool valid{ false };
    };

    enum class DebugHandTargetMode : std::uint8_t
    {
        None = 0,
        LiveGripDelta,
        NativeWeaponRelative,
    };

    struct DebugHandSnapshot
    {
        RE::NiTransform nativeHandInWeapon{};
        RE::NiTransform nativeBaselineHandInWeapon{};
        RE::NiTransform liveBaselineHandInWeapon{};
        RE::NiTransform resolvedHandInWeapon{};
        RE::NiTransform resolvedHandWorld{};
        std::array<RE::NiTransform, kFingerTransformCount>
            nativeFingerLocals{};
        std::uint16_t nativeFingerLocalMask{ 0 };
        DebugHandTargetMode targetMode{ DebugHandTargetMode::None };
        float motionTranslationGameUnits{ 0.0f };
        float motionRotationDegrees{ 0.0f };
        bool nativeHandValid{ false };
        bool nativeBaselineValid{ false };
        bool liveBaselineValid{ false };
        bool resolvedHandInWeaponValid{ false };
        bool resolvedHandWorldValid{ false };
        bool motionGateApplicable{ false };
        bool motionQualified{ false };
        bool visualAuthorityPublished{ false };
    };

    struct DebugAuthoritySnapshot
    {
        RuntimeStatus runtime{};
        RE::NiTransform controllerWeaponWorld{};
        RE::NiTransform nativeBaselineWeaponWorld{};
        RE::NiTransform desiredWeaponWorld{};
        DebugHandSnapshot rightHand{};
        DebugHandSnapshot leftHand{};
        std::uint64_t frameCaptureSequence{ 0 };
        bool controllerWeaponValid{ false };
        bool nativeBaselineWeaponValid{ false };
        bool desiredWeaponValid{ false };
        bool frameCaptureReady{ false };
        bool frameCapturePrepared{ false };
        bool weaponFixedHandsExpected{ false };
        bool partialReloadExpected{ false };
        bool weaponFixedHandsApplied{ false };
        bool beforeRockApplicationAttempted{ false };
        bool beforeRockApplicationSucceeded{ false };
        bool afterRockApplicationAttempted{ false };
        bool afterRockApplicationSucceeded{ false };
    };

    // Install the verified reload lifecycle and WeaponFire hooks only after
    // ROCK reports skeleton-ready. ROCK owns the shared native graph-output
    // detour and invokes captureNativeGraphOutput through its V1 phase API.
    [[nodiscard]] bool installEventHooks();
    void captureNativeGraphOutput();

    void setRuntimeEnabled(bool enabled);
    void setLocalManualCycleTestEnabled(bool enabled);
    void setLocalReloadTestEnabled(bool enabled);
    void setLocalReloadPartialAuthorityEnabled(bool enabled);
    void setManualCycleHandAnimationEligible(bool eligible);
    void setManualCycleRockGripSnapshot(
        const ManualCycleRockGripSnapshot& snapshot);

    void beginRockFrame(float deltaSeconds);
    [[nodiscard]] bool applyCapturedPose(ApplyPhase phase);
    void completeRockFrame();
    void resetTransientState();

    [[nodiscard]] bool isHookInstalled();
    [[nodiscard]] RuntimeStatus queryRuntimeStatus();
    [[nodiscard]] std::uint32_t currentLocalAuthorityFlags();
    [[nodiscard]] std::uint32_t copyCapturedTransforms(
        CapturedTransform* outTransforms,
        std::uint32_t maxTransforms);
    [[nodiscard]] bool queryNativeHandPose(bool left, NativeHandPose& outPose);
    // Game-thread-only diagnostic snapshot. Values are copied and contain no
    // scene pointers, so callers may use them only for the current publication.
    [[nodiscard]] bool queryDebugAuthoritySnapshot(
        DebugAuthoritySnapshot& outSnapshot);
}
