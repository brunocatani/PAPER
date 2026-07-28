#pragma once

namespace paper::tactical_reload_bridge
{
    void initializeSession();
    void resetSession();

    [[nodiscard]] bool contractReady();
    [[nodiscard]] bool installInputHook();

    void setRuntimeEnabled(bool enabled);
    void beginFrame(float deltaSeconds);
    void notifyPlayerReloadStart();
    void notifyPlayerReloadEnd();
}
