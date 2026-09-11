#pragma once

#include <string>

namespace paper::config_defaults
{
    // Values come from PaperConfig's compiled defaults. No example/file I/O.
    [[nodiscard]] std::string makeDefaultIni();
}
