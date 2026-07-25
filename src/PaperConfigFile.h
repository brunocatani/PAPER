#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

namespace paper::config_file
{
    enum class EnsureStatus
    {
        Existing,
        Created,
        Failed,
    };

    struct EnsureResult
    {
        EnsureStatus status{ EnsureStatus::Failed };
        std::error_code error{};
    };

    // Creates parent directories and the file only when it is absent.
    // The CREATE_NEW write is atomic with respect to an existing user file.
    [[nodiscard]] EnsureResult ensureFileExists(
        const std::filesystem::path& path,
        std::string_view defaultContents);
}
