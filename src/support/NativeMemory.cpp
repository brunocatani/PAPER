#include "support/NativeMemory.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace paper::native_memory
{
    bool pointerLooksReadable(const void* pointer)
    {
        return reinterpret_cast<std::uintptr_t>(pointer) > 0x10000;
    }

    // SEH alone guards native memory: a VirtualQuery per access is a kernel
    // call that costs more than the work, and a fault already fails closed.
    // The target may be partially written on failure.
    bool guardedCopyFromMemory(
        const void* source,
        void* target,
        const std::size_t byteCount)
    {
        const auto start = reinterpret_cast<std::uintptr_t>(source);
        if (!target || byteCount == 0 || !pointerLooksReadable(source) ||
            start + byteCount < start) {
            return false;
        }
#if defined(_MSC_VER)
        __try {
            std::memcpy(target, source, byteCount);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#else
        std::memcpy(target, source, byteCount);
        return true;
#endif
    }

    bool guardedDecrement(std::uint32_t* counter, std::uint32_t& remaining)
    {
        remaining = 0;
        if (!pointerLooksReadable(counter)) {
            return false;
        }
#if defined(_MSC_VER)
        __try {
            remaining = static_cast<std::uint32_t>(
                InterlockedDecrement(reinterpret_cast<volatile LONG*>(counter)));
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#else
        remaining = static_cast<std::uint32_t>(
            InterlockedDecrement(reinterpret_cast<volatile LONG*>(counter)));
        return true;
#endif
    }
}
