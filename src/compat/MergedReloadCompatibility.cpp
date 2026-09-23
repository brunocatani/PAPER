#include "PCH.h"

#include "compat/MergedReloadCompatibility.h"
#include "compat/MergedReloadPolicy.h"
#include "PaperLog.h"
#include "support/AcquiredAnimationGraphManager.h"
#include "support/NativeMemory.h"

#include <Windows.h>
#include <atomic>
#include <memory>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>

namespace paper::merged_reload_compatibility
{
    namespace
    {
        using ResolveBinding = void* (*)(void*, const void*, const void*);
        using ContextGraph = const void* (*)(const void*);
        using LookupBinding = void* (*)(void*, const void*, std::uintptr_t, const char*);

        // FO4VR 1.2.72 raw witnesses: 0x141726E40 installs this vtable;
        // 0x14192CF12 invokes slot +8 for a subgraph-bound clip (userData=4).
        // 0x141727820 resolves context -> graph and looks up clip+0x90 in
        // that graph's loaded subgraph. 0x141774800 returns a BORROWED wrapper:
        // its temporary reference is released before return. Activation at
        // 0x14192CF4B retains the binding and 0x1419303A0 imports its triggers.
        constexpr std::uintptr_t kResolverSlot = 0x2E02178;
        constexpr std::uintptr_t kResolverFunction = 0x1727820;
        constexpr std::uintptr_t kContextGraphFunction = 0x16AF760;
        constexpr std::uintptr_t kLookupFunction = 0x1774800;
        constexpr std::uintptr_t kLookupSingleton = 0x5AB9200;
        constexpr std::uintptr_t kGraphVtable = 0x2E00A48;
        constexpr std::uintptr_t kWrapperVtable = 0x2E0DFC0;
        constexpr std::uintptr_t kBindingVtable = 0x2E0FAE8;
        constexpr std::size_t kMaximumGraphs = 8;

        std::atomic<ResolveBinding> s_original{ nullptr };
        DWORD s_ownerThread = 0;
        bool s_installAttempted = false;
        std::atomic<bool> s_enabled{ false };
        std::atomic<bool> s_faulted{ false };
        std::atomic<bool> s_threadWarning{ false };
        std::uint64_t s_attempts = 0;

        // The game thread owns installation and all compatibility decisions.
        // This bounded logger worker owns only copied text and shared sinks,
        // never engine objects; destruction drains/joins it after the logger.
        // Reload callbacks perform no file I/O and never wait for the worker.
        struct Diagnostics
        {
            std::shared_ptr<spdlog::details::thread_pool> pool;
            std::shared_ptr<spdlog::async_logger> log;
        };
        std::unique_ptr<Diagnostics> s_diagnostics;

        template <class T>
        bool read(const void* object, const std::ptrdiff_t offset, T& out)
        {
            return native_memory::tryReadField(object, offset, out);
        }

        bool hasVtable(const void* object, const std::uintptr_t offset)
        {
            std::uintptr_t actual = 0;
            return read(object, 0, actual) && actual == REL::Module::get().base() + offset;
        }

        bool readClipPath(const void* clip, merged_reload_policy::Path& path)
        {
            std::uintptr_t name = 0;
            if (!read(clip, 0x90, name)) return false;
            name &= ~std::uintptr_t{ 1 };
            if (name == 0) return false;
            for (std::size_t i = 0; i < path.size(); ++i) {
                if (!read(reinterpret_cast<const void*>(name), i, path[i])) return false;
                if (path[i] == '\0') return i != 0;
                if (static_cast<unsigned char>(path[i]) < 0x20 ||
                    static_cast<unsigned char>(path[i]) > 0x7e) return false;
            }
            return false;
        }

        bool isPlayerGraph(const void* graph, const void* manager, const char*& stage)
        {
            stage = "player-graph-array";
            // BSAnimationGraphManager ctor 0x14168F4F0 and population
            // 0x141691990 agree: +40 flags/capacity, +48 storage, +50 size.
            std::uint32_t capacity = 0;
            std::uint32_t count = 0;
            if (!manager || !read(manager, 0x40, capacity) || !read(manager, 0x50, count) ||
                count == 0 || count > kMaximumGraphs || count > (capacity & 0x7fffffff)) return false;
            auto storage = reinterpret_cast<std::uintptr_t>(manager) + 0x48;
            if ((capacity & 0x80000000) == 0 && !read(manager, 0x48, storage)) return false;
            if (storage == 0) return false;
            for (std::uint32_t i = 0; i < count; ++i) {
                const void* candidate = nullptr;
                if (!read(reinterpret_cast<const void*>(storage), i * sizeof(void*), candidate)) return false;
                if (candidate == graph) return true;
            }
            stage = "not-player-graph";
            return false;
        }

        bool playableBinding(const void* wrapper, float& duration, const char*& stage)
        {
            // Wrapper ctor 0x1418FC3E0 / consumer 0x14192CF4B: +10 binding.
            // Missing-binding constructor 0x141930720 / duration accessor
            // 0x14192E940: binding+18 animation, animation+14 seconds.
            stage = "normal-binding-wrapper";
            const void* binding = nullptr;
            if (!hasVtable(wrapper, kWrapperVtable) || !read(wrapper, 0x10, binding)) return false;
            stage = "normal-animation-binding";
            const void* animation = nullptr;
            if (!hasVtable(binding, kBindingVtable) || !read(binding, 0x18, animation)) return false;
            stage = "normal-animation-payload";
            std::uintptr_t vtable = 0;
            int tracks = 0;
            if (!read(animation, 0, vtable) || !native_animation_graph::pointerInModuleImage(vtable) ||
                !read(animation, 0x14, duration) || !read(animation, 0x18, tracks)) return false;
            stage = "normal-animation-duration";
            return merged_reload_policy::playableReload(duration, tracks);
        }

        void* resolveMissingReserve(const void* context, const void* clip)
        {
            merged_reload_policy::Path requested{};
            merged_reload_policy::Path normal{};
            if (!readClipPath(clip, requested) ||
                !merged_reload_policy::normalReloadPath(requested.data(), normal)) return nullptr;
            if (GetCurrentThreadId() != s_ownerThread) {
                if (!s_threadWarning.exchange(true, std::memory_order_relaxed) && s_diagnostics) {
                    s_diagnostics->log->warn("[PAPER::Compatibility] Merged reload binding skipped: reserve callback is not on the PAPER frame thread");
                }
                return nullptr;
            }

            const auto base = REL::Module::get().base();
            const auto* graph = reinterpret_cast<ContextGraph>(base + kContextGraphFunction)(context);
            if (!hasVtable(graph, kGraphVtable)) return nullptr;
            // Acquire for this callback only. No graph/binding pointer survives it.
            native_animation_graph::AcquiredGraphManager manager{ RE::PlayerCharacter::GetSingleton() };
            const char* stage = "player-manager";
            if (!isPlayerGraph(graph, manager.get(), stage)) return nullptr;

            ++s_attempts;
            const bool logAttempt = s_attempts <= 8 || s_attempts % 64 == 0;
            const void* behavior = nullptr;
            std::uintptr_t subgraphKey = 0;
            void* lookup = nullptr;
            void* replacement = nullptr;
            float duration = 0.0f;
            stage = "selected-subgraph";
            if (read(context, 0x10, behavior) && read(behavior, 0x30, subgraphKey) &&
                read(reinterpret_cast<const void*>(base + kLookupSingleton), 0, lookup) && lookup) {
                // Reuse the exact native graph/subgraph lookup. It searches
                // resident bindings only: no resource load, path probing, or
                // clip/graph mutation. The native caller retains the result.
                replacement = reinterpret_cast<LookupBinding>(base + kLookupFunction)(
                    lookup, graph, subgraphKey, normal.data());
                if (!playableBinding(replacement, duration, stage)) replacement = nullptr;
            }
            if (logAttempt && s_diagnostics) {
                s_diagnostics->log->log(replacement ? spdlog::level::info : spdlog::level::warn,
                    "[PAPER::Compatibility] Merged reload binding: requested='{}' normal='{}' result={} stage={} duration={:.3f}s attempt={}",
                    requested.data(), normal.data(), replacement ? "recovered" : "unavailable",
                    replacement ? "native-binding-with-triggers" : stage, duration, s_attempts);
            }
            return replacement;
        }

        void* onResolveBinding(void* manager, const void* context, const void* clip)
        {
            // An authored reserve binding always wins, even if unfamiliar to
            // PAPER. Compatibility is selected only on a native lookup MISS.
            // Supplying the missing asset removes the fallback automatically;
            // no cached substitution persists across weapons or graph rebuilds.
            const auto original = s_original.load(std::memory_order_acquire);
            auto* result = original(manager, context, clip);
            if (result || !s_enabled.load(std::memory_order_acquire)) return result;
            try {
                return resolveMissingReserve(context, clip);
            } catch (...) {
                // Native behavior on failure is the original null result.
                s_faulted.store(true, std::memory_order_release);
                s_enabled.store(false, std::memory_order_release);
                try {
                    if (s_diagnostics) s_diagnostics->log->error("[PAPER::Compatibility] Merged reload binding failed; native missing-binding handling retained");
                } catch (...) {
                    // A logging failure must not escape the native callback.
                }
                return result;
            }
        }

        template <std::size_t N>
        bool matches(const std::uintptr_t offset, const std::array<std::uint8_t, N>& expected)
        {
            std::array<std::uint8_t, N> actual{};
            return native_memory::guardedCopyFromMemory(
                       reinterpret_cast<const void*>(REL::Module::get().base() + offset),
                       actual.data(), actual.size()) && actual == expected;
        }
    }

    bool installHook()
    {
        if (s_original.load(std::memory_order_acquire)) return true;
        if (s_installAttempted) return false;
        s_installAttempted = true;
        constexpr std::array<std::uint8_t, 16> resolverBytes{
            0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xca, 0x49, 0x8b, 0xf8 };
        constexpr std::array<std::uint8_t, 13> contextBytes{
            0x48, 0x8b, 0x41, 0x08, 0x48, 0x85, 0xc0, 0x75, 0x0f, 0x48, 0x8b, 0x01, 0x48 };
        constexpr std::array<std::uint8_t, 17> lookupBytes{
            0x48, 0x8b, 0xc4, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x81, 0xec, 0x78, 0x02, 0x00, 0x00 };
        const auto base = REL::Module::get().base();
        auto* slot = reinterpret_cast<std::uintptr_t*>(base + kResolverSlot);
        std::uintptr_t original = 0;
        if (!REL::Module::IsVR() || REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72 ||
            !native_memory::tryReadValue(slot, original) || original != base + kResolverFunction ||
            !matches(kResolverFunction, resolverBytes) || !matches(kContextGraphFunction, contextBytes) ||
            !matches(kLookupFunction, lookupBytes)) {
            PAPER_LOG_ERROR(Compatibility, "Merged reload binding hook rejected: native resolver/layout validation failed");
            return false;
        }
        try {
            auto diagnostics = std::make_unique<Diagnostics>();
            diagnostics->pool = std::make_shared<spdlog::details::thread_pool>(32, 1);
            diagnostics->log = std::make_shared<spdlog::async_logger>(
                "PAPER_MergedReload", logger::instance->sinks().begin(), logger::instance->sinks().end(),
                diagnostics->pool, spdlog::async_overflow_policy::overrun_oldest);
            diagnostics->log->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
            s_diagnostics = std::move(diagnostics);
        } catch (...) {
            PAPER_LOG_ERROR(Compatibility, "Merged reload binding hook inactive: asynchronous diagnostics unavailable");
            return false;
        }
        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            s_diagnostics.reset();
            PAPER_LOG_ERROR(Compatibility, "Merged reload binding hook inactive: resolver slot is not writable");
            return false;
        }
        s_ownerThread = GetCurrentThreadId();
        s_original.store(reinterpret_cast<ResolveBinding>(original), std::memory_order_release);
        const auto previous = InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(slot), reinterpret_cast<void*>(&onResolveBinding),
            reinterpret_cast<void*>(original));
        DWORD ignored = 0;
        if (!VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored)) {
            PAPER_LOG_WARN(Compatibility, "Merged reload binding hook: restoring slot protection failed");
        }
        if (previous != reinterpret_cast<void*>(original)) {
            s_original.store(nullptr, std::memory_order_release);
            s_diagnostics.reset();
            PAPER_LOG_ERROR(Compatibility, "Merged reload binding hook inactive: resolver slot changed during installation");
            return false;
        }
        PAPER_LOG_INFO(Compatibility, "Merged reload compatibility ready: missing reserve bindings use the same weapon's loaded normal reload; authored reserve bindings remain native");
        return true;
    }

    void setRuntimeEnabled(const bool enabled)
    {
        s_enabled.store(enabled && s_original.load(std::memory_order_acquire) &&
            !s_faulted.load(std::memory_order_acquire), std::memory_order_release);
    }

    spdlog::logger* diagnosticLogger() noexcept
    {
        return s_diagnostics ? s_diagnostics->log.get() : nullptr;
    }
}
