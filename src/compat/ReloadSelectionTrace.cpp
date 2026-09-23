#include "PCH.h"

#include "compat/ReloadSelectionTrace.h"
#include "compat/MergedReloadCompatibility.h"
#include "compat/TacticalReloadBridge.h"
#include "PaperLog.h"
#include "support/Fo4VrRuntime.h"
#include "support/NativeMemory.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <utility>

namespace paper::reload_selection_trace
{
    namespace
    {
        using ProcessAction = bool (*)(void*);
        using Condition = RE::SCRIPT_FUNCTION::ConditionFunction_t*;
        constexpr std::uintptr_t kProcessCall = 0xFC0B7B;
        constexpr std::uintptr_t kProcessAction = 0xF9BEF0;
        constexpr std::uintptr_t kLoadedAmmo = 0x4C1C70;
        constexpr std::uintptr_t kHasKeyword = 0x4BAA70;
        constexpr std::uintptr_t kWornHasKeyword = 0x4BABF0;
        constexpr std::uintptr_t kHasPerk = 0x4B98B0;

        // Installation and snapshots belong to the PAPER frame thread.
        // Reset messages only disable the atomic gate. No engine pointer is
        // retained after dispatch or sent to the asynchronous logger worker.
        F4SE::Trampoline s_trampoline;
        std::atomic<ProcessAction> s_original{ nullptr };
        DWORD s_owner = 0;
        bool s_attempted = false;
        std::atomic<bool> s_enabled{ false };
        std::atomic<bool> s_faulted{ false };
        std::atomic<bool> s_wrongThreadReported{ false };
        std::uint64_t s_requests = 0;
        thread_local std::uint64_t s_activeRequest = 0;
        thread_local unsigned s_clipCount = 0;

        struct Inputs
        {
            std::uint32_t weapon = 0;
            const char* stage = "data-handler";
            // -1 means unavailable, never a false/zero native condition.
            float ammo = -1, reserve = -1, ignore = -1, manual = -1, perk = -1;
        };

        float evaluate(std::uintptr_t offset, RE::PlayerCharacter* player, void* parameter)
        {
            RE::ConditionCheckParams context{};
            context.actionRef = player;
            float value = -1;
            const auto condition = reinterpret_cast<Condition>(REL::Module::get().base() + offset);
            return condition(context, parameter, nullptr, value) && std::isfinite(value) && value >= 0 ? value : -1;
        }

        const char* captureInputs(RE::PlayerCharacter* player, Inputs& inputs)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return "data-handler";
            inputs.stage = "equipped-weapon";
            const auto* item = f4vr::getEquippedItem();
            if (!item || !item->item.object || item->item.object->formType != RE::ENUM_FORM_ID::kWEAP)
                return "equipped-weapon";
            inputs.weapon = item->item.object->formID;
            inputs.stage = "tr-forms";
            auto* reserve = data->LookupForm<RE::BGSKeyword>(0x1734, "TacticalReload.esm"sv);
            auto* ignore = data->LookupForm<RE::BGSKeyword>(0x1ED3, "TacticalReload.esm"sv);
            auto* manual = data->LookupForm<RE::BGSKeyword>(0x1ECF, "TacticalReload.esm"sv);
            auto* perk = data->LookupForm<RE::BGSPerk>(0x1ED0, "TacticalReload.esm"sv);
            inputs.stage = "GetLoadedAmmoCount";
            inputs.ammo = evaluate(kLoadedAmmo, player, nullptr);
            inputs.stage = "WornHasKeyword/AnimsReloadReserve";
            if (reserve) inputs.reserve = evaluate(kWornHasKeyword, player, reserve);
            inputs.stage = "HasKeyword/IgnoreReloadReserve";
            if (ignore) inputs.ignore = evaluate(kHasKeyword, player, ignore);
            inputs.stage = "HasKeyword/ManualReload";
            if (manual) inputs.manual = evaluate(kHasKeyword, player, manual);
            inputs.stage = "HasPerk/ManualReloadPerk";
            if (perk) inputs.perk = evaluate(kHasPerk, player, perk);
            if (!reserve || !ignore || !manual || !perk) return "tr-forms";
            if (inputs.ammo < 0 || inputs.reserve < 0 || inputs.ignore < 0 || inputs.manual < 0 || inputs.perk < 0)
                return "native-condition";
            return "complete";
        }

        const char* guardedCapture(RE::PlayerCharacter* player, Inputs& inputs) noexcept
        {
            __try {
                return captureInputs(player, inputs);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                s_faulted.store(true, std::memory_order_release);
                return inputs.stage;
            }
        }

        void reportFault() noexcept
        {
            s_faulted.store(true, std::memory_order_release);
            try {
                if (auto* log = merged_reload_compatibility::diagnosticLogger())
                    log->error("[PAPER::Compatibility] TR_SELECT disabled after diagnostic failure; native dispatch retained");
            } catch (...) {}
        }

        std::uint64_t beginRequest(void* actionData)
        {
            auto* log = merged_reload_compatibility::diagnosticLogger();
            if (!log || !tactical_reload_bridge::contractReady()) return 0;
            if (GetCurrentThreadId() != s_owner) {
                if (!s_wrongThreadReported.exchange(true, std::memory_order_relaxed))
                    log->warn("[PAPER::Compatibility] TR_SELECT unavailable: dispatcher is not on the PAPER frame thread");
                return 0;
            }
            // ActionInput ctor 0x1401715E0 and native action gate 0x140E77CF0
            // agree on actor +8 and BGSAction +18. Only ActionReload is traced.
            auto* player = RE::PlayerCharacter::GetSingleton();
            RE::TESObjectREFR* actor = nullptr;
            RE::TESForm* action = nullptr;
            if (!player) return 0;
            if (!native_memory::tryReadField(actionData, 0x08, actor)) {
                log->warn("[PAPER::Compatibility] TR_SELECT unavailable: action-data/actor");
                s_faulted.store(true, std::memory_order_release);
                return 0;
            }
            if (actor != player) return 0;
            if (!native_memory::tryReadField(actionData, 0x18, action)) {
                log->warn("[PAPER::Compatibility] TR_SELECT unavailable: action-data/action");
                s_faulted.store(true, std::memory_order_release);
                return 0;
            }
            if (!action || action != RE::TESForm::GetFormByID(0x4A56)) return 0;
            const auto request = ++s_requests;
            if (request > 64 && request % 128 != 0) return 0;
            Inputs inputs{};
            const auto* stage = guardedCapture(player, inputs);
            tactical_reload_bridge_policy::State bridge{};
            bool bridgeEnabled = false;
            const bool bridgeValid = tactical_reload_bridge::readDiagnosticState(bridge, bridgeEnabled);
            log->info("[PAPER::Compatibility] TR_SELECT before request={} weapon={:08X} loadedAmmo={} WornHasAnimsReloadReserve={} IgnoreReloadReserve={} ManualReload={} ManualReloadPerk={} bridgeValid={} bridgeEnabled={} transaction={} phase={} elapsed={:.3f}s keywordAddedByPaper={} stage={} traceFaulted={}",
                request, inputs.weapon, inputs.ammo, inputs.reserve, inputs.ignore, inputs.manual, inputs.perk,
                bridgeValid, bridgeEnabled, bridge.transactionSequence, static_cast<unsigned>(bridge.phase),
                bridge.elapsedSeconds, bridge.keywordAddedByPaper, stage, s_faulted.load(std::memory_order_acquire));
            return request;
        }

        bool onProcessAction(void* actionData)
        {
            std::uint64_t request = 0;
            if (s_enabled.load(std::memory_order_acquire) && !s_faulted.load(std::memory_order_acquire)) {
                try { request = beginRequest(actionData); }
                catch (...) { reportFault(); }
            }
            const auto previousRequest = std::exchange(s_activeRequest, request);
            const auto previousClips = std::exchange(s_clipCount, 0u);
            // Observe only. The native call executes exactly once with its
            // original argument, and its result is returned unchanged.
            const bool result = s_original.load(std::memory_order_acquire)(actionData);
            const auto clips = std::exchange(s_clipCount, previousClips);
            s_activeRequest = previousRequest;
            if (request) {
                try {
                    merged_reload_compatibility::diagnosticLogger()->info(
                        "[PAPER::Compatibility] TR_SELECT after request={} accepted={} synchronousPlayerClips={}", request, result, clips);
                } catch (...) { reportFault(); }
            }
            return result;
        }

        template <std::size_t N>
        bool matches(std::uintptr_t offset, const std::array<std::uint8_t, N>& bytes)
        {
            std::array<std::uint8_t, N> actual{};
            return native_memory::guardedCopyFromMemory(
                reinterpret_cast<const void*>(REL::Module::get().base() + offset), actual.data(), N) && actual == bytes;
        }
    }

    bool installHook()
    {
        if (s_original.load(std::memory_order_acquire)) return true;
        if (s_attempted || !tactical_reload_bridge::contractReady() ||
            !logger::instance || !logger::instance->should_log(spdlog::level::debug)) return false;
        s_attempted = true;
        // DoAction 0x140FC07E0 constructs TESActionData and calls Process here
        // BEFORE 0x140E75360 selects the idle and 0x140E753F0 dispatches it.
        // SCRIPT records (indices 756/560/682/448) identify these condition
        // handlers; their raw code agrees on RCX->subject, RDX parameter,
        // R9 float output. Use native equipped-instance keyword/ammo semantics.
        const bool signatures =
            matches(kProcessCall - 4, std::array<std::uint8_t, 11>{ 0x48,0x8D,0x4D,0x80,0xE8,0x70,0xB3,0xFD,0xFF,0x84,0xC0 }) &&
            matches(kProcessAction, std::array<std::uint8_t, 15>{ 0x48,0x8B,0xD1,0x48,0x8B,0x0D,0x76,0x88,0xB6,0x04,0xE9,0x01,0x93,0xED,0xFF }) &&
            matches(kLoadedAmmo, std::array<std::uint8_t, 18>{ 0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x33,0xDB,0x49,0x8B,0xF9,0x41,0x89,0x19 }) &&
            matches(kHasKeyword, std::array<std::uint8_t, 16>{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x41,0xC7,0x01,0,0,0,0,0x48,0x8B,0x09 }) &&
            matches(kWornHasKeyword, std::array<std::uint8_t, 16>{ 0x40,0x53,0x48,0x83,0xEC,0x30,0x41,0xC7,0x01,0,0,0,0,0x4C,0x8B,0x01 }) &&
            matches(kHasPerk, std::array<std::uint8_t, 17>{ 0x40,0x57,0x48,0x83,0xEC,0x20,0x33,0xC0,0x49,0x8B,0xF9,0x41,0x89,0x01,0x4C,0x8B,0x01 });
        if (!REL::Module::IsVR() || REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72 ||
            !signatures || !merged_reload_compatibility::diagnosticLogger()) {
            PAPER_LOG_ERROR(Compatibility, "TR_SELECT hook rejected: native signature or asynchronous logger unavailable");
            return false;
        }
        s_trampoline.create(16);
        s_owner = GetCurrentThreadId();
        s_original.store(reinterpret_cast<ProcessAction>(REL::Module::get().base() + kProcessAction), std::memory_order_release);
        s_trampoline.write_call<5>(REL::Module::get().base() + kProcessCall, &onProcessAction);
        PAPER_LOG_INFO(Compatibility, "TR_SELECT pre-selection trace installed: debug only, first 64 requests then every 128; condition -1=unavailable; phases 0=idle 1=press-armed 2=awaiting-start 3=reload-active");
        return true;
    }

    void setRuntimeEnabled(bool enabled)
    {
        s_enabled.store(enabled && logger::instance && logger::instance->should_log(spdlog::level::debug), std::memory_order_release);
    }

    void observeClip(const char* name) noexcept
    {
        if (!s_activeRequest || s_clipCount >= 16) return;
        ++s_clipCount;
        try {
            merged_reload_compatibility::diagnosticLogger()->info(
                "[PAPER::Compatibility] TR_SELECT clip request={} name='{}'", s_activeRequest, name);
        } catch (...) { reportFault(); }
    }
}
