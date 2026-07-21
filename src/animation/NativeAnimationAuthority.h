#pragma once

#include "RE/NetImmerse/NiTransform.h"

#include <array>
#include <cstdint>

namespace rock_reanimate::native_animation_authority
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
        bool reloadEventActive{ false };
        bool localManualCycleLeaseActive{ false };
    };

    struct ManualCycleRockGripBaselines
    {
        RE::NiTransform rightHandInWeapon{};
        RE::NiTransform leftHandInWeapon{};
        bool rightValid{ false };
        bool leftValid{ false };
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

    // Install the verified reload lifecycle and WeaponFire hooks only after
    // ROCK reports skeleton-ready. ROCK owns the shared native graph-output
    // detour and invokes captureNativeGraphOutput through its V1 phase API.
    [[nodiscard]] bool installEventHooks();
    void captureNativeGraphOutput();

    void setRuntimeEnabled(bool enabled);
    void setLocalManualCycleTestEnabled(bool enabled);
    void setLocalReloadTestEnabled(bool enabled);
    void setLocalReloadPartialAuthorityEnabled(bool enabled);
    void setManualCycleTwoHandAuthorityActive(bool active);
    void setManualCycleRockGripBaselines(
        const ManualCycleRockGripBaselines& baselines);

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
}
