#pragma once

#include <array>
#include <cmath>
#include <string_view>

namespace paper::merged_reload_policy
{
    inline constexpr std::size_t kPathCapacity = 260;
    using Path = std::array<char, kPathCapacity>;

    [[nodiscard]] constexpr char asciiLower(const char value) noexcept
    {
        return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
    }

    // Match the authored optional reload slot, never an arbitrary substring
    // or another weapon's path. Keep the selected graph's directory/extension.
    [[nodiscard]] constexpr bool normalReloadPath(
        const std::string_view requested, Path& out) noexcept
    {
        out = {};
        const auto separator = requested.find_last_of("/\\");
        const auto begin = separator == std::string_view::npos ? 0 : separator + 1;
        const auto leaf = requested.substr(begin);
        constexpr std::string_view reserve = "wpnreloadreserve.hkt";
        if (requested.size() >= out.size() || leaf.size() != reserve.size()) {
            return false;
        }
        for (std::size_t index = 0; index < reserve.size() - 1; ++index) {
            if (asciiLower(leaf[index]) != reserve[index]) return false;
        }
        const char extension = asciiLower(leaf.back());
        if (extension != 't' && extension != 'x') return false;
        for (std::size_t index = 0; index < begin; ++index) out[index] = requested[index];
        constexpr std::string_view normal = "WPNReload.hk";
        for (std::size_t index = 0; index < normal.size(); ++index) out[begin + index] = normal[index];
        out[begin + normal.size()] = leaf.back();
        return true;
    }

    [[nodiscard]] inline bool playableReload(
        const float duration, const int transformTracks) noexcept
    {
        return std::isfinite(duration) && duration > 0.0f && transformTracks > 0;
    }
}
