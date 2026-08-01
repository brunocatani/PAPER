#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace paper::animation_evidence_policy
{
    [[nodiscard]] constexpr std::uint32_t boundedSampleCopyCount(
        const std::uint32_t requestedCount,
        const std::uint64_t remainingBytes,
        const std::size_t sampleBytes) noexcept
    {
        if (sampleBytes == 0) {
            return 0;
        }
        const auto capacity = remainingBytes / sampleBytes;
        return static_cast<std::uint32_t>((std::min)(
            static_cast<std::uint64_t>(requestedCount),
            capacity));
    }

    [[nodiscard]] constexpr float sampleTime(
        const float durationSeconds,
        const std::uint32_t sampleIndex,
        const std::uint32_t authoredSampleCount) noexcept
    {
        if (!(durationSeconds > 0.0f) || authoredSampleCount < 2) {
            return 0.0f;
        }
        const auto boundedIndex = (std::min)(
            sampleIndex,
            authoredSampleCount - 1);
        return durationSeconds * static_cast<float>(boundedIndex) /
               static_cast<float>(authoredSampleCount - 1);
    }
}
