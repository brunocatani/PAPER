#pragma once

#include "PaperLog.h"
#include "api/RequiredUpdates.h"
#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/WorldDraw.h>
#include <Windows.h>

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
        [[nodiscard]] bool submit(const rpsui::api::world_draw::Publication& publication) noexcept
        {
            return bind() && _api->submit(_owner, &publication) == rpsui::api::Result::Ok;
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
            const auto module = GetModuleHandleW(rpsui::api::kModuleName);
            const auto query = module ?
                reinterpret_cast<rpsui::api::QueryInterfaceV1>(GetProcAddress(module, rpsui::api::kQueryExportName)) : nullptr;
            const rpsui::api::framework::Api* registry{};
            const rpsui::api::world_draw::Api* draw{};
            if (rpsui::api::acquire(query, registry) != rpsui::api::Result::Ok ||
                rpsui::api::acquire(query, draw) != rpsui::api::Result::Ok) {
                if (!_unavailableLogged) {
                    _unavailableLogged = true;
                    PAPER_LOG_WARN(Api, "Native-animation debug drawing unavailable: RPS UI Framework world drawing 2.0 is not loaded");
                    if (module && !query) required_updates::noteRpsUiWithoutDiscovery();
                }
                return false;
            }
            rpsui::api::framework::ConsumerRegistration registration{};
            std::snprintf(registration.consumerId, sizeof(registration.consumerId), "%s", "paper.debug");
            std::snprintf(registration.displayName, sizeof(registration.displayName), "%s", "PAPER native-animation diagnostics");
            rpsui::api::framework::ConsumerHandle handle{};
            const auto result = registry->registerConsumer(&registration, &handle);
            if (result != rpsui::api::Result::Ok || handle.ownerToken == 0) {
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
        const rpsui::api::world_draw::Api* _api{};
        std::uint32_t _retryCountdown{};
        std::uint64_t _owner{};
        bool _unavailableLogged{};
    };
}
