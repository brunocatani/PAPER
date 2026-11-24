#include "mods_config/ModsConfigClient.h"

#include "PaperConfig.h"
#include "PaperLog.h"
#include "mods_config/ModsConfigFO4Api.h"

#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

namespace paper::mods_config_client
{
    namespace
    {
        using ::mods_config::api::Change;
        using ::mods_config::api::MenuEventType;
        using ::mods_config::api::ModsConfigFO4Client;
        using ::mods_config::api::Saved;
        using ::mods_config::api::Value;

        // The modName of PAPER's config spec.
        constexpr const char* kModName = "PAPER";

        /**
         * A value of the menu as the text PAPER's settings store reads; the store normalizes it.
         */
        std::string valueText(const Value& value)
        {
            return std::visit(
                [](const auto& held) -> std::string {
                    using Held = std::decay_t<decltype(held)>;
                    if constexpr (std::is_same_v<Held, bool>)
                        return held ? "true" : "false";
                    else if constexpr (std::is_same_v<Held, std::string>)
                        return held;
                    else
                        return std::format("{}", held);
                },
                value);
        }

        /**
         * A live setting's preview, or its Discard back to the file's value. The setting's TOML path is its tables,
         * then its key.
         */
        void onChange(const Change& change)
        {
            if (change.filePath.empty() || change.path.size() < 2)
                return;
            std::string table;
            for (std::size_t index = 0; index + 1 < change.path.size(); ++index) {
                if (index)
                    table += '.';
                table += change.path[index];
            }
            const auto key = change.path.back();
            if (change.discard)
                PaperConfig::discardPreviewValue(table, key);
            else
                (void)PaperConfig::setPreviewValue(table, key, valueText(change.value));
        }

        /**
         * A Save wrote PAPER.toml: the previews end and the file is read at once instead of after the watcher's
         * delay, so the saved values follow the previews without a frame of the old ones.
         */
        void onSaved(const Saved&) { PaperConfig::reloadAfterSave(); }

        /**
         * Mods Config's F4SE message after a Save, sent to each plugin of the spec's `f4sePluginRequirements`. PAPER
         * learns of Saves through `onSaved`, so it ignores the message; without a listener F4SE logs a failed dispatch.
         */
        void onModsConfigMessage(F4SE::MessagingInterface::Message*) {}

        /**
         * The menu closing ends every preview, should a Discard not have come for one.
         */
        void onMenuEvent(MenuEventType event, std::string_view)
        {
            if (event == MenuEventType::MenuClosed)
                PaperConfig::clearPreviewValues();
        }
    }

    void connect() noexcept
    {
        try {
            auto& client = ModsConfigFO4Client::get();
            client.addState("modVersion", [] { return std::optional<Value>(std::string(Version::NAME)); });
            client.subscribeToChanges(&onChange);
            client.subscribeToSaves(&onSaved);
            client.subscribeToMenuEvents(&onMenuEvent);
            if (const auto* messaging = F4SE::GetMessagingInterface())
                (void)messaging->RegisterListener(&onModsConfigMessage, ::mods_config::api::MODS_CONFIG_FO4_F4SE_NAME);
            if (const int error = client.connect(kModName); error != 0)
                PAPER_LOG_INFO(Config, "Mods Config FO4 VR not connected ({}): PAPER runs without its native API", error);
            else
                PAPER_LOG_INFO(Config, "Connected to Mods Config FO4 VR (API {})", client.apiVersion());
        } catch (...) {
            PAPER_LOG_WARN(Config, "Connecting to Mods Config FO4 VR failed");
        }
    }
}
