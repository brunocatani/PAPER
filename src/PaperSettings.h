#pragma once

// PAPER.toml through RPS Framework's settings store: every option's table,
// key, help and compiled default, the INI keys of earlier versions, and the
// conversion of loaded values into PaperConfig.

#include "PaperConfig.h"

#include <RPS/Settings/Store.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace paper::settings
{
    inline constexpr std::string_view kFileName = "PAPER.toml";

    [[nodiscard]] std::vector<RPS::Settings::Definition> definitions();
    [[nodiscard]] std::span<const RPS::Settings::LegacyKey> legacyKeys();

    // The store for PAPER.toml in directory; startup loads migrate PAPER.ini
    // from the same directory once.
    [[nodiscard]] std::unique_ptr<RPS::Settings::Store> makeStore(
        const std::filesystem::path& directory);

    // Loaded values as PAPER uses them: enumerations parsed, numbers clamped
    // to their documented ranges. activePath is the store's file.
    [[nodiscard]] PaperConfig fromStore(const RPS::Settings::Store& store);
}
