#pragma once

namespace paper::manual_reload_only
{
    [[nodiscard]] bool installHook();
    [[nodiscard]] bool physicalControlReady() noexcept;
    [[nodiscard]] bool installPhysicalCycleHook() noexcept;
    [[nodiscard]] bool physicalCycleReady() noexcept;
    void setRuntimeEnabled(bool enabled);
    [[nodiscard]] bool runtimeEnabled() noexcept;
    void observePlayerReloadStart();
    void resetSession();
}
