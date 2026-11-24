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

        // The store's values with the overrides laid over them.
        struct Values
        {
            const RPS::Settings::Store& store;
            const RPS::Settings::LoadedValues& overrides;
        };

        const std::string& valueOf(const Values& values, std::string_view table, std::string_view key)
        {
            const auto* setting = values.store.find(table, key);
            if (!setting)
                throw std::logic_error(std::format("PAPER setting {}.{} is not declared", table, key));
            if (const auto found = values.overrides.find(RPS::Settings::settingPath(table, key));
                found != values.overrides.end())
                return found->second;
            return setting->value;
        }
        bool boolean(const Values& values, std::string_view table, std::string_view key)
        {
            return valueOf(values, table, key) == "true";
        }
        float number(const Values& values, std::string_view table, std::string_view key, float fallback)
        {
            const auto& value = valueOf(values, table, key);
            float result = fallback;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{} && std::isfinite(result) ? result : fallback;
        }
        long integer(const Values& values, std::string_view table, std::string_view key, long fallback)
        {
            const auto& value = valueOf(values, table, key);
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
            Old{ "iLogLevel", "Main", "LogLevel" },
            Old{ "bManualReloadOnly", "Reload", "ManualReloadOnly", "false" },
            Old{ "fHandBlendSeconds", "Reload", "HandBlendSeconds" },
            Old{ "bBoltHoldOpen", "Reload", "SlideOpenOnEmpty" },
            Old{ "fBoltHoldCloseSeconds", "Reload", "SlideCloseSeconds" },
            Old{ "bChamberRound", "Reload", "ChamberRound", "false" },
            Old{ "bBoltAppreciation", "BoltAppreciation", "BoltAppreciation" },
            Old{ "fBoltPullDistance", "BoltAppreciation", "BoltPullDistance" },
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
        constexpr auto bolt = "03. Bolt Appreciation";
        constexpr auto debug = "04. Debug Drawing";
        const auto player = Group::Player;
        // The first help line is the option's title, which its row on PAPER's Mods Config page
        // (data/mod/ModsConfigFO4/Config/PAPER/spec.json) shares; choices are "N = label" pairs.
        // PAPER has no switch of its own: installed, it runs.
        return {
            { "Main", "LogLevel", general,
                "Log Level\nHow much PAPER writes to Documents\\My Games\\Fallout4VR\\F4SE\\PAPER.log.\n"
                "0 = Trace. 1 = Debug. 2 = Info. 3 = Warning. 4 = Error. 5 = Critical. 6 = Off.\n"
                "Info is for normal play: startup, hook checks and reload diagnostics. Trace and Debug write much more. "
                "This does not turn on debug drawing.",
                player, ValueType::Integer, text(d.logLevel) },

            { "Reload", "ManualReloadOnly", reload,
                "Manual Reload Only\nAn empty gun does not reload by itself, whether the reload would start at once, "
                "after a delay or after a shot; you reload it.\nManual input, ROCK's reload, Tactical Reload and "
                "resuming a reload work either way. Each trigger pull on an empty gun with rounds to load plays one "
                "dry-fire click, except on a charging-reload gun such as the laser musket.\n"
                "False: the game reloads by itself as usual.",
                player, ValueType::Boolean, text(d.manualReloadOnly) },
            { "Reload", "SlideOpenOnEmpty", reload,
                "Slide Open on Empty\nWhen a gun runs empty, its slide or bolt locks back and stays open, in either "
                "hand, until a reload starts or rounds arrive, with the parts that move with it such as a tilting "
                "barrel.\nWorks with Manual Reload Only: without it the game reloads an empty gun by itself, which "
                "closes it again.\nThe open position comes from the gun's own animations, read the first time it is "
                "equipped each session, so a gun whose animations never open its slide or bolt stays closed. Bolt, "
                "lever, pump and double-barrel guns and revolvers are not affected.\nFalse: the slide and bolt follow "
                "the animation.",
                player, ValueType::Boolean, text(d.slideOpenOnEmpty) },
            { "Reload", "ChamberRound", reload,
                "Round in the Chamber\nEvery gun that fires cartridges keeps a round in the chamber, in either hand: "
                "pistols, rifles, shotguns and machine guns.\nA round in the chamber when you reload stays there on "
                "top of the full magazine, as far as your reserve goes, except on revolvers, double-barrels and "
                "miniguns. Each gun keeps its rounds and chamber when you unequip it and in your saves, unless it "
                "sits in a stack of identical guns. The ammo counter shows a cartridge for the chamber: solid with a "
                "round in it, dim without.\nEnergy weapons, the Gauss rifle, launchers and other guns with no case "
                "to throw out are left alone, and so is a gun a weapon mod has changed to such ammo.\nFalse: the "
                "game's own magazine count.",
                player, ValueType::Boolean, text(d.chamberRound) },
            { "Reload", "HandBlendSeconds", reload,
                "Hand Blend Time\nSeconds the left hand takes to move from your controller or grip onto the animation "
                "when a reload, pump stroke or cycle takes it, and back when that ends. 0 moves it instantly. "
                "Range: 0 to 1.",
                player, ValueType::Float, text(d.handBlendSeconds) },
            { "Reload", "SlideCloseSeconds", reload,
                "Slide Close Time\nSeconds an open slide or bolt takes to close when a reload starts. 0 closes it "
                "instantly. Range: 0 to 1.",
                player, ValueType::Float, text(d.slideCloseSeconds) },

            { "BoltAppreciation", "BoltAppreciation", bolt,
                "Bolt Appreciation\nWork the bolt of a bolt-action rifle by hand after each shot.\nUse the hand the "
                "rifle's own cycle animation uses, usually the firing hand: hold the gun with your other hand, let go "
                "of the firing grip and grip the bolt where the small white marker shows. That hand takes the bolt "
                "the way the animation does from the moment the rifle is drawn; your other hand can take it too, once "
                "the weapon's collision is built. Pull it all the way back to "
                "throw out the case and push it home to chamber the next round. The trigger does nothing until you "
                "have, and clicks if you pull it. The reload stays as it was.\nThese rifles keep a chamber whatever "
                "Round in the Chamber says. A rifle whose cycle animation shows no hand on the bolt keeps its usual "
                "cycle. Lever guns, pumps and revolvers are not affected.\nFalse: the rifle cycles by itself.",
                player, ValueType::Boolean, text(d.boltAppreciation) },
            { "BoltAppreciation", "BoltPullDistance", bolt,
                "Bolt Pull Distance\nThe length of a full bolt pull in game units, the same on every rifle; 7 is "
                "about 9 cm.\nMoving your hand past half of it takes the bolt all the way, at the speed of the "
                "rifle's own animation. Let go and the bolt stays where it is, open or closed. Range: 2 to 30.",
                player, ValueType::Float, text(d.boltPullDistance) },

            { "Debug", "DebugDrawNativeAnimation", debug,
                "Draw Native Animation\nDraws PAPER's native weapon frame, native hand targets, ROCK's live grip "
                "targets, the applied FRIK hands and their target-error lines through RPS UI Framework's world "
                "drawing. Diagnostic only; off for normal play.",
                player, ValueType::Boolean, text(d.debugDrawNativeAnimation) },
            { "Debug", "DebugDrawNativeAnimationText", debug,
                "Native Animation Labels\nAdds short mode, capture and per-hand motion and error labels.",
                player, ValueType::Boolean, text(d.debugDrawNativeAnimationText) },
            { "Debug", "DebugNativeAnimationAxisLength", debug,
                "Axis Length\nLength of drawn transform axes in game units. Range: 1 to 20.", player,
                ValueType::Float, text(d.debugNativeAnimationAxisLength) },
            { "Debug", "DebugNativeAnimationMarkerSize", debug,
                "Marker Size\nSize of drawn cross markers in game units. Range: 0.25 to 5.", player,
                ValueType::Float, text(d.debugNativeAnimationMarkerSize) },
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
                "## Change these in game on PAPER's page in Mods Config FO4 VR, or edit this file. Changes apply\n"
                "## while the game runs.\n"
                "## Each option has its help above it. When PAPER starts it adds new options with their defaults and\n"
                "## rewrites its help lines (##). Your own notes are kept when they start with a single # on the lines\n"
                "## above an option.\n",
                {} },
            definitions(), RPS::Settings::LegacyIni{ { directory / "PAPER.ini" }, legacyKeys() });
    }

    PaperConfig fromStore(const RPS::Settings::Store& store, const RPS::Settings::LoadedValues& overrides)
    {
        const PaperConfig d{};
        PaperConfig config{};
        const Values values{ store, overrides };
        config.activePath = store.path(Group::Player).string();
        config.logLevel = static_cast<int>(integer(values, "Main", "LogLevel", d.logLevel));
        config.manualReloadOnly = boolean(values, "Reload", "ManualReloadOnly");
        config.handBlendSeconds =
            std::clamp(number(values, "Reload", "HandBlendSeconds", d.handBlendSeconds), 0.0f, kHandBlendMax);
        config.slideOpenOnEmpty = boolean(values, "Reload", "SlideOpenOnEmpty");
        config.slideCloseSeconds =
            std::clamp(number(values, "Reload", "SlideCloseSeconds", d.slideCloseSeconds), 0.0f, kBoltHoldCloseMax);
        config.chamberRound = boolean(values, "Reload", "ChamberRound");
        config.boltAppreciation = boolean(values, "BoltAppreciation", "BoltAppreciation");
        config.boltPullDistance =
            std::clamp(number(values, "BoltAppreciation", "BoltPullDistance", d.boltPullDistance), kBoltPullMin, kBoltPullMax);
        config.debugDrawNativeAnimation = boolean(values, "Debug", "DebugDrawNativeAnimation");
        config.debugDrawNativeAnimationText = boolean(values, "Debug", "DebugDrawNativeAnimationText");
        config.debugNativeAnimationAxisLength = std::clamp(
            number(values, "Debug", "DebugNativeAnimationAxisLength", d.debugNativeAnimationAxisLength), kAxisMin, kAxisMax);
        config.debugNativeAnimationMarkerSize = std::clamp(
            number(values, "Debug", "DebugNativeAnimationMarkerSize", d.debugNativeAnimationMarkerSize), kMarkerMin, kMarkerMax);
        return config;
    }
}
