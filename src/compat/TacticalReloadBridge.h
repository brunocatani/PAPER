#pragma once

namespace paper::tactical_reload_bridge
{
    // F4SE message threads request changes; prepareFrame applies them on the
    // ROCK owner thread before hook installation or bridge state access.
    void initializeSession();
    void resetSession();
    void prepareFrame();

    [[nodiscard]] bool contractReady();
    [[nodiscard]] bool installInputHook();

    void setRuntimeEnabled(bool enabled);
    void beginFrame(float deltaSeconds);
    void notifyPlayerReloadStart();
    void notifyPlayerReloadEnd();
}
