#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace paper::native_memory
{
    [[nodiscard]] bool pointerLooksReadable(const void* pointer);
    [[nodiscard]] bool guardedCopyFromMemory(
        const void* source,
        void* target,
        std::size_t byteCount);
    // Atomically decrements a native reference count; false if it faults.
    [[nodiscard]] bool guardedDecrement(
        std::uint32_t* counter,
        std::uint32_t& remaining);

    template <class T>
    [[nodiscard]] bool tryReadValue(const T* address, T& out)
    {
        static_assert(
            std::is_trivially_copyable_v<T>,
            "native reads require trivially copyable values");
        return guardedCopyFromMemory(address, &out, sizeof(T));
    }

    template <class T>
    [[nodiscard]] bool tryReadField(
        const void* base,
        const std::ptrdiff_t offset,
        T& out)
    {
        if (!base) {
            return false;
        }
        const auto* address = reinterpret_cast<const T*>(
            reinterpret_cast<const std::byte*>(base) + offset);
        return tryReadValue(address, out);
    }
}
