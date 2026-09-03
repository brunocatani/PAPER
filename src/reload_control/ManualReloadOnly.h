#pragma once

namespace paper::manual_reload_only
{
    [[nodiscard]] bool installHook();
    void setRuntimeEnabled(bool enabled);
    void observePlayerReloadStart();
    void resetSession();
}
