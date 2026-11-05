#pragma once

#include <cstdint>

namespace paper::manual_reload_only
{
    // Fallout4.esm's WPNRifleFireDry, once per call.
    void playDryFire();
    // A sound descriptor form, heard at the player. Silent when the native call
    // failed its check or the form is absent.
    void playSound(std::uint32_t soundForm);
    [[nodiscard]] bool installHook();
    [[nodiscard]] bool physicalControlReady() noexcept;
    [[nodiscard]] bool installPhysicalCycleHook() noexcept;
    [[nodiscard]] bool physicalCycleReady() noexcept;
    void setRuntimeEnabled(bool enabled);
    [[nodiscard]] bool runtimeEnabled() noexcept;
    void observePlayerReloadStart();
    void resetSession();
}
