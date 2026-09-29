#include "attachment_bindings/AttachmentBindings.h"
#include "PaperLog.h"
#include "support/Fo4VrRuntime.h"
#include "support/NativeMemory.h"

#include <Windows.h>
#include <array>

namespace paper::attachment_bindings
{
    namespace
    {
        using DispatchKeyword = void (*)(RE::NiAVObject*, RE::BGSKeyword*);
        // FO4VR: raw callers 0x14030A210/0x14030D180 pass scene root in RCX,
        // selected instance keyword in RDX. 0x1401B92F0 checks type 8 before
        // dispatching remapNode -> 0x1401B9550 -> 0x1401B9C60. The last function
        // attaches the branch and creates native ChildOrigin ownership data.
        constexpr std::uintptr_t kDispatchKeyword = 0x1B92F0;
        constexpr std::array<std::uint8_t, 25> kDispatcherPrefix{
            0x48,0x85,0xC9,0x0F,0x84,0x4A,0x02,0x00,0x00,0x55,0x53,0x57,
            0x48,0x8B,0xEC,0x48,0x83,0xEC,0x70,0x48,0x8B,0xDA,0x48,0x8B,0xF9
        };
        DWORD s_ownerThread{0};
        policy::Identity s_observed{};
        bool s_checked{false}, s_available{false}, s_faulted{false};

        bool validateDispatcher()
        {
            if (!s_checked) {
                s_checked = true;
                std::array<std::uint8_t, kDispatcherPrefix.size()> actual{};
                const auto address = REL::Module::get().base() + kDispatchKeyword;
                s_available = native_memory::guardedCopyFromMemory(
                    reinterpret_cast<const void*>(address), actual.data(), actual.size()) && actual == kDispatcherPrefix;
                if (!s_available) PAPER_LOG_ERROR(Animation, "ATTACHMENT_BIND native keyword dispatcher signature mismatch; repair disabled");
            }
            return s_available;
        }

        std::string_view nodeName(const RE::NiAVObject* node)
        {
            return node && node->name.c_str() ? std::string_view{node->name.c_str()} : std::string_view{};
        }

        void reconcileImpl(const policy::Identity& identity, const char*& stage)
        {
            stage = "equipped-item";
            auto* root = reinterpret_cast<RE::NiAVObject*>(identity.root);
            auto* equipped = f4vr::getEquippedItem();
            auto* weapon = equipped && equipped->item.object ? equipped->item.object->As<RE::TESObjectWEAP>() : nullptr;
            if (!weapon || weapon->formID != identity.form) return;
            const auto* instance = equipped->item.instanceData ?
                equipped->item.instanceData.get() : &weapon->weaponData;
            if (reinterpret_cast<std::uintptr_t>(instance) != identity.instance) return;
            stage = "instance-keywords";
            const auto* keywords = instance->GetKeywordData();
            if (!native_memory::pointerRangeLooksReadable(root, sizeof(RE::NiAVObject)) || !root->IsNode()) {
                PAPER_LOG_WARN(Animation, "ATTACHMENT_BIND weapon={:08X} stage=invalid-scene-root", identity.form);
                return;
            }
            if (!keywords || keywords->numKeywords > 512 ||
                (keywords->numKeywords && !native_memory::pointerRangeLooksReadable(
                    keywords->keywords, keywords->numKeywords * sizeof(RE::BGSKeyword*)))) {
                PAPER_LOG_WARN(Animation, "ATTACHMENT_BIND weapon={:08X} stage=invalid-keyword-data", identity.form);
                return;
            }
            // Collect before mutating: conflicts must never depend on keyword order.
            struct Candidate { RE::BGSKeyword* keyword; policy::RemapNames names; };
            std::array<Candidate, 16> candidates{};
            std::size_t count = 0;
            for (std::uint32_t i = 0; i < keywords->numKeywords; ++i) {
                stage = "keyword-name";
                auto* keyword = keywords->keywords[i];
                if (!keyword || !keyword->formEditorID.c_str()) continue;
                const auto names = policy::parseNodeRemap(keyword->formEditorID.c_str());
                if (!names) continue;
                bool duplicate = false;
                for (std::size_t j = 0; j < count; ++j) {
                    if (candidates[j].names.node != names->node) continue;
                    if (candidates[j].names.connection != names->connection) {
                        PAPER_LOG_WARN(Animation, "ATTACHMENT_BIND weapon={:08X} stage=conflicting-remaps node={}", identity.form, names->node);
                        return;
                    }
                    duplicate = true;
                }
                if (duplicate) continue;
                if (count == candidates.size()) {
                    PAPER_LOG_WARN(Animation, "ATTACHMENT_BIND weapon={:08X} stage=remap-limit", identity.form);
                    return;
                }
                candidates[count++] = {keyword, *names};
            }
            PAPER_LOG_INFO(Animation, "ATTACHMENT_BIND weapon={:08X}/{:016X} root={:016X} keywords={} declarations={}",
                identity.form, identity.weaponGeneration, identity.root, keywords->numKeywords, count);
            // ChildOrigin is established by constructor 0x1401BA090 and string
            // initializer 0x142B24BE0. Native ownership survives PAPER resets;
            // the engine releases it with the weapon's scene/attachment lifetime.
            const RE::BSFixedString originName{"ChildOrigin"};
            for (std::size_t i = 0; i < count; ++i) {
                const auto& candidate = candidates[i];
                stage = "source-search";
                const auto found = policy::findUnique(root, candidate.names.node, nodeName,
                    [](RE::NiAVObject* object) -> std::size_t {
                        auto* node = object->IsNode(); return node ? node->children.size() : 0;
                    },
                    [](RE::NiAVObject* object, std::size_t index) {
                        // findUnique rejects child lists above its 512-slot bound.
                        return object->IsNode()->children[static_cast<std::uint16_t>(index)].get();
                    });
                if (!found.complete || found.ambiguous || !found.match || !found.match->IsNode()) {
                    PAPER_LOG_WARN(Animation, "ATTACHMENT_BIND weapon={:08X}/{:016X} node={} target={} stage=source-search complete={} ambiguous={} found={} visited={}",
                        identity.form, identity.weaponGeneration, candidate.names.node, candidate.names.connection,
                        found.complete, found.ambiguous, found.match != nullptr, found.visited);
                    continue;
                }
                const RE::NiPointer<RE::NiAVObject> source{found.match};
                stage = "native-origin-before";
                if (source->GetExtraData(originName)) {
                    PAPER_LOG_INFO(Animation, "ATTACHMENT_BIND weapon={:08X}/{:016X} node={} target={} status=already-native parent={}",
                        identity.form, identity.weaponGeneration, candidate.names.node, candidate.names.connection, nodeName(source->parent));
                    continue;
                }
                if (!validateDispatcher()) return;
                const RE::BSFixedString beforeParent{source->parent ? source->parent->name : RE::BSFixedString{}};
                const auto* originalParent = source->parent;
                const auto originalLocal = source->local;
                const auto dispatch = reinterpret_cast<DispatchKeyword>(REL::Module::get().base() + kDispatchKeyword);
                stage = "native-dispatch";
                dispatch(root, candidate.keyword);
                stage = "native-origin-after";
                const bool bound = source->GetExtraData(originName) != nullptr;
                // The engine writes the connection translation before resolving
                // its parent. An unresolved remap must not leave that partial edit.
                if (!bound && source->parent == originalParent) source->local = originalLocal;
                PAPER_LOG_INFO(Animation, "ATTACHMENT_BIND weapon={:08X}/{:016X} node={} target={} status={} parentBefore={} parentAfter={}",
                    identity.form, identity.weaponGeneration, candidate.names.node, candidate.names.connection,
                    bound ? "native-remap-applied" : "native-remap-unresolved", beforeParent.c_str(), nodeName(source->parent));
                stage = "scene-world-update";
                f4vr::updateTransformsDown(source.get(), true);
            }
        }

        bool guardedReconcile(const policy::Identity& identity, const char*& stage)
        {
            __try { reconcileImpl(identity, stage); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    void reconcile(const policy::Identity& identity) noexcept
    {
        if (!identity.valid() || s_faulted) return;
        const auto thread = GetCurrentThreadId();
        if (!s_ownerThread) s_ownerThread = thread;
        if (s_ownerThread != thread || s_observed == identity) return;
        s_observed = identity;
        const char* stage = "begin";
        try {
            if (guardedReconcile(identity, stage)) return;
        } catch (...) {}
        s_faulted = true;
        PAPER_LOG_ERROR(Animation, "ATTACHMENT_BIND weapon={:08X} stage={} fault; repair disabled for this session", identity.form, stage);
    }

    void reset() noexcept
    {
        s_observed = {};
        s_ownerThread = 0;
        s_faulted = false;
    }
}
