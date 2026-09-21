#pragma once

#include <atomic>
#include <cstdint>

namespace paper::tactical_reload_bridge
{
    enum class SessionAction : std::uint8_t { None, Reset, Initialize };

    class SessionRequests
    {
    public:
        // Session messages publish intent only. A newer Initialize supersedes
        // a reset for the departed save; initialization normalizes the loaded
        // save's keyword with its own native-reload checks.
        void request(SessionAction action) noexcept { _pending.store(action, std::memory_order_release); }
        [[nodiscard]] bool pending() const noexcept { return _pending.load(std::memory_order_acquire) != SessionAction::None; }
        SessionAction take() noexcept { return _pending.exchange(SessionAction::None, std::memory_order_acq_rel); }

        bool beginFrame(std::uint32_t thread) noexcept
        {
            if (!thread) return false;
            std::uint32_t expected = 0;
            return _owner.compare_exchange_strong(expected, thread, std::memory_order_acq_rel) || expected == thread;
        }
        [[nodiscard]] std::uint32_t owner() const noexcept { return _owner.load(std::memory_order_acquire); }

    private:
        std::atomic<SessionAction> _pending{ SessionAction::None };
        std::atomic<std::uint32_t> _owner{ 0 };
    };
}
