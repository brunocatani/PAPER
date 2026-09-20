#pragma once

#include "support/NativeMemory.h"
#include "REL/Module.h"

#include <atomic>
#include <cstdint>

namespace RE { class TESObjectREFR; }

namespace paper::native_animation_graph
{
    constexpr std::uintptr_t kRefrGraphHolderInterfaceOffset = 0x48;
    constexpr std::uintptr_t kGetGraphManagerVtableSlotOffset = 0x20;
    constexpr std::uintptr_t kGraphManagerRefCountOffset = 0x8;
    constexpr std::uintptr_t kGraphManagerVtableModuleOffset = 0x2E00550;

    [[nodiscard]] inline bool pointerInModuleImage(const std::uintptr_t value)
    {
        const auto base = REL::Module::get().base();
        return value > base && value - base < 0x800'0000ull;
    }

    using GetGraphManagerFn = bool (*)(void*, void**);
    using DestroyGraphManagerFn = void* (*)(void*, std::uint32_t);

    class AcquiredGraphManager
    {
    public:
        explicit AcquiredGraphManager(RE::TESObjectREFR* refr)
        {
            if (!refr) {
                return;
            }
            auto* holder = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(refr) +
                kRefrGraphHolderInterfaceOffset);
            std::uintptr_t vtable = 0;
            std::uintptr_t getManager = 0;
            if (!native_memory::tryReadValue(
                    reinterpret_cast<const std::uintptr_t*>(holder),
                    vtable) ||
                !pointerInModuleImage(vtable) ||
                !native_memory::tryReadValue(
                    reinterpret_cast<const std::uintptr_t*>(
                        vtable + kGetGraphManagerVtableSlotOffset),
                    getManager) ||
                !pointerInModuleImage(getManager)) {
                return;
            }
            void* raw = nullptr;
            reinterpret_cast<GetGraphManagerFn>(getManager)(holder, &raw);
            _reference = raw;
            std::uintptr_t managerVtable = 0;
            if (raw && native_memory::tryReadValue(
                           reinterpret_cast<const std::uintptr_t*>(raw),
                           managerVtable) &&
                managerVtable == REL::Module::get().base() +
                    kGraphManagerVtableModuleOffset) {
                _manager = raw;
            }
        }

        AcquiredGraphManager(const AcquiredGraphManager&) = delete;
        AcquiredGraphManager& operator=(
            const AcquiredGraphManager&) = delete;

        ~AcquiredGraphManager()
        {
            if (!_reference) {
                return;
            }
            auto* refCount = reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uintptr_t>(_reference) +
                kGraphManagerRefCountOffset);
            if (!native_memory::pointerRangeLooksWritable(
                    refCount,
                    sizeof(*refCount)) ||
                std::atomic_ref<std::uint32_t>{ *refCount }.fetch_sub(
                    1,
                    std::memory_order_acq_rel) != 1) {
                return;
            }
            std::uintptr_t vtable = 0;
            std::uintptr_t destroy = 0;
            if (native_memory::tryReadValue(
                    reinterpret_cast<const std::uintptr_t*>(_reference),
                    vtable) &&
                pointerInModuleImage(vtable) &&
                native_memory::tryReadValue(
                    reinterpret_cast<const std::uintptr_t*>(vtable),
                    destroy) &&
                pointerInModuleImage(destroy)) {
                reinterpret_cast<DestroyGraphManagerFn>(destroy)(
                    _reference,
                    1);
            }
        }

        [[nodiscard]] const void* get() const
        {
            return _manager;
        }

    private:
        void* _reference{ nullptr };
        const void* _manager{ nullptr };
    };

}
