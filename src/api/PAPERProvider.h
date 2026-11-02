#pragma once

#include <PAPER/Provider.h>

#include <cstdint>

namespace paper::provider
{
    void initialize();
    void shutdown();
    void beginFrame(std::uint64_t frameIndex);
    void completeFrame();
    void resetRuntime();

    void configureDevelopmentCapture(
        api::DevelopmentCaptureMode mode,
        api::WeaponMotionCacheAccess cacheAccess,
        std::uint32_t configFlags,
        std::uint64_t configRevision);
    [[nodiscard]] std::uint32_t refreshDevelopmentCaptureDemand(
        std::uint32_t legacyRequestedScopes);
    void publishDevelopmentCaptureScopes(std::uint32_t activeScopes);
    [[nodiscard]] std::uint32_t allowedDevelopmentCaptureScopes();

    void publishConfig(const api::ConfigState& state);
    void publishRuntime(const api::RuntimeState& state);
    void publishNativePosePipeline(
        const api::NativePoseFrameState& frame,
        const api::NativeHandSolution& rightHand,
        const api::NativeHandSolution& leftHand);
    void clearNativePosePipeline();
    void dispatchEvent(api::EventKind kind);
    void dispatchEvent(const api::Event& eventData);

    [[nodiscard]] bool hasConsumerCapability(
        api::ConsumerCapability capability);
    [[nodiscard]] std::uint32_t currentConsumerAuthorityFlags();
    [[nodiscard]] std::uint32_t generation();
    [[nodiscard]] bool isReady();
    // Owner-thread only; validates the same live equipped item as admission.
    [[nodiscard]] bool physicalReloadActive() noexcept;
    [[nodiscard]] bool physicalManualCycleActive() noexcept;
}
