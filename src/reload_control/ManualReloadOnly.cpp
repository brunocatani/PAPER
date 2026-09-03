#include "reload_control/ManualReloadOnly.h"

#include "native/NativeOffsets.h"
#include "PaperLog.h"

#include <Windows.h>

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
        using NativeActionExecutorFn =
            void (*)(void* actor, std::int32_t actionId, void* actionData);

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
        std::atomic<NativeActionExecutorFn> s_originalActionExecutor{ nullptr };
        std::atomic<bool> s_hookInstalled{ false };
        std::atomic<bool> s_hookInstallFailed{ false };
        std::atomic<bool> s_actionObservationHookInstalled{ false };
        std::atomic<bool> s_actionObservationHookInstallFailed{ false };
        std::atomic<bool> s_runtimeEnabled{ false };
        std::atomic<std::uintptr_t> s_installedCallTarget{ 0 };
        std::atomic<std::uintptr_t> s_installedActionCallTarget{ 0 };
        std::atomic<std::uint64_t> s_suppressedRequestSequence{ 0 };
        std::atomic<std::uint64_t> s_suppressedSequenceAtLastReloadStart{ 0 };
        std::atomic<std::uint64_t> s_reloadActionExecutionSequence{ 0 };
        std::atomic<std::uint64_t> s_reloadActionExecutionSequenceAtLastReloadStart{ 0 };
        std::atomic<std::uintptr_t> s_lastReloadActionCaller{ 0 };
        std::atomic<std::uintptr_t> s_lastReloadActionCallerOffset{ 0 };
        std::atomic<bool> s_lastReloadActionCallerIsMainModule{ false };

        [[nodiscard]] std::uintptr_t decodeRelativeCallTarget(
            const std::uintptr_t callsiteAddress)
        {
            const auto* callBytes =
                reinterpret_cast<const std::uint8_t*>(callsiteAddress);
            if (!callBytes || callBytes[0] != 0xE8) {
                return 0;
            }

            std::int32_t relativeTarget = 0;
            std::memcpy(
                &relativeTarget,
                callBytes + 1,
                sizeof(relativeTarget));
            return static_cast<std::uintptr_t>(
                static_cast<std::intptr_t>(callsiteAddress + 5u) +
                relativeTarget);
        }

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
                s_suppressedRequestSequence.fetch_add(
                    1,
                    std::memory_order_acq_rel);
                return false;
            }

            const auto original =
                s_originalDispatcher.load(std::memory_order_acquire);
            return original ? original(dispatcher, actionId, priority) : false;
        }

        void onNativeActionExecution(
            void* actor,
            const std::int32_t actionId,
            void* actionData)
        {
            constexpr std::int32_t kReloadActionId = 0x6C;
            if (actionId == kReloadActionId) {
                std::array<void*, 16> frames{};
                const auto frameCount = CaptureStackBackTrace(
                    0,
                    static_cast<DWORD>(frames.size()),
                    frames.data(),
                    nullptr);
                const auto moduleBase = REL::Module::get().base();
                const auto dispatcherBegin =
                    moduleBase + offsets::kFunc_PlayerControls_DoAction;
                constexpr std::uintptr_t kDispatcherMaximumSize = 0x800;
                std::uintptr_t caller = 0;
                for (USHORT index = 0; index + 1 < frameCount; ++index) {
                    const auto frame =
                        reinterpret_cast<std::uintptr_t>(frames[index]);
                    if (frame >= dispatcherBegin &&
                        frame < dispatcherBegin + kDispatcherMaximumSize) {
                        caller = reinterpret_cast<std::uintptr_t>(
                            frames[index + 1]);
                        break;
                    }
                }

                HMODULE callerModule = nullptr;
                const bool callerModuleResolved = caller != 0 &&
                    GetModuleHandleExW(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<LPCWSTR>(caller),
                        &callerModule);
                const bool callerIsMainModule =
                    callerModuleResolved &&
                    callerModule == GetModuleHandleW(nullptr);
                const auto callerOffset = callerIsMainModule ?
                    caller - moduleBase : 0;
                const auto sequence =
                    s_reloadActionExecutionSequence.fetch_add(
                        1,
                        std::memory_order_acq_rel) +
                    1;
                s_lastReloadActionCaller.store(
                    caller,
                    std::memory_order_release);
                s_lastReloadActionCallerOffset.store(
                    callerOffset,
                    std::memory_order_release);
                s_lastReloadActionCallerIsMainModule.store(
                    callerIsMainModule,
                    std::memory_order_release);
                PAPER_LOG_INFO(
                    Reload,
                    "Native reload action execution diagnostic: sequence={} caller=0x{:X} mainModule={} callerOffset=0x{:X}",
                    sequence,
                    caller,
                    callerIsMainModule,
                    callerOffset);
            }

            const auto original =
                s_originalActionExecutor.load(std::memory_order_acquire);
            if (original) {
                original(actor, actionId, actionData);
            }
        }

        void installActionExecutionObservationHook()
        {
            if (s_actionObservationHookInstalled.load(
                    std::memory_order_acquire) ||
                s_actionObservationHookInstallFailed.load(
                    std::memory_order_acquire)) {
                return;
            }

            REL::Relocation<std::uintptr_t> callsite{
                REL::Offset(offsets::kCallsite_PlayerControls_ExecuteAction)
            };
            REL::Relocation<std::uintptr_t> expectedExecutor{
                REL::Offset(offsets::kFunc_PlayerControls_ExecuteAction)
            };
            const auto callsiteAddress = callsite.address();
            const auto decodedTarget =
                decodeRelativeCallTarget(callsiteAddress);
            if (decodedTarget != expectedExecutor.address()) {
                s_actionObservationHookInstallFailed.store(
                    true,
                    std::memory_order_release);
                PAPER_LOG_ERROR(
                    Reload,
                    "Reload action observation hook validation failed callsite=0x{:X} target=0x{:X} expected=0x{:X}",
                    callsiteAddress,
                    decodedTarget,
                    expectedExecutor.address());
                return;
            }

            s_originalActionExecutor.store(
                reinterpret_cast<NativeActionExecutorFn>(
                    expectedExecutor.address()),
                std::memory_order_release);
            std::uintptr_t original = 0;
            try {
                original = F4SE::GetTrampoline().write_call<5>(
                    callsiteAddress,
                    &onNativeActionExecution);
            } catch (const std::exception& error) {
                s_actionObservationHookInstallFailed.store(
                    true,
                    std::memory_order_release);
                PAPER_LOG_ERROR(
                    Reload,
                    "Reload action observation hook installation failed: {}",
                    error.what());
                return;
            } catch (...) {
                s_actionObservationHookInstallFailed.store(
                    true,
                    std::memory_order_release);
                PAPER_LOG_ERROR(
                    Reload,
                    "Reload action observation hook installation failed with an unknown exception");
                return;
            }

            if (original != expectedExecutor.address()) {
                s_actionObservationHookInstallFailed.store(
                    true,
                    std::memory_order_release);
                PAPER_LOG_ERROR(
                    Reload,
                    "Reload action observation hook returned unexpected original target 0x{:X}; expected 0x{:X}",
                    original,
                    expectedExecutor.address());
                return;
            }

            s_installedActionCallTarget.store(
                decodeRelativeCallTarget(callsiteAddress),
                std::memory_order_release);
            s_actionObservationHookInstalled.store(
                true,
                std::memory_order_release);
            PAPER_LOG_INFO(
                Reload,
                "Installed reload action execution observation hook at 0x{:X}",
                callsiteAddress);
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
        const auto decodedTarget = decodeRelativeCallTarget(callsiteAddress);
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

        s_installedCallTarget.store(
            decodeRelativeCallTarget(callsiteAddress),
            std::memory_order_release);
        installActionExecutionObservationHook();

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
        if (!wasEnabled && effectiveEnabled) {
            s_suppressedSequenceAtLastReloadStart.store(
                s_suppressedRequestSequence.load(std::memory_order_acquire),
                std::memory_order_release);
            s_reloadActionExecutionSequenceAtLastReloadStart.store(
                s_reloadActionExecutionSequence.load(
                    std::memory_order_acquire),
                std::memory_order_release);
        }
        if (wasEnabled != effectiveEnabled) {
            PAPER_LOG_INFO(
                Reload,
                "Manual-reload-only automatic reload suppression {}",
                effectiveEnabled ? "enabled" : "disabled");
        }
    }

    void observePlayerReloadStart()
    {
        if (!s_runtimeEnabled.load(std::memory_order_acquire)) {
            return;
        }

        REL::Relocation<std::uintptr_t> callsite{
            REL::Offset(offsets::kCallsite_PlayerControls_AutomaticReload)
        };
        const auto liveTarget = decodeRelativeCallTarget(callsite.address());
        const auto installedTarget =
            s_installedCallTarget.load(std::memory_order_acquire);
        REL::Relocation<std::uintptr_t> actionCallsite{
            REL::Offset(offsets::kCallsite_PlayerControls_ExecuteAction)
        };
        const auto liveActionTarget =
            decodeRelativeCallTarget(actionCallsite.address());
        const auto installedActionTarget =
            s_installedActionCallTarget.load(std::memory_order_acquire);
        const auto suppressedSequence =
            s_suppressedRequestSequence.load(std::memory_order_acquire);
        const auto previousSequence =
            s_suppressedSequenceAtLastReloadStart.exchange(
                suppressedSequence,
                std::memory_order_acq_rel);
        const auto reloadActionSequence =
            s_reloadActionExecutionSequence.load(std::memory_order_acquire);
        const auto previousReloadActionSequence =
            s_reloadActionExecutionSequenceAtLastReloadStart.exchange(
                reloadActionSequence,
                std::memory_order_acq_rel);
        const auto lastReloadActionCaller =
            s_lastReloadActionCaller.load(std::memory_order_acquire);
        const auto lastReloadActionCallerOffset =
            s_lastReloadActionCallerOffset.load(std::memory_order_acquire);
        const bool lastReloadActionCallerIsMainModule =
            s_lastReloadActionCallerIsMainModule.load(
                std::memory_order_acquire);

        PAPER_LOG_INFO(
            Reload,
            "Manual-reload-only reload-start diagnostic: suppressedSincePreviousStart={} suppressedTotal={} reloadActionsSincePreviousStart={} reloadActionsTotal={} suppressionHookIntact={} actionHookIntact={} lastReloadCaller=0x{:X} lastReloadCallerMainModule={} lastReloadCallerOffset=0x{:X}",
            suppressedSequence - previousSequence,
            suppressedSequence,
            reloadActionSequence - previousReloadActionSequence,
            reloadActionSequence,
            installedTarget != 0 && liveTarget == installedTarget,
            installedActionTarget != 0 &&
                liveActionTarget == installedActionTarget,
            lastReloadActionCaller,
            lastReloadActionCallerIsMainModule,
            lastReloadActionCallerOffset);
    }

    void resetSession()
    {
        setRuntimeEnabled(false);
        s_suppressedSequenceAtLastReloadStart.store(
            s_suppressedRequestSequence.load(std::memory_order_acquire),
            std::memory_order_release);
        s_reloadActionExecutionSequenceAtLastReloadStart.store(
            s_reloadActionExecutionSequence.load(std::memory_order_acquire),
            std::memory_order_release);
    }
}
