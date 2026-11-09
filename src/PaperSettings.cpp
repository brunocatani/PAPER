#include "PaperSettings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <format>
#include <stdexcept>
#include <string>

namespace paper::settings
{
    namespace
    {
        using RPS::Settings::Definition;
        using RPS::Settings::Group;
        using RPS::Settings::LegacyKey;
        using RPS::Settings::ValueType;

        // Ranges the loader clamps to; the help states them.
        constexpr float kHandBlendMax = 1.0f;
        constexpr float kBoltHoldCloseMax = 1.0f;
        constexpr float kBoltPullMin = 2.0f, kBoltPullMax = 30.0f;
        constexpr float kAxisMin = 1.0f, kAxisMax = 20.0f;
        constexpr float kMarkerMin = 0.25f, kMarkerMax = 5.0f;

        std::string text(bool value) { return value ? "true" : "false"; }
        std::string text(float value)
        {
            std::array<char, 32> buffer{};
            const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            return RPS::Settings::normalizeValue(ValueType::Float,
                std::string_view(buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())))
                .value_or("0.0");
        }
        std::string text(std::integral auto value) { return std::to_string(value); }

        const std::string& valueOf(const RPS::Settings::Store& store, std::string_view table, std::string_view key)
        {
            const auto* setting = store.find(table, key);
            if (!setting)
                throw std::logic_error(std::format("PAPER setting {}.{} is not declared", table, key));
            return setting->value;
        }
        bool boolean(const RPS::Settings::Store& store, std::string_view table, std::string_view key)
        {
            return valueOf(store, table, key) == "true";
        }
        float number(const RPS::Settings::Store& store, std::string_view table, std::string_view key, float fallback)
        {
            const auto& value = valueOf(store, table, key);
            float result = fallback;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{} && std::isfinite(result) ? result : fallback;
        }
        long integer(const RPS::Settings::Store& store, std::string_view table, std::string_view key, long fallback)
        {
            const auto& value = valueOf(store, table, key);
            long result = fallback;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{} ? result : fallback;
        }

        struct Old
        {
            std::string_view oldKey, table, key;
            // The default in PAPER 1.2 when it differs from today's; an INI
            // value equal to it was never chosen, so today's default applies.
            std::string_view oldDefault{};
        };
        // INI key of PAPER 1.2 and earlier, by option. Only values that differ
        // from that version's default carry over.
        constexpr std::array kOldKeys{
            Old{ "bEnabled", "Main", "Enabled" },
            Old{ "iLogLevel", "Main", "LogLevel" },
            Old{ "bManualReloadOnly", "Reload", "ManualReloadOnly", "false" },
            Old{ "fHandBlendSeconds", "Reload", "HandBlendSeconds" },
            Old{ "bBoltHoldOpen", "Reload", "BoltHoldOpen" },
            Old{ "fBoltHoldCloseSeconds", "Reload", "BoltHoldCloseSeconds" },
            Old{ "bChamberRound", "Reload", "ChamberRound", "false" },
            Old{ "bBoltAppreciation", "Reload", "BoltAppreciation" },
            Old{ "fBoltPullDistance", "Reload", "BoltPullDistance" },
            Old{ "bBoltHandFromAnimation", "Reload", "BoltHandFromAnimation" },
            Old{ "bDebugDrawNativeAnimation", "Debug", "DebugDrawNativeAnimation" },
            Old{ "bDebugDrawNativeAnimationText", "Debug", "DebugDrawNativeAnimationText" },
            Old{ "fDebugNativeAnimationAxisLength", "Debug", "DebugNativeAnimationAxisLength" },
            Old{ "fDebugNativeAnimationMarkerSize", "Debug", "DebugNativeAnimationMarkerSize" },
        };
    }

    std::vector<Definition> definitions()
    {
        const PaperConfig d{};
        constexpr auto general = "01. General";
        constexpr auto reload = "02. Reload";
        constexpr auto debug = "03. Debug Drawing";
        const auto player = Group::Player;
        return {
            { "Main", "Enabled", general, "Enables PAPER's reload coordination and manual-reload-only behavior.", player,
                ValueType::Boolean, text(d.enabled) },
            { "Main", "LogLevel", general,
                "Log file: Documents\\My Games\\Fallout4VR\\F4SE\\PAPER.log.\n"
                "0=Trace, 1=Debug, 2=Info (normal play), 3=Warning, 4=Error, 5=Critical, 6=Off.\n"
                "Includes the selected level and all more severe messages. Info includes startup, hook validation and\n"
                "reload diagnostics; Trace and Debug are more verbose. This does not enable debug drawings.",
                player, ValueType::Integer, text(d.logLevel) },

            { "Reload", "ManualReloadOnly", reload,
                "true blocks automatic reload when ammunition reaches zero, including immediate, delayed and post-fire\n"
                "requests. false allows normal automatic reload. Manual input, ROCK reload dispatch, Tactical Reload and\n"
                "reload resumption remain available either way. While true, each trigger pull on an empty gun with rounds\n"
                "to load plays one dry-fire click, except on a charging-reload gun such as the laser musket.",
                player, ValueType::Boolean, text(d.manualReloadOnly) },
            { "Reload", "HandBlendSeconds", reload,
                "Seconds the left hand takes to move from your controller or grip onto the animation when a reload, pump\n"
                "stroke or cycle takes it, and back when that ends. 0 moves it instantly. Range: 0 to 1.",
                player, ValueType::Float, text(d.handBlendSeconds) },
            { "Reload", "BoltHoldOpen", reload,
                "true leaves the bolt or slide open on an empty gun, in either hand, until a reload starts or rounds\n"
                "arrive, with the parts that move with it such as a tilting barrel. It needs a bolt on the game's standard\n"
                "WeaponBolt bone. The open position is read off screen from the weapon's own clips the first time it is\n"
                "equipped each session, so a gun none of whose clips open its bolt never holds. Manual actions such as\n"
                "bolt, lever, pump and double-barrel guns and revolvers never hold.",
                player, ValueType::Boolean, text(d.boltHoldOpen) },
            { "Reload", "BoltHoldCloseSeconds", reload,
                "Seconds the open bolt takes to return to the animation when a reload starts. 0 moves it instantly.\n"
                "Range: 0 to 1.",
                player, ValueType::Float, text(d.boltHoldCloseSeconds) },
            { "Reload", "ChamberRound", reload,
                "true keeps a round in the chamber of every gun that fires cartridges, in either hand: pistols, rifles,\n"
                "shotguns and machine guns. Energy weapons, the Gauss rifle, launchers and other guns with no case to throw\n"
                "out are left alone, and so is a gun a weapon mod has changed to such ammo. A round in the chamber when you\n"
                "reload stays there on top of the full magazine, as far as your reserve goes, except on revolvers,\n"
                "double-barrels and miniguns. Each gun keeps its rounds and chamber when you unequip it and in your saves,\n"
                "unless it sits in a stack of identical guns. The ammo counter shows a cartridge for the chamber: solid\n"
                "with a round in it, dim without.",
                player, ValueType::Boolean, text(d.chamberRound) },
            { "Reload", "BoltAppreciation", reload,
                "true makes you work the bolt of a bolt-action rifle by hand after each shot. Use the hand the rifle's own\n"
                "cycle animation uses, usually the firing hand: hold the gun with your other hand, let go of the firing\n"
                "grip and grip the bolt where the small white marker shows. Pull it all the way back to throw out the case\n"
                "and push it home to chamber the next round. The trigger does nothing until you have, and clicks if you\n"
                "pull it. The reload stays as it was. These rifles keep a chamber as ChamberRound describes, whatever that\n"
                "option says. A rifle whose cycle animation shows no hand on the bolt keeps its usual cycle. Lever guns,\n"
                "pumps and revolvers are not affected.",
                player, ValueType::Boolean, text(d.boltAppreciation) },
            { "Reload", "BoltPullDistance", reload,
                "The length of a full pull, in game units, the same on every rifle. 7 is about 9 cm. Moving your hand past\n"
                "half of it takes the bolt all the way, at the speed of the rifle's own animation. Let go and the bolt\n"
                "stays where it is, open or closed. Range: 2 to 30.",
                player, ValueType::Float, text(d.boltPullDistance) },
            { "Reload", "BoltHandFromAnimation", reload,
                "true lets the hand the rifle's cycle animation uses take the bolt the moment the rifle is drawn, placed\n"
                "and posed the way the animation does as the bolt moves. Your other hand takes it where your grip puts it,\n"
                "with the fingers closed on the bolt's surface, once the weapon's collision is built, about a second after\n"
                "equipping. false treats both hands that way.",
                player, ValueType::Boolean, text(d.boltHandFromAnimation) },

            { "Debug", "DebugDrawNativeAnimation", debug,
                "Draws PAPER's native weapon frame, native hand targets, ROCK live grip targets, applied FRIK hands and\n"
                "their target-error lines through RPS UI Framework's world drawing. Diagnostic only; off for normal play.",
                player, ValueType::Boolean, text(d.debugDrawNativeAnimation) },
            { "Debug", "DebugDrawNativeAnimationText", debug, "Adds concise mode, capture and per-hand motion and error labels.",
                player, ValueType::Boolean, text(d.debugDrawNativeAnimationText) },
            { "Debug", "DebugNativeAnimationAxisLength", debug,
                "World-space length in game units of transform axes. Range: 1 to 20.", player, ValueType::Float,
                text(d.debugNativeAnimationAxisLength) },
            { "Debug", "DebugNativeAnimationMarkerSize", debug,
                "World-space size in game units of cross markers. Range: 0.25 to 5.", player, ValueType::Float,
                text(d.debugNativeAnimationMarkerSize) },
        };
    }

    std::span<const LegacyKey> legacyKeys()
    {
        // Built once from the definitions, which own the default text.
        static const auto keys = [] {
            struct Table
            {
                std::vector<std::string> defaults;
                std::vector<LegacyKey> keys;
            } table;
            const auto all = definitions();
            table.defaults.reserve(kOldKeys.size());
            for (const auto& old : kOldKeys) {
                const auto found = std::ranges::find_if(all,
                    [&](const Definition& definition) { return definition.table == old.table && definition.key == old.key; });
                if (found == all.end())
                    throw std::logic_error(std::format("PAPER old key {} names no option", old.oldKey));
                LegacyKey key{};
                key.oldKey = old.oldKey;
                key.table = old.table;
                key.key = old.key;
                key.oldDefault = table.defaults.emplace_back(
                    old.oldDefault.empty() ? found->defaultValue : std::string(old.oldDefault));
                table.keys.push_back(key);
            }
            return table;
        }();
        return keys.keys;
    }

    std::unique_ptr<RPS::Settings::Store> makeStore(const std::filesystem::path& directory)
    {
        return std::make_unique<RPS::Settings::Store>(
            RPS::Settings::Files{ directory, "PAPER", std::string(kFileName), {},
                "## PAPER.toml - PAPER options\n"
                "## Change these in game in the RobCo PALM wheel menu, or edit this file. Changes apply while the game\n"
                "## runs.\n"
                "## Each option has its help above it. When PAPER starts it adds new options with their defaults and\n"
                "## rewrites its help lines (##). Your own notes are kept when they start with a single # on the lines\n"
                "## above an option.\n",
                {} },
            definitions(), RPS::Settings::LegacyIni{ { directory / "PAPER.ini" }, legacyKeys() });
    }

    PaperConfig fromStore(const RPS::Settings::Store& store)
    {
        const PaperConfig d{};
        PaperConfig config{};
        config.activePath = store.path(Group::Player).string();
        config.enabled = boolean(store, "Main", "Enabled");
        config.logLevel = static_cast<int>(integer(store, "Main", "LogLevel", d.logLevel));
        config.manualReloadOnly = boolean(store, "Reload", "ManualReloadOnly");
        config.handBlendSeconds =
            std::clamp(number(store, "Reload", "HandBlendSeconds", d.handBlendSeconds), 0.0f, kHandBlendMax);
        config.boltHoldOpen = boolean(store, "Reload", "BoltHoldOpen");
        config.boltHoldCloseSeconds =
            std::clamp(number(store, "Reload", "BoltHoldCloseSeconds", d.boltHoldCloseSeconds), 0.0f, kBoltHoldCloseMax);
        config.chamberRound = boolean(store, "Reload", "ChamberRound");
        config.boltAppreciation = boolean(store, "Reload", "BoltAppreciation");
        config.boltPullDistance =
            std::clamp(number(store, "Reload", "BoltPullDistance", d.boltPullDistance), kBoltPullMin, kBoltPullMax);
        config.boltHandFromAnimation = boolean(store, "Reload", "BoltHandFromAnimation");
        config.debugDrawNativeAnimation = boolean(store, "Debug", "DebugDrawNativeAnimation");
        config.debugDrawNativeAnimationText = boolean(store, "Debug", "DebugDrawNativeAnimationText");
        config.debugNativeAnimationAxisLength = std::clamp(
            number(store, "Debug", "DebugNativeAnimationAxisLength", d.debugNativeAnimationAxisLength), kAxisMin, kAxisMax);
        config.debugNativeAnimationMarkerSize = std::clamp(
            number(store, "Debug", "DebugNativeAnimationMarkerSize", d.debugNativeAnimationMarkerSize), kMarkerMin, kMarkerMax);
        return config;
    }
}
