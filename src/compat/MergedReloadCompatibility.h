#pragma once

namespace spdlog { class logger; }

namespace paper::merged_reload_compatibility
{
    bool installHook();
    void setRuntimeEnabled(bool enabled);
    // Borrowed after successful installation; the module owns the bounded
    // worker and shared sinks for its lifetime. Callers enqueue copied values.
    [[nodiscard]] spdlog::logger* diagnosticLogger() noexcept;
}
