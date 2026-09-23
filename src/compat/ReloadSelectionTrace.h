#pragma once

namespace paper::reload_selection_trace
{
    bool installHook();
    void setRuntimeEnabled(bool enabled);
    // Called only for a player clip already matched by hand-action telemetry.
    // Correlation lasts strictly within the native dispatch on this thread.
    void observeClip(const char* name) noexcept;
}
