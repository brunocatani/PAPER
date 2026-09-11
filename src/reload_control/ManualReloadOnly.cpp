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
#include <span>

namespace paper::manual_reload_only
{
    namespace
    {
        using NativeActionDispatcherFn =
            bool (*)(void* dispatcher, std::int32_t actionId, std::uint32_t priority);
        using NativeActionExecutorFn =
            void (*)(void* actor, std::int32_t actionId, void* actionData);
        // 0xF9BEF0 forwards RCX as the action-data argument to 0xE75200.
        // Both automatic callers destroy their stack-owned action after return.
        using NativeActionDataDispatcherFn = bool (*)(void* actionData);

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

        // PlayerCharacter's ammo-consumption override (0xF7A2B0) branches on
        // the remaining count returned by Actor at 0xE4E930. At zero it either
        // arms +0x1010 or submits action-table slot +0x380 (action 0x6C) here.
        constexpr std::array<std::uint8_t, 217>
            kImmediateAutomaticReloadProducerSignature{
                0x0F, 0xB6, 0x93, 0xA1, 0x12, 0x00, 0x00, 0x4C, 0x8B, 0xBC, 0x24, 0xE0,
                0x00, 0x00, 0x00, 0x4C, 0x8B, 0xB4, 0x24, 0x18, 0x01, 0x00, 0x00, 0xF6,
                0xC2, 0x02, 0x0F, 0x84, 0xDF, 0x00, 0x00, 0x00, 0x85, 0xF6, 0x0F, 0x85,
                0xD7, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x47, 0x08, 0x48, 0x85, 0xC0, 0x74,
                0x08, 0x8B, 0x88, 0x10, 0x01, 0x00, 0x00, 0xEB, 0x09, 0x48, 0x8B, 0x07,
                0x8B, 0x88, 0xA8, 0x02, 0x00, 0x00, 0xC1, 0xE9, 0x03, 0x80, 0xE1, 0x01,
                0x84, 0xC9, 0x0F, 0x85, 0xAF, 0x00, 0x00, 0x00, 0xF6, 0xC2, 0x04, 0x74,
                0x1D, 0x44, 0x89, 0xA3, 0x10, 0x10, 0x00, 0x00, 0x4C, 0x8B, 0xA4, 0x24,
                0x10, 0x01, 0x00, 0x00, 0x8B, 0xC6, 0x48, 0x81, 0xC4, 0xE8, 0x00, 0x00,
                0x00, 0x5F, 0x5E, 0x5B, 0x5D, 0xC3, 0x44, 0x89, 0x65, 0x97, 0xE8, 0xB0,
                0x19, 0x0F, 0xFF, 0x41, 0x8B, 0xCC, 0x4C, 0x8B, 0x88, 0x80, 0x03, 0x00,
                0x00, 0x89, 0x4C, 0x24, 0x28, 0x48, 0x8D, 0x4D, 0xC7, 0x4C, 0x8B, 0xC3,
                0xBA, 0x02, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x64, 0x24, 0x20, 0xE8, 0x9C,
                0x70, 0x1F, 0xFF, 0x48, 0x8D, 0x4D, 0xEF, 0xE8, 0x63, 0x70, 0xC4, 0x00,
                0x48, 0x8D, 0x4D, 0xF7, 0xE8, 0x5A, 0x70, 0xC4, 0x00, 0x48, 0x8D, 0x05,
                0x3B, 0x66, 0xD0, 0x01, 0x48, 0x8D, 0x4D, 0xC7, 0x0F, 0x57, 0xC0, 0x66,
                0x0F, 0x7F, 0x45, 0x07, 0x48, 0x89, 0x45, 0xC7, 0x44, 0x89, 0x65, 0xFF,
                0x44, 0x89, 0x65, 0x17, 0x44, 0x89, 0x65, 0x1F, 0xE8, 0x72, 0x19, 0x02,
                0x00,
            };

        // Player update (0xF06500) advances that same timer against
        // fPlayerAutoReloadDelaySec and submits the same action directly.
        // Native code clears the timer before this CALL; cleanup still runs
        // when our wrapper declines the action, without touching player memory.
        constexpr std::array<std::uint8_t, 184>
            kDelayedAutomaticReloadProducerSignature{
                0xF3, 0x41, 0x0F, 0x10, 0x8C, 0x24, 0x10, 0x10, 0x00, 0x00, 0x41, 0x0F,
                0x2F, 0xC8, 0x0F, 0x82, 0xC9, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x05,
                0x03, 0x4D, 0xCB, 0x04, 0xF3, 0x0F, 0x58, 0xC1, 0xF3, 0x41, 0x0F, 0x11,
                0x84, 0x24, 0x10, 0x10, 0x00, 0x00, 0x0F, 0x2F, 0x05, 0x62, 0x95, 0x8C,
                0x02, 0x0F, 0x82, 0xA6, 0x00, 0x00, 0x00, 0x41, 0xC7, 0x84, 0x24, 0x10,
                0x10, 0x00, 0x00, 0x00, 0x00, 0x80, 0xBF, 0x89, 0x5C, 0x24, 0x68, 0xE8,
                0x4F, 0x4B, 0x16, 0xFF, 0x4C, 0x8B, 0x88, 0x80, 0x03, 0x00, 0x00, 0x8B,
                0xCB, 0x48, 0x8D, 0x4D, 0x60, 0x4D, 0x8B, 0xC4, 0xBA, 0x02, 0x00, 0x00,
                0x00, 0x89, 0x5C, 0x24, 0x28, 0x48, 0x89, 0x5C, 0x24, 0x20, 0xE8, 0x3C,
                0xA2, 0x26, 0xFF, 0x48, 0x8D, 0x8D, 0x88, 0x00, 0x00, 0x00, 0xE8, 0x00,
                0xA2, 0xCB, 0x00, 0x48, 0x8D, 0x8D, 0x90, 0x00, 0x00, 0x00, 0xE8, 0xF4,
                0xA1, 0xCB, 0x00, 0x48, 0x8D, 0x05, 0xD5, 0x97, 0xD7, 0x01, 0x48, 0x8D,
                0x4D, 0x60, 0x0F, 0x57, 0xC0, 0x66, 0x0F, 0x7F, 0x85, 0xA0, 0x00, 0x00,
                0x00, 0x48, 0x89, 0x45, 0x60, 0x89, 0x9D, 0x98, 0x00, 0x00, 0x00, 0x89,
                0x9D, 0xB0, 0x00, 0x00, 0x00, 0x89, 0x9D, 0xB8, 0x00, 0x00, 0x00, 0xE8,
                0x03, 0x4B, 0x09, 0x00,
            };

        constexpr std::array<std::uint8_t, 15> kActionDataDispatcherSignature{
            0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0x76, 0x88,
            0xB6, 0x04, 0xE9, 0x01, 0x93, 0xED, 0xFF,
        };

        enum class AutomaticReloadSource : std::size_t
        {
            PostFire,
            Immediate,
            Delayed,
            Count,
        };

        constexpr auto kAutomaticReloadSourceCount =
            static_cast<std::size_t>(AutomaticReloadSource::Count);

        struct AutomaticReloadProducer
        {
            const char* name;
            std::uintptr_t signatureOffset;
            std::span<const std::uint8_t> signature;
            std::uintptr_t callsiteOffset;
            std::uintptr_t targetOffset;
        };

        constexpr std::array<AutomaticReloadProducer, kAutomaticReloadSourceCount>
            kAutomaticReloadProducers{{
                { "post-fire", offsets::kSignature_PlayerControls_AutomaticReload,
                    kAutomaticReloadProducerSignature,
                    offsets::kCallsite_PlayerControls_AutomaticReload,
                    offsets::kFunc_PlayerControls_DoAction },
                { "immediate", offsets::kSignature_PlayerCharacter_ImmediateAutomaticReload,
                    kImmediateAutomaticReloadProducerSignature,
                    offsets::kCallsite_PlayerCharacter_ImmediateAutomaticReload,
                    offsets::kFunc_TESActionData_Process },
                { "delayed", offsets::kSignature_PlayerCharacter_DelayedAutomaticReload,
                    kDelayedAutomaticReloadProducerSignature,
                    offsets::kCallsite_PlayerCharacter_DelayedAutomaticReload,
                    offsets::kFunc_TESActionData_Process },
            }};

        struct SuppressionCounters
        {
            std::atomic<std::uint64_t> total{ 0 };
            std::atomic<std::uint64_t> atLastReloadStart{ 0 };
        };

        std::atomic<NativeActionDispatcherFn> s_originalDispatcher{ nullptr };
        std::atomic<NativeActionDataDispatcherFn> s_originalActionDataDispatcher{ nullptr };
        std::atomic<NativeActionExecutorFn> s_originalActionExecutor{ nullptr };
        std::atomic<bool> s_hookInstalled{ false };
        std::atomic<bool> s_hookInstallFailed{ false };
        std::atomic<bool> s_actionObservationHookInstalled{ false };
        std::atomic<bool> s_actionObservationHookInstallFailed{ false };
        std::atomic<bool> s_runtimeEnabled{ false };
        std::array<std::atomic<std::uintptr_t>, kAutomaticReloadSourceCount>
            s_installedCallTargets{};
        std::atomic<std::uintptr_t> s_installedActionCallTarget{ 0 };
        std::array<SuppressionCounters, kAutomaticReloadSourceCount> s_suppressed{};
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
            const std::uintptr_t address,
            const std::span<const std::uint8_t> expected)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
            if (!bytes) {
                return false;
            }
            for (std::size_t index = 0;
                 index < expected.size();
                 ++index) {
                if (bytes[index] != expected[index]) {
                    return false;
                }
            }
            return true;
        }

        void recordSuppressedRequest(const AutomaticReloadSource source)
        {
            const auto index = static_cast<std::size_t>(source);
            const auto count = s_suppressed[index].total.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            // One immediate record per producer per process. Subsequent counts
            // are summarized on manual reload-start; empty-trigger polling is quiet.
            if (count == 1) {
                PAPER_LOG_INFO(Reload,
                    "Manual-reload-only blocked automatic reload: source={} total={}",
                    kAutomaticReloadProducers[index].name, count);
            }
        }

        void snapshotSuppressionCounters()
        {
            for (auto& counter : s_suppressed) {
                counter.atLastReloadStart.store(
                    counter.total.load(std::memory_order_acquire),
                    std::memory_order_release);
            }
        }

        bool onAutomaticReloadRequest(
            void* dispatcher,
            const std::int32_t actionId,
            const std::uint32_t priority)
        {
            if (s_runtimeEnabled.load(std::memory_order_acquire)) {
                recordSuppressedRequest(AutomaticReloadSource::PostFire);
                return false;
            }

            const auto original =
                s_originalDispatcher.load(std::memory_order_acquire);
            return original ? original(dispatcher, actionId, priority) : false;
        }

        bool onDirectAutomaticReloadRequest(
            const AutomaticReloadSource source, void* actionData)
        {
            if (s_runtimeEnabled.load(std::memory_order_acquire)) {
                recordSuppressedRequest(source);
                return false;
            }
            const auto original =
                s_originalActionDataDispatcher.load(std::memory_order_acquire);
            return original ? original(actionData) : false;
        }

        bool onImmediateAutomaticReloadRequest(void* actionData)
        {
            return onDirectAutomaticReloadRequest(
                AutomaticReloadSource::Immediate, actionData);
        }

        bool onDelayedAutomaticReloadRequest(void* actionData)
        {
            return onDirectAutomaticReloadRequest(
                AutomaticReloadSource::Delayed, actionData);
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

        const auto moduleBase = REL::Module::get().base();
        if (!matchesAutomaticReloadProducerSignature(
                moduleBase + offsets::kFunc_TESActionData_Process,
                kActionDataDispatcherSignature)) {
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only action-data dispatcher signature validation failed");
            s_hookInstallFailed.store(true, std::memory_order_release);
            return false;
        }

        // Validate all producer branches and their CALL targets before patching
        // any of them. The two direct paths never enter PlayerControls::DoAction.
        for (const auto& producer : kAutomaticReloadProducers) {
            const auto callsiteAddress = moduleBase + producer.callsiteOffset;
            const auto decodedTarget = decodeRelativeCallTarget(callsiteAddress);
            if (!matchesAutomaticReloadProducerSignature(
                    moduleBase + producer.signatureOffset, producer.signature) ||
                decodedTarget != moduleBase + producer.targetOffset) {
                PAPER_LOG_ERROR(Reload,
                    "Manual-reload-only hook validation failed source={} callsite=0x{:X} target=0x{:X} expected=0x{:X}",
                    producer.name, callsiteAddress, decodedTarget,
                    moduleBase + producer.targetOffset);
                s_hookInstallFailed.store(true, std::memory_order_release);
                return false;
            }
        }

        auto& trampoline = F4SE::GetTrampoline();
        // Three suppression wrappers plus the existing action observer each
        // use a 14-byte CommonLib write_call<5> relay; PAPER reserves 64 bytes.
        constexpr std::size_t kRequiredTrampolineBytes = 4 * 14;
        if (trampoline.free_size() < kRequiredTrampolineBytes) {
            s_hookInstallFailed.store(true, std::memory_order_release);
            PAPER_LOG_ERROR(Reload,
                "Manual-reload-only requires {} trampoline bytes; {} remain",
                kRequiredTrampolineBytes, trampoline.free_size());
            return false;
        }

        s_originalDispatcher.store(reinterpret_cast<NativeActionDispatcherFn>(
            moduleBase + offsets::kFunc_PlayerControls_DoAction), std::memory_order_release);
        s_originalActionDataDispatcher.store(reinterpret_cast<NativeActionDataDispatcherFn>(
            moduleBase + offsets::kFunc_TESActionData_Process), std::memory_order_release);
        const std::array<std::uintptr_t, kAutomaticReloadSourceCount> wrappers{
            reinterpret_cast<std::uintptr_t>(&onAutomaticReloadRequest),
            reinterpret_cast<std::uintptr_t>(&onImmediateAutomaticReloadRequest),
            reinterpret_cast<std::uintptr_t>(&onDelayedAutomaticReloadRequest),
        };
        const char* installingSource = "post-fire";
        try {
            for (std::size_t index = 0; index < kAutomaticReloadSourceCount; ++index) {
                const auto& producer = kAutomaticReloadProducers[index];
                installingSource = producer.name;
                const auto callsiteAddress = moduleBase + producer.callsiteOffset;
                const auto original = trampoline.write_call<5>(
                    callsiteAddress, wrappers[index]);
                const auto installedTarget = decodeRelativeCallTarget(callsiteAddress);
                if (original != moduleBase + producer.targetOffset || installedTarget == 0) {
                    // Suppression stays disabled if installation is incomplete.
                    // Any installed wrapper forwards to its validated original
                    // for the remainder of this process; never patch it twice.
                    s_hookInstallFailed.store(true, std::memory_order_release);
                    PAPER_LOG_ERROR(Reload,
                        "Manual-reload-only hook installation failed source={} original=0x{:X} expected=0x{:X} installed=0x{:X}; suppression remains disabled",
                        producer.name, original, moduleBase + producer.targetOffset,
                        installedTarget);
                    return false;
                }
                s_installedCallTargets[index].store(installedTarget, std::memory_order_release);
                PAPER_LOG_INFO(Reload,
                    "Installed validated automatic reload hook source={} callsite=0x{:X}",
                    producer.name, callsiteAddress);
            }
        } catch (const std::exception& error) {
            s_hookInstallFailed.store(true, std::memory_order_release);
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only hook installation failed source={}: {}",
                installingSource, error.what());
            return false;
        } catch (...) {
            s_hookInstallFailed.store(true, std::memory_order_release);
            PAPER_LOG_ERROR(
                Reload,
                "Manual-reload-only hook installation failed source={} with an unknown exception",
                installingSource);
            return false;
        }
        s_hookInstalled.store(true, std::memory_order_release);
        installActionExecutionObservationHook();

        PAPER_LOG_INFO(
            Reload,
            "Manual-reload-only automatic producers ready: post-fire, immediate, delayed; manual reload dispatchers remain untouched");
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
            snapshotSuppressionCounters();
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

        std::array<std::uint64_t, kAutomaticReloadSourceCount> totals{};
        std::array<std::uint64_t, kAutomaticReloadSourceCount> sincePreviousStart{};
        std::array<bool, kAutomaticReloadSourceCount> hooksIntact{};
        const auto moduleBase = REL::Module::get().base();
        for (std::size_t index = 0; index < kAutomaticReloadSourceCount; ++index) {
            const auto liveTarget = decodeRelativeCallTarget(
                moduleBase + kAutomaticReloadProducers[index].callsiteOffset);
            const auto installedTarget =
                s_installedCallTargets[index].load(std::memory_order_acquire);
            hooksIntact[index] = installedTarget != 0 && liveTarget == installedTarget;
            totals[index] = s_suppressed[index].total.load(std::memory_order_acquire);
            sincePreviousStart[index] = totals[index] -
                s_suppressed[index].atLastReloadStart.exchange(
                    totals[index], std::memory_order_acq_rel);
        }
        REL::Relocation<std::uintptr_t> actionCallsite{
            REL::Offset(offsets::kCallsite_PlayerControls_ExecuteAction)
        };
        const auto liveActionTarget =
            decodeRelativeCallTarget(actionCallsite.address());
        const auto installedActionTarget =
            s_installedActionCallTarget.load(std::memory_order_acquire);
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
            "Manual-reload-only reload-start diagnostic: suppressedSincePreviousStart(postFire/immediate/delayed)={}/{}/{} suppressedTotal(postFire/immediate/delayed)={}/{}/{} reloadActionsSincePreviousStart={} reloadActionsTotal={} suppressionHooksIntact(postFire/immediate/delayed)={}/{}/{} actionHookIntact={} lastReloadCaller=0x{:X} lastReloadCallerMainModule={} lastReloadCallerOffset=0x{:X}",
            sincePreviousStart[0], sincePreviousStart[1], sincePreviousStart[2],
            totals[0], totals[1], totals[2],
            reloadActionSequence - previousReloadActionSequence,
            reloadActionSequence,
            hooksIntact[0], hooksIntact[1], hooksIntact[2],
            installedActionTarget != 0 &&
                liveActionTarget == installedActionTarget,
            lastReloadActionCaller,
            lastReloadActionCallerIsMainModule,
            lastReloadActionCallerOffset);
    }

    void resetSession()
    {
        setRuntimeEnabled(false);
        snapshotSuppressionCounters();
        s_reloadActionExecutionSequenceAtLastReloadStart.store(
            s_reloadActionExecutionSequence.load(std::memory_order_acquire),
            std::memory_order_release);
    }
}
