#include "reload_control/ManualReloadOnly.h"

#include "native/NativeOffsets.h"
#include "PaperLog.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>

namespace paper::manual_reload_only
{
    namespace
    {
        using NativeActionDispatcherFn =
            bool (*)(void* dispatcher, std::int32_t actionId, std::uint32_t priority);

        // Fallout4VR.exe 1.2.72 PlayerControls::DoAction post-fire fallback.
        // The final CALL is reached only for fire actions 0x71..0x74 when a
        // loaded-ammo object exists and Actor::GetCurrentAmmoCount returns 0.
        constexpr std::array<std::uint8_t, 60>
            kAutomaticReloadProducerSignature{
                0x8B, 0x54, 0x24, 0x40, 0x48, 0x8B, 0x4C, 0x24,
                0x38, 0xE8, 0x7A, 0x4E, 0xE4, 0xFF, 0x8B, 0x54,
                0x24, 0x40, 0x48, 0x8B, 0x4C, 0x24, 0x38, 0x48,
                0x8B, 0xF8, 0xE8, 0x59, 0xE9, 0xE1, 0xFF, 0x48,
                0x85, 0xFF, 0x74, 0x1A, 0x85, 0xC0, 0x0F, 0x85,
                0x8C, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4D, 0x60,
                0x8D, 0x50, 0x6C, 0x44, 0x8D, 0x40, 0x02, 0xE8,
                0x8C, 0xFA, 0xFF, 0xFF,
            };

        std::atomic<NativeActionDispatcherFn> s_originalDispatcher{ nullptr };
        std::atomic<bool> s_hookInstalled{ false };
        std::atomic<bool> s_hookInstallFailed{ false };
        std::atomic<bool> s_runtimeEnabled{ false };

        [[nodiscard]] bool matchesAutomaticReloadProducerSignature(
            const std::uintptr_t address)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
            if (!bytes) {
                return false;
            }
            for (std::size_t index = 0;
                 index < kAutomaticReloadProducerSignature.size();
                 ++index) {
                if (bytes[index] != kAutomaticReloadProducerSignature[index]) {
                    return false;
                }
            }
            return true;
        }

        bool onAutomaticReloadRequest(
            void* dispatcher,
            const std::int32_t actionId,
            const std::uint32_t priority)
        {
            if (s_runtimeEnabled.load(std::memory_order_acquire)) {
                return false;
            }

            const auto original =
                s_originalDispatcher.load(std::memory_order_acquire);
            return original ? original(dispatcher, actionId, priority) : false;
        }
    }

    bool installHook()
    {
        if (s_hookInstalled.load(std::memory_order_acquire)) {
            return !s_hookInstallFailed.load(std::memory_order_acquire);
        }
        if (s_hookInstallFailed.load(std::memory_order_acquire)) {
            return false;
        }
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72) {
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only hook rejected a non-FO4VR-1.2.72 executable layout");
            s_hookInstallFailed.store(true, std::memory_order_release);
            return false;
        }

        REL::Relocation<std::uintptr_t> signatureEntry{
            REL::Offset(offsets::kSignature_PlayerControls_AutomaticReload)
        };
        REL::Relocation<std::uintptr_t> callsite{
            REL::Offset(offsets::kCallsite_PlayerControls_AutomaticReload)
        };
        REL::Relocation<std::uintptr_t> expectedDispatcher{
            REL::Offset(offsets::kFunc_PlayerControls_DoAction)
        };
        const auto callsiteAddress = callsite.address();
        const auto* callBytes =
            reinterpret_cast<const std::uint8_t*>(callsiteAddress);
        std::int32_t relativeTarget = 0;
        if (callBytes && callBytes[0] == 0xE8) {
            std::memcpy(
                &relativeTarget,
                callBytes + 1,
                sizeof(relativeTarget));
        }
        const auto decodedTarget = callBytes && callBytes[0] == 0xE8 ?
            static_cast<std::uintptr_t>(
                static_cast<std::intptr_t>(callsiteAddress + 5u) +
                relativeTarget) :
            0u;
        if (!matchesAutomaticReloadProducerSignature(signatureEntry.address()) ||
            !callBytes || callBytes[0] != 0xE8 ||
            decodedTarget != expectedDispatcher.address()) {
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only hook validation failed signature=0x{:X} callsite=0x{:X} opcode=0x{:02X} target=0x{:X} expected=0x{:X}",
                signatureEntry.address(),
                callsiteAddress,
                callBytes ? callBytes[0] : 0,
                decodedTarget,
                expectedDispatcher.address());
            s_hookInstallFailed.store(true, std::memory_order_release);
            return false;
        }

        const auto expectedOriginal = reinterpret_cast<NativeActionDispatcherFn>(
            expectedDispatcher.address());
        s_originalDispatcher.store(expectedOriginal, std::memory_order_release);
        auto& trampoline = F4SE::GetTrampoline();
        std::uintptr_t original = 0;
        try {
            original = trampoline.write_call<5>(
                callsiteAddress,
                &onAutomaticReloadRequest);
        } catch (const std::exception& error) {
            s_hookInstallFailed.store(true, std::memory_order_release);
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only hook installation failed: {}",
                error.what());
            return false;
        } catch (...) {
            s_hookInstallFailed.store(true, std::memory_order_release);
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only hook installation failed with an unknown exception");
            return false;
        }
        s_hookInstalled.store(true, std::memory_order_release);
        if (original != expectedDispatcher.address()) {
            // The patched wrapper still forwards to the independently
            // validated target, so leaving runtime suppression disabled
            // preserves vanilla behavior without risking a second patch.
            s_hookInstallFailed.store(true, std::memory_order_release);
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only hook returned unexpected original target 0x{:X}; suppression remains disabled",
                original);
            return false;
        }

        PAPER_LOG_INFO(
            Reload,
            "Installed validated empty-loaded-ammo automatic reload hook at 0x{:X}; manual reload dispatchers remain untouched",
            callsiteAddress);
        return true;
    }

    void setRuntimeEnabled(const bool enabled)
    {
        const bool effectiveEnabled =
            enabled &&
            s_hookInstalled.load(std::memory_order_acquire) &&
            !s_hookInstallFailed.load(std::memory_order_acquire);
        const bool wasEnabled = s_runtimeEnabled.exchange(
            effectiveEnabled,
            std::memory_order_acq_rel);
        if (wasEnabled != effectiveEnabled) {
            PAPER_LOG_INFO(
                Reload,
                "Manual-reload-only automatic reload suppression {}",
                effectiveEnabled ? "enabled" : "disabled");
        }
    }

    void resetSession()
    {
        setRuntimeEnabled(false);
    }
}
