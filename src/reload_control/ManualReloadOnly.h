#pragma once

namespace paper::manual_reload_only
{
    [[nodiscard]] bool installHook();
    [[nodiscard]] bool physicalControlReady() noexcept;
    void setRuntimeEnabled(bool enabled);
    void observePlayerReloadStart();
    void resetSession();
}
