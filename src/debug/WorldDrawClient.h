#pragma once

#include "PaperLog.h"
#include "RPSUIWorldDrawApi.h"

#include <cstdint>
#include <cstdio>

namespace paper::debug_visualization
{
    // RPS UI Framework draws PAPER's optional native-animation diagnostics.
    // Without it the diagnostics stay hidden and PAPER runs normally. Game
    // thread (ROCK animation phases) only. The RPS UI owner is registered on
    // first use and kept for the process lifetime; no callback is installed,
    // so RPS UI never calls back into this object.
    class WorldDrawClient
    {
    public:
        [[nodiscard]] bool submit(const rpsui::sdk::WorldDrawPublicationV1& publication) noexcept
        {
            return bind() && _api->submit(_owner, &publication) == rpsui::sdk::ResultV1::Ok;
        }

        void clear() noexcept
        {
            if (_api && _owner) (void)_api->clear(_owner);
        }

    private:
        [[nodiscard]] bool bind() noexcept
        {
            if (_api && _owner) return true;
            // About one attempt per second at 90 Hz while RPS UI is absent or refuses.
            if (_retryCountdown > 0) {
                --_retryCountdown;
                return false;
            }
            _retryCountdown = kBindRetryCalls;
            const auto* registry = rpsui::sdk::RequestApiV1();
            const auto* draw = registry ? rpsui::sdk::RequestWorldDrawApiV1() : nullptr;
            if (!draw) {
                if (!_unavailableLogged) {
                    _unavailableLogged = true;
                    PAPER_LOG_WARN(Api, "Native-animation debug drawing unavailable: RPS UI Framework world drawing v1 is not loaded");
                }
                return false;
            }
            rpsui::sdk::ConsumerRegistrationV1 registration{};
            std::snprintf(registration.consumerId, sizeof(registration.consumerId), "%s", "paper.debug");
            std::snprintf(registration.displayName, sizeof(registration.displayName), "%s", "PAPER native-animation diagnostics");
            rpsui::sdk::ConsumerHandleV1 handle{};
            const auto result = registry->registerConsumer(&registration, &handle);
            if (result != rpsui::sdk::ResultV1::Ok || handle.ownerToken == 0) {
                if (!_unavailableLogged) {
                    _unavailableLogged = true;
                    PAPER_LOG_WARN(Api, "Native-animation debug drawing unavailable: RPS UI consumer registration refused ({})",
                        static_cast<int>(result));
                }
                return false;
            }
            _api = draw;
            _owner = handle.ownerToken;
            PAPER_LOG_INFO(Api, "Native-animation debug drawing renders through RPS UI Framework (owner {})", _owner);
            return true;
        }

        static constexpr std::uint32_t kBindRetryCalls = 90;
        const rpsui::sdk::WorldDrawApiV1* _api{};
        std::uint32_t _retryCountdown{};
        std::uint64_t _owner{};
        bool _unavailableLogged{};
    };
}
