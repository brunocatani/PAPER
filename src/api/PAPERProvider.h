#pragma once

#include "api/PAPERApi.h"

#include <cstdint>

namespace paper::provider
{
    void initialize();
    void shutdown();
    void beginFrame(std::uint64_t frameIndex);
    void completeFrame();
    void resetRuntime();

    void configureDevelopmentCapture(
        api::PaperDevelopmentCaptureModeV1 mode,
        api::PaperWeaponMotionCacheAccessV1 cacheAccess,
        std::uint32_t configFlags,
        std::uint64_t configRevision);
    [[nodiscard]] std::uint32_t refreshDevelopmentCaptureDemand(
        std::uint32_t legacyRequestedScopes);
    void publishDevelopmentCaptureScopes(std::uint32_t activeScopes);
    [[nodiscard]] std::uint32_t allowedDevelopmentCaptureScopes();

    void publishConfig(const api::PaperConfigStateV1& state);
    void publishRuntime(const api::PaperRuntimeStateV1& state);
    void publishNativePosePipeline(
        const api::PaperNativePoseFrameStateV1& frame,
        const api::PaperNativeHandSolutionV1& rightHand,
        const api::PaperNativeHandSolutionV1& leftHand);
    void clearNativePosePipeline();
    void dispatchEvent(api::PaperEventKindV1 kind);
    void dispatchEvent(const api::PaperEventV1& eventData);

    [[nodiscard]] bool hasConsumerCapability(
        api::PaperConsumerCapabilityV1 capability);
    [[nodiscard]] std::uint32_t currentConsumerAuthorityFlags();
    [[nodiscard]] std::uint32_t generation();
    [[nodiscard]] bool isReady();
}
