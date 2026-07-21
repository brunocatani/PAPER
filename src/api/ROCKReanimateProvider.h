#pragma once

#include "api/ROCKReanimateApi.h"

#include <cstdint>

namespace rock_reanimate::provider
{
    void initialize();
    void shutdown();
    void beginFrame(std::uint64_t frameIndex);
    void completeFrame();
    void resetRuntime();

    void publishConfig(const api::ReanimateConfigStateV1& state);
    void publishRuntime(const api::ReanimateRuntimeStateV1& state);
    void dispatchEvent(api::ReanimateEventKindV1 kind);

    [[nodiscard]] std::uint32_t currentConsumerAuthorityFlags();
    [[nodiscard]] std::uint32_t generation();
    [[nodiscard]] bool isReady();
}
