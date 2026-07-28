#include "PCH.h"

#include "compat/TacticalReloadBridge.h"

#include "compat/TacticalReloadBridgePolicy.h"
#include "native/NativeOffsets.h"
#include "PaperLog.h"
#include "support/Fo4VrRuntime.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace paper::tactical_reload_bridge
{
    namespace
    {
        using ReadyWeaponShouldHandleEventFn =
            bool (*)(void* handler, const RE::InputEvent* event);
        using PolicyState = tactical_reload_bridge_policy::State;
        using PolicyStep = tactical_reload_bridge_policy::Step;

        constexpr std::string_view kTacticalReloadPlugin =
            "TacticalReload.esm";
        constexpr RE::TESFormID kAnimsReloadReserveLocalId = 0x001734;
        constexpr RE::TESFormID kManualReloadLocalId = 0x001ECF;
        constexpr RE::TESFormID kIgnoreReloadReserveLocalId = 0x001ED3;
        constexpr RE::TESFormID kAnimsReloadReserve3rdLocalId = 0x001ED7;

        struct SignatureByte
        {
            std::size_t offset;
            std::uint8_t value;
        };

        // Fallout4VR.exe 1.2.72 ReadyWeaponHandler::ShouldHandleEvent.
        // RIP-relative displacements are deliberately excluded.
        constexpr std::array<SignatureByte, 20>
            kReadyWeaponShouldHandleSignature{
                SignatureByte{ 0, 0x40 },
                SignatureByte{ 1, 0x57 },
                SignatureByte{ 2, 0x48 },
                SignatureByte{ 3, 0x83 },
                SignatureByte{ 4, 0xEC },
                SignatureByte{ 5, 0x20 },
                SignatureByte{ 6, 0x48 },
                SignatureByte{ 7, 0x8B },
                SignatureByte{ 8, 0x0D },
                SignatureByte{ 13, 0x48 },
                SignatureByte{ 14, 0x8B },
                SignatureByte{ 15, 0xFA },
                SignatureByte{ 16, 0xE8 },
                SignatureByte{ 21, 0x84 },
                SignatureByte{ 22, 0xC0 },
                SignatureByte{ 23, 0x0F },
                SignatureByte{ 24, 0x84 },
                SignatureByte{ 29, 0x48 },
                SignatureByte{ 30, 0x89 },
                SignatureByte{ 31, 0x74 },
            };

        std::atomic<bool> s_contractReady{ false };
        std::atomic<bool> s_cleanSessionBaseline{ false };
        std::atomic<bool> s_runtimeEnabled{ false };
        std::atomic<bool> s_hookInstalled{ false };
        std::atomic<bool> s_hookInstallFailed{ false };
        std::atomic<bool> s_runtimeFault{ false };
        std::atomic<bool> s_baselineDeferralLogged{ false };
        std::atomic<bool> s_threadMismatchLogged{ false };
        std::atomic<DWORD> s_ownerThreadId{ 0 };
        std::atomic<RE::BGSKeyword*> s_manualReloadKeyword{ nullptr };
        ReadyWeaponShouldHandleEventFn s_originalShouldHandleEvent{ nullptr };
        PolicyState s_state{};

        [[nodiscard]] bool requireOwnerThread(const char* operation)
        {
            const DWORD currentThread = GetCurrentThreadId();
            DWORD expected = 0;
            if (s_ownerThreadId.compare_exchange_strong(
                    expected,
                    currentThread,
                    std::memory_order_acq_rel)) {
                return true;
            }
            if (expected == currentThread) {
                return true;
            }
            if (!s_threadMismatchLogged.exchange(
                    true,
                    std::memory_order_acq_rel)) {
                PAPER_LOG_ERROR(
                    Compatibility,
                    "Tactical Reload bridge rejected {} on thread {} (owner={})",
                    operation,
                    currentThread,
                    expected);
            }
            return false;
        }

        [[nodiscard]] bool matchesReadyWeaponShouldHandleSignature(
            const std::uintptr_t address) noexcept
        {
            if (address == 0) {
                return false;
            }
            const auto* bytes =
                reinterpret_cast<const std::uint8_t*>(address);
            for (const auto& expected : kReadyWeaponShouldHandleSignature) {
                if (bytes[expected.offset] != expected.value) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool tryReadActorStateStorage(
            const RE::Actor* actor,
            std::uint32_t& outStorage) noexcept
        {
            if (!actor) {
                return false;
            }
#if defined(_MSC_VER)
            __try {
#endif
                const auto* actorState =
                    static_cast<const RE::ActorState*>(actor);
                std::memcpy(
                    &outStorage,
                    reinterpret_cast<const std::byte*>(actorState) +
                        tactical_reload_bridge_policy::
                            kActorStateStorageOffset,
                    sizeof(outStorage));
                return true;
#if defined(_MSC_VER)
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
#endif
        }

        [[nodiscard]] bool tryHasManualReloadKeyword(
            RE::PlayerCharacter* player,
            RE::BGSKeyword* keyword,
            bool& outPresent) noexcept
        {
            if (!player || !keyword) {
                return false;
            }
#if defined(_MSC_VER)
            __try {
#endif
                auto* reference = static_cast<RE::TESObjectREFR*>(player);
                const auto* keywordForm =
                    static_cast<const RE::IKeywordFormBase*>(reference);
                outPresent = keywordForm->HasKeyword(keyword, nullptr);
                return true;
#if defined(_MSC_VER)
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
#endif
        }

        [[nodiscard]] bool tryMutateManualReloadKeyword(
            RE::PlayerCharacter* player,
            RE::BGSKeyword* keyword,
            const bool add) noexcept
        {
            if (!player || !keyword) {
                return false;
            }
#if defined(_MSC_VER)
            __try {
#endif
                if (add) {
                    player->AddKeyword(keyword);
                } else {
                    player->RemoveKeyword(keyword);
                }
                return true;
#if defined(_MSC_VER)
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
#endif
        }

        void recordRuntimeFault(const char* operation)
        {
            s_runtimeEnabled.store(false, std::memory_order_release);
            s_runtimeFault.store(true, std::memory_order_release);
            PAPER_LOG_ERROR(
                Compatibility,
                "Tactical Reload bridge disabled after {} failed; shipped reload handling remains untouched",
                operation);
        }

        [[nodiscard]] bool applyPolicyStep(
            const PolicyStep& step,
            const char* operation)
        {
            if (step.addKeyword || step.removeKeyword) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* keyword =
                    s_manualReloadKeyword.load(std::memory_order_acquire);
                if (!tryMutateManualReloadKeyword(
                        player,
                        keyword,
                        step.addKeyword)) {
                    recordRuntimeFault(operation);
                    return false;
                }
            }

            s_state = step.state;
            switch (step.reason) {
            case tactical_reload_bridge_policy::StepReason::
                ManualIntentArmed:
                PAPER_LOG_DEBUG(
                    Compatibility,
                    "Tactical Reload transaction {} armed keywordPreExisting={} keywordAddedByPaper={}",
                    step.state.transactionSequence,
                    step.state.keywordPresentAtArm,
                    step.state.keywordAddedByPaper);
                break;
            case tactical_reload_bridge_policy::StepReason::ButtonReleased:
            case tactical_reload_bridge_policy::StepReason::ReloadStarted:
            case tactical_reload_bridge_policy::StepReason::ReloadEnded:
                PAPER_LOG_DEBUG(
                    Compatibility,
                    "Tactical Reload transaction {} state={}",
                    step.state.transactionSequence,
                    tactical_reload_bridge_policy::stepReasonName(
                        step.reason));
                break;
            case tactical_reload_bridge_policy::StepReason::
                PressHoldExpired:
            case tactical_reload_bridge_policy::StepReason::
                ReloadStartExpired:
            case tactical_reload_bridge_policy::StepReason::
                ReloadActiveExpired:
                PAPER_LOG_WARN(
                    Compatibility,
                    "Tactical Reload transaction {} recovered through {}",
                    step.state.transactionSequence,
                    tactical_reload_bridge_policy::stepReasonName(
                        step.reason));
                break;
            default:
                break;
            }
            return true;
        }

        [[nodiscard]] bool playerReadyForManualReload()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* equippedItem = f4vr::getEquippedItem();
            auto* equippedForm =
                equippedItem ? equippedItem->item.object : nullptr;
            if (!player || !equippedForm ||
                equippedForm->formType != RE::ENUM_FORM_ID::kWEAP ||
                !equippedForm->As<RE::TESObjectWEAP>()) {
                return false;
            }

            std::uint32_t actorStateStorage = 0;
            return tryReadActorStateStorage(
                       player,
                       actorStateStorage) &&
                   tactical_reload_bridge_policy::
                       isWeaponReadyForManualReload(
                           tactical_reload_bridge_policy::
                               decodeWeaponState(actorStateStorage));
        }

        void processAcceptedButtonEvent(const RE::ButtonEvent& event)
        {
            if (tactical_reload_bridge_policy::isJustPressed(
                    event.QAnalogValue(),
                    event.QHeldDownSecs())) {
                if (!playerReadyForManualReload()) {
                    return;
                }

                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* keyword =
                    s_manualReloadKeyword.load(std::memory_order_acquire);
                bool keywordPresent = false;
                if (!tryHasManualReloadKeyword(
                        player,
                        keyword,
                        keywordPresent)) {
                    recordRuntimeFault("ManualReload presence query");
                    return;
                }
                (void)applyPolicyStep(
                    tactical_reload_bridge_policy::armManualIntent(
                        s_state,
                        true,
                        keywordPresent),
                    "ManualReload add");
                return;
            }

            if (tactical_reload_bridge_policy::isReleased(
                    event.QAnalogValue(),
                    event.QHeldDownSecs())) {
                (void)applyPolicyStep(
                    tactical_reload_bridge_policy::releaseManualIntent(
                        s_state),
                    "button release");
            }
        }

        bool onReadyWeaponShouldHandleEvent(
            void* handler,
            const RE::InputEvent* event)
        {
            const auto original = s_originalShouldHandleEvent;
            if (!original) {
                return false;
            }

            // The native predicate is authoritative for device mapping,
            // handedness, menus, and control state. It is called exactly once.
            const bool accepted = original(handler, event);
            if (!accepted ||
                !s_runtimeEnabled.load(std::memory_order_acquire) ||
                !s_contractReady.load(std::memory_order_acquire) ||
                s_runtimeFault.load(std::memory_order_acquire) ||
                !requireOwnerThread("accepted input")) {
                return accepted;
            }

            const auto* button =
                event ? event->As<RE::ButtonEvent>() : nullptr;
            if (button) {
                processAcceptedButtonEvent(*button);
            }
            return accepted;
        }

        [[nodiscard]] bool establishCleanSessionBaseline()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* keyword =
                s_manualReloadKeyword.load(std::memory_order_acquire);
            bool keywordPresent = false;
            if (!player || !keyword ||
                !tryHasManualReloadKeyword(
                    player,
                    keyword,
                    keywordPresent)) {
                return false;
            }
            if (!keywordPresent) {
                return true;
            }

            std::uint32_t actorStateStorage = 0;
            if (!tryReadActorStateStorage(player, actorStateStorage)) {
                return false;
            }

            const bool nativeReloading =
                tactical_reload_bridge_policy::isReloading(
                    tactical_reload_bridge_policy::decodeGunState(
                        actorStateStorage));
            if (nativeReloading &&
                !s_baselineDeferralLogged.exchange(
                    true,
                    std::memory_order_acq_rel)) {
                PAPER_LOG_INFO(
                    Compatibility,
                    "Preserved ManualReload while the native player gun state is reloading; bridge activation is deferred");
            }
            if (nativeReloading) {
                return false;
            }

            if (!tryMutateManualReloadKeyword(player, keyword, false)) {
                recordRuntimeFault(
                    "stale ManualReload session normalization");
                return false;
            }
            PAPER_LOG_INFO(
                Compatibility,
                "Removed stale transient ManualReload keyword at session initialization");
            return true;
        }
    }

    void initializeSession()
    {
        if (!requireOwnerThread("session initialization")) {
            s_contractReady.store(false, std::memory_order_release);
            return;
        }

        s_runtimeEnabled.store(false, std::memory_order_release);
        s_state = {};
        s_manualReloadKeyword.store(nullptr, std::memory_order_release);
        s_contractReady.store(false, std::memory_order_release);
        s_cleanSessionBaseline.store(false, std::memory_order_release);
        s_baselineDeferralLogged.store(false, std::memory_order_release);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            PAPER_LOG_WARN(
                Compatibility,
                "Tactical Reload bridge inactive: TESDataHandler is unavailable");
            return;
        }

        auto* animsReloadReserve =
            dataHandler->LookupForm<RE::BGSKeyword>(
                kAnimsReloadReserveLocalId,
                kTacticalReloadPlugin);
        auto* manualReload =
            dataHandler->LookupForm<RE::BGSKeyword>(
                kManualReloadLocalId,
                kTacticalReloadPlugin);
        auto* ignoreReloadReserve =
            dataHandler->LookupForm<RE::BGSKeyword>(
                kIgnoreReloadReserveLocalId,
                kTacticalReloadPlugin);
        auto* animsReloadReserve3rd =
            dataHandler->LookupForm<RE::BGSKeyword>(
                kAnimsReloadReserve3rdLocalId,
                kTacticalReloadPlugin);
        if (!animsReloadReserve || !manualReload ||
            !ignoreReloadReserve || !animsReloadReserve3rd) {
            PAPER_LOG_INFO(
                Compatibility,
                "Tactical Reload bridge inactive: supported TacticalReload.esm keyword contract is absent or malformed");
            return;
        }

        s_manualReloadKeyword.store(
            manualReload,
            std::memory_order_release);
        s_contractReady.store(true, std::memory_order_release);
        PAPER_LOG_INFO(
            Compatibility,
            "Tactical Reload 1.4 contract validated ManualReload={:08X} AnimsReloadReserve={:08X} IgnoreReloadReserve={:08X} AnimsReloadReserve3rd={:08X}",
            manualReload->formID,
            animsReloadReserve->formID,
            ignoreReloadReserve->formID,
            animsReloadReserve3rd->formID);
        s_cleanSessionBaseline.store(
            establishCleanSessionBaseline(),
            std::memory_order_release);
    }

    void resetSession()
    {
        s_runtimeEnabled.store(false, std::memory_order_release);
        if (requireOwnerThread("session reset")) {
            (void)applyPolicyStep(
                tactical_reload_bridge_policy::reset(
                    s_state,
                    tactical_reload_bridge_policy::StepReason::
                        SessionReset),
                "session-reset ManualReload cleanup");
            s_state = {};
        }
        s_contractReady.store(false, std::memory_order_release);
        s_cleanSessionBaseline.store(false, std::memory_order_release);
        s_baselineDeferralLogged.store(false, std::memory_order_release);
        s_manualReloadKeyword.store(nullptr, std::memory_order_release);
    }

    bool contractReady()
    {
        return s_contractReady.load(std::memory_order_acquire);
    }

    bool installInputHook()
    {
        if (s_hookInstalled.load(std::memory_order_acquire)) {
            return s_originalShouldHandleEvent != nullptr;
        }
        if (!contractReady()) {
            return true;
        }
        if (s_hookInstallFailed.load(std::memory_order_acquire) ||
            !requireOwnerThread("hook installation")) {
            return false;
        }
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() !=
                F4SE::RUNTIME_VR_1_2_72) {
            PAPER_LOG_ERROR(
                Compatibility,
                "Tactical Reload input hook rejected a non-FO4VR-1.2.72 executable layout");
            s_hookInstallFailed.store(true, std::memory_order_release);
            return false;
        }

        REL::Relocation<std::uintptr_t> entry{
            REL::Offset(
                offsets::
                    kVtableEntry_ReadyWeaponHandler_ShouldHandleEvent)
        };
        REL::Relocation<std::uintptr_t> expectedTarget{
            REL::Offset(
                offsets::
                    kFunc_ReadyWeaponHandler_ShouldHandleEvent)
        };
        auto* slot = reinterpret_cast<std::uintptr_t*>(entry.address());
        if (!slot || *slot != expectedTarget.address() ||
            !matchesReadyWeaponShouldHandleSignature(
                expectedTarget.address())) {
            PAPER_LOG_ERROR(
                Compatibility,
                "Tactical Reload ReadyWeapon hook validation failed slot=0x{:X} expected=0x{:X} found=0x{:X} signature={}",
                entry.address(),
                expectedTarget.address(),
                slot ? *slot : 0,
                matchesReadyWeaponShouldHandleSignature(
                    expectedTarget.address()));
            s_hookInstallFailed.store(true, std::memory_order_release);
            return false;
        }

        s_originalShouldHandleEvent =
            reinterpret_cast<ReadyWeaponShouldHandleEventFn>(*slot);
        DWORD oldProtect = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                PAGE_EXECUTE_READWRITE,
                &oldProtect)) {
            PAPER_LOG_ERROR(
                Compatibility,
                "Tactical Reload ReadyWeapon hook VirtualProtect failed at 0x{:X}",
                entry.address());
            s_originalShouldHandleEvent = nullptr;
            s_hookInstallFailed.store(true, std::memory_order_release);
            return false;
        }

        *slot = reinterpret_cast<std::uintptr_t>(
            &onReadyWeaponShouldHandleEvent);
        FlushInstructionCache(
            GetCurrentProcess(),
            slot,
            sizeof(*slot));
        DWORD unusedProtect = 0;
        if (!VirtualProtect(
                slot,
                sizeof(*slot),
                oldProtect,
                &unusedProtect)) {
            PAPER_LOG_WARN(
                Compatibility,
                "Tactical Reload ReadyWeapon hook installed at 0x{:X}, but restoring page protection failed",
                entry.address());
        }

        s_hookInstalled.store(true, std::memory_order_release);
        PAPER_LOG_INFO(
            Compatibility,
            "Installed validated FO4VR ReadyWeapon accepted-input bridge at 0x{:X}, original=0x{:X}",
            entry.address(),
            reinterpret_cast<std::uintptr_t>(
                s_originalShouldHandleEvent));
        return true;
    }

    void setRuntimeEnabled(const bool enabled)
    {
        const bool effectiveEnabled =
            enabled &&
            s_contractReady.load(std::memory_order_acquire) &&
            s_cleanSessionBaseline.load(std::memory_order_acquire) &&
            s_hookInstalled.load(std::memory_order_acquire) &&
            !s_runtimeFault.load(std::memory_order_acquire);
        const bool wasEnabled = s_runtimeEnabled.exchange(
            effectiveEnabled,
            std::memory_order_acq_rel);
        if (wasEnabled && !effectiveEnabled &&
            requireOwnerThread("runtime disable")) {
            (void)applyPolicyStep(
                tactical_reload_bridge_policy::reset(
                    s_state,
                    tactical_reload_bridge_policy::StepReason::
                        RuntimeDisabled),
                "runtime-disable ManualReload cleanup");
        }
    }

    void beginFrame(const float deltaSeconds)
    {
        if (!requireOwnerThread("frame advance")) {
            return;
        }
        if (s_contractReady.load(std::memory_order_acquire) &&
            !s_cleanSessionBaseline.load(std::memory_order_acquire) &&
            !s_runtimeFault.load(std::memory_order_acquire) &&
            establishCleanSessionBaseline()) {
            s_cleanSessionBaseline.store(
                true,
                std::memory_order_release);
            PAPER_LOG_INFO(
                Compatibility,
                "Tactical Reload bridge established a clean ManualReload session baseline");
        }
        (void)applyPolicyStep(
            tactical_reload_bridge_policy::advance(
                s_state,
                deltaSeconds,
                s_runtimeEnabled.load(std::memory_order_acquire)),
            "watchdog ManualReload cleanup");
    }

    void notifyPlayerReloadStart()
    {
        if (!s_runtimeEnabled.load(std::memory_order_acquire) ||
            !requireOwnerThread("reload start")) {
            return;
        }
        (void)applyPolicyStep(
            tactical_reload_bridge_policy::observeReloadStart(s_state),
            "reload-start ManualReload cleanup");
    }

    void notifyPlayerReloadEnd()
    {
        if (!s_runtimeEnabled.load(std::memory_order_acquire) ||
            !requireOwnerThread("reload end")) {
            return;
        }
        (void)applyPolicyStep(
            tactical_reload_bridge_policy::observeReloadEnd(s_state),
            "reload-end ManualReload cleanup");
    }
}
