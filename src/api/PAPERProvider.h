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

    void publishConfig(const api::ConfigState& state);
    void publishRuntime(const api::RuntimeState& state);
    void publishNativePosePipeline(
        const api::NativePoseFrameState& frame,
        const api::NativeHandSolution& rightHand,
        const api::NativeHandSolution& leftHand);
    void clearNativePosePipeline();
    void dispatchEvent(api::EventKind kind);

    [[nodiscard]] bool hasConsumerCapability(
        api::ConsumerCapability capability);
    [[nodiscard]] std::uint32_t currentConsumerAuthorityFlags();
    [[nodiscard]] std::uint32_t generation();
    [[nodiscard]] bool isReady();
    // Owner-thread only; validates the same live equipped item as admission.
    [[nodiscard]] bool physicalReloadActive() noexcept;
    [[nodiscard]] bool physicalManualCycleActive() noexcept;
}
