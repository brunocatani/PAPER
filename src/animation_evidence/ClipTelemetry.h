#pragma once

#include <cstdint>
#include <memory>

namespace spdlog { class logger; }

namespace paper::clip_telemetry
{
    // Debug-only, first-person clip evidence for the cycle trace. Installs the
    // clip generator activate/deactivate hooks the first time a target is set,
    // writes to the cycle trace's asynchronous logger and never requests
    // animation authority. A zero form ID or null logger clears the target.
    void refreshHandActionTrace(
        std::uint32_t weaponFormId, std::uint64_t weaponGenerationKey,
        std::uintptr_t weaponNode, const std::shared_ptr<spdlog::logger>& log);
}
