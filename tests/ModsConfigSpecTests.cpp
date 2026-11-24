// The Mods Config FO4 VR page (data/mod/ModsConfigFO4/Config/PAPER/spec.json)
// against PAPER's settings catalog: the spec names PAPER, its DLL and
// PAPER.toml; every setting it shows is a PAPER.toml option with PAPER's own
// type and compiled default; every option is on the page or deliberately left
// out; and each slider spans exactly the range the loader accepts. Help text
// is not checked.
#include "PaperSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using nlohmann::json;
    using RPS::Settings::Definition;
    using RPS::Settings::ValueType;

    // PAPER.toml options the page does not show, as "Table.Key".
    constexpr std::array<std::string_view, 0> kLeftOut{};

    void require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool nearlyEqual(double left, double right)
    {
        return std::abs(left - right) <= 1e-6 * (std::max)({ 1.0, std::abs(left), std::abs(right) });
    }

    /**
     * Every row with a storage, from the front page, its sections and every page.
     */
    void collectSettings(const json& node, std::vector<const json*>& settings)
    {
        if (node.is_array()) {
            for (const auto& item : node)
                collectSettings(item, settings);
            return;
        }
        if (!node.is_object())
            return;
        require(!node.contains("defaultFile") || node.at("defaultFile") == "config",
            "a page or section edits a file other than PAPER.toml");
        if (node.contains("storage"))
            settings.push_back(&node);
        for (const char* child : { "content", "pages" }) {
            if (node.contains(child))
                collectSettings(node.at(child), settings);
        }
    }

    std::string_view specType(ValueType type)
    {
        switch (type) {
        case ValueType::Boolean: return "bool";
        case ValueType::Integer: return "int";
        case ValueType::Float: return "float";
        case ValueType::String: return "string";
        default: return "";
        }
    }

    /**
     * The values PAPER runs with when one float option is set to a value, as a preview sets it.
     */
    paper::PaperConfig valuesWith(const RPS::Settings::Store& store, const Definition& definition, double value)
    {
        const auto text = RPS::Settings::normalizeValue(ValueType::Float, std::format("{}", static_cast<float>(value)));
        require(text.has_value(), std::format("{} cannot hold {}", definition.key, value));
        return paper::settings::fromStore(store, { { RPS::Settings::settingPath(definition.table, definition.key), *text } });
    }

    /**
     * Checks a float slider against the loader: one step inside either end runs differently from the end, so the
     * loader takes every value it offers, and past either end runs as the end, so it offers every value the loader
     * takes.
     */
    void checkSlider(const RPS::Settings::Store& store, const Definition& definition, const json& row)
    {
        const auto name = RPS::Settings::settingPath(definition.table, definition.key);
        const auto& options = row.at("valueOptions");
        const double minimum = options.at("min").get<double>();
        const double maximum = options.at("max").get<double>();
        const double step = options.at("step").get<double>();
        require(minimum < maximum && step > 0.0, std::format("{} has no usable slider", name));
        const double span = maximum - minimum;
        require(!(valuesWith(store, definition, minimum) == valuesWith(store, definition, minimum + step)) &&
                    !(valuesWith(store, definition, maximum) == valuesWith(store, definition, maximum - step)),
            std::format("{} has a slider wider than PAPER accepts", name));
        require(valuesWith(store, definition, minimum - span) == valuesWith(store, definition, minimum) &&
                    valuesWith(store, definition, maximum + span) == valuesWith(store, definition, maximum),
            std::format("{} has a slider narrower than PAPER accepts", name));
    }

    /**
     * Checks a row's valueType and default against PAPER's compiled default, the value Reset sets.
     */
    void checkDefault(const std::string& name, const json& row, const Definition& definition)
    {
        const auto type = specType(definition.type);
        require(!type.empty(), std::format("{} has a type the page can't show", name));
        require(row.value("valueType", "") == type,
            std::format("{} is valueType {} in the spec, {} in PAPER", name, row.value("valueType", ""), type));
        require(row.contains("default"), std::format("{} has no default", name));
        const auto& value = row.at("default");
        bool same = false;
        switch (definition.type) {
        case ValueType::Boolean:
            same = value.is_boolean() && value.get<bool>() == (definition.defaultValue == "true");
            break;
        case ValueType::Integer:
            same = value.is_number_integer() && value.get<long long>() == std::stoll(definition.defaultValue);
            break;
        case ValueType::Float:
            same = value.is_number() && nearlyEqual(value.get<double>(), std::stod(definition.defaultValue));
            break;
        case ValueType::String:
            same = value.is_string() && value.get<std::string>() == definition.defaultValue;
            break;
        default:
            break;
        }
        require(same, std::format("{} defaults to {} in the spec, {} in PAPER", name, value.dump(), definition.defaultValue));
    }
}

int main()
{
    try {
        const fs::path root(PAPER_MODS_CONFIG_TEST_DIR);
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root);
        const auto store = paper::settings::makeStore(root);
        store->load(true);
        const auto definitions = paper::settings::definitions();

        std::ifstream input(PAPER_MODS_CONFIG_SPEC, std::ios::binary);
        require(input.good(), "cannot read the spec");
        const json spec = json::parse(input);
        require(spec.value("modName", "") == "PAPER", "the spec's modName is not PAPER, the name PAPER's client connects under");
        const auto& plugins = spec.at("f4sePluginRequirements");
        require(std::find(plugins.begin(), plugins.end(), json("PAPER")) != plugins.end(),
            "the spec does not require PAPER's F4SE plugin");
        const auto& files = spec.at("files");
        require(files.size() == 1 && spec.value("defaultFile", "") == "config", "the spec's only file is not its default");
        const auto& file = files.at("config");
        require(file.value("format", "") == "toml" && file.value("root", "") == "myGames" &&
                    file.value("path", "") == std::format("Mods_Config/PAPER/{}", paper::settings::kFileName) &&
                    !file.value("omitDefault", false),
            "the spec's file is not PAPER.toml under My Games");

        std::vector<const json*> rows;
        collectSettings(spec.at("content"), rows);
        if (spec.contains("pages"))
            collectSettings(spec.at("pages"), rows);

        std::set<std::string> shown;
        for (const auto* row : rows) {
            const auto& storage = row->at("storage");
            require(!storage.contains("file"), "a setting names a file; the page edits PAPER.toml only");
            const auto& path = storage.at("path");
            require(path.is_array() && path.size() == 2, std::format("a storage is not a table and key: {}", storage.dump()));
            const auto table = path[0].get<std::string>();
            const auto key = path[1].get<std::string>();
            const auto name = RPS::Settings::settingPath(table, key);
            require(shown.insert(name).second, std::format("{} is on the page twice", name));
            const auto definition = std::ranges::find_if(definitions,
                [&](const Definition& entry) { return entry.table == table && entry.key == key; });
            require(definition != definitions.end(), std::format("{} is not a PAPER setting", name));
            checkDefault(name, *row, *definition);
            if (row->value("type", "") == "slider") {
                require(definition->type == ValueType::Float, std::format("{} is a slider but not a float", name));
                checkSlider(*store, *definition, *row);
            }
        }

        for (const auto leftOut : kLeftOut)
            require(!shown.contains(std::string(leftOut)), std::format("{} is left out but on the page", leftOut));
        for (const auto& definition : definitions) {
            const auto name = RPS::Settings::settingPath(definition.table, definition.key);
            require(shown.contains(name) || std::ranges::find(kLeftOut, std::string_view(name)) != kLeftOut.end(),
                std::format("{} is a new PAPER.toml setting: add it to the Mods Config page or to kLeftOut", name));
        }
        fs::remove_all(root, ignored);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
