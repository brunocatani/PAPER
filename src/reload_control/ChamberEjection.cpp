#include "reload_control/ChamberEjection.h"

#include "PaperLog.h"
#include "api/RockApiClient.h"
#include "native/NativeOffsets.h"
#include "support/Fo4VrRuntime.h"

#include <F4SE/API.h>
#include <F4SE/Interfaces.h>

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace paper::chamber_ejection
{
    namespace
    {
        using Eject = void (*)(RE::TESObjectREFR*, const void*, std::uint32_t);
        using Debris = void* (*)(void*, RE::TESObjectCELL*, float, const char*, RE::TESObjectREFR*, const RE::NiPoint3*,
            const RE::NiMatrix3*, const RE::NiPoint3*, const RE::NiPoint3*, float, bool, bool, bool);
        using RemovedMessage = void (*)(void*, RE::ExtraDataList*, std::int32_t);

        // Native velocity setters need the dynamic-initialized bit, and a body merely
        // lacking the static and keyframed bits may not have its motion ready yet.
        [[nodiscard]] constexpr bool roundBodyDynamic(const std::uint32_t flags, const std::uint32_t motionIndex) noexcept
        {
            return (flags & 7) == 2 && motionIndex != 0;
        }

        // Only the short wait for the round's 3D and physics. The engine owns the
        // reference, so a ticket running out or being reset never deletes it.
        struct Launch
        {
            std::uint32_t reference{ 0 };
            std::uint32_t ammo{ 0 };
            std::uint32_t cell{ 0 };
            RE::NiPoint3 velocity{};
            RE::NiPoint3 spin{};
            float age{ 0.0f };
            float launchAge{ 0.0f };
            bool launched{ false };
        };
        constexpr float kLaunchTimeoutSeconds = 1.5f;
        // After the launch, how long the body's motion is left to settle before the last check.
        constexpr float kAfterLaunchSeconds = 0.5f;
        std::array<Launch, 32> s_launches{};
        std::uint32_t s_launchWorld{ 0 };

        // Inside one eject call only, so native shots, NPCs, other threads and
        // unrelated debris all keep the game's own casing.
        struct Ejection
        {
            RE::TESObjectREFR* actor{ nullptr };
            // The round to make in place of the casing; none for a spent case.
            RE::TESAmmo* ammo{ nullptr };
            std::uint32_t reference{ 0 };
            bool intercepted{ false };
        };
        thread_local Ejection* t_ejection{ nullptr };
        // The one ammo form whose removal shows nothing on the HUD, inside one debit.
        thread_local const RE::TESBoundObject* t_quietDebit{ nullptr };

        Debris s_debris{ nullptr };
        RemovedMessage s_removedMessage{ nullptr };
        bool s_checked{ false };
        bool s_ready{ false };

        template <class T>
        class Scope
        {
        public:
            Scope(T& slot, T value) :
                _slot(slot),
                _previous(std::exchange(slot, value))
            {}
            ~Scope() { _slot = _previous; }
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;

        private:
            T& _slot;
            T _previous;
        };

        [[nodiscard]] std::uintptr_t moduleBase() noexcept
        {
            return REL::Module::get().base();
        }

        [[nodiscard]] bool finite(const RE::NiPoint3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        // The same reference Euler convention the game's spawns use.
        [[nodiscard]] RE::NiPoint3 referenceAngles(const RE::NiMatrix3& rotation) noexcept
        {
            const auto& r = rotation.entry;
            const double horizontal = std::hypot(double(r[0][0]), double(r[1][0]));
            const auto pitch = static_cast<float>(std::atan2(-double(r[2][0]), horizontal));
            if (horizontal > 1e-6) {
                return { static_cast<float>(std::atan2(double(r[2][1]), double(r[2][2]))),
                    pitch,
                    static_cast<float>(std::atan2(double(r[1][0]), double(r[0][0]))) };
            }
            return { 0.0f, pitch, static_cast<float>(std::atan2(-double(r[0][1]), double(r[1][1]))) };
        }

        [[nodiscard]] Launch* freeLaunch() noexcept
        {
            const auto found = std::find_if(s_launches.begin(), s_launches.end(), [](const Launch& entry) {
                return entry.reference == 0;
            });
            return found != s_launches.end() ? std::addressof(*found) : nullptr;
        }

        [[nodiscard]] std::uint32_t spawnRound(
            RE::TESObjectCELL* cell,
            const RE::NiPoint3& position,
            const RE::NiMatrix3& rotation,
            const RE::NiPoint3& velocity,
            const RE::NiPoint3& spin)
        {
            auto* handler = RE::TESDataHandler::GetSingleton();
            auto* launch = freeLaunch();
            if (!handler || !cell || !launch || !finite(position) || !finite(velocity) || !finite(spin)) {
                return 0;
            }
            RE::NEW_REFR_DATA data{};
            data.location = position;
            data.direction = referenceAngles(rotation);
            if (!finite(data.direction)) {
                return 0;
            }
            data.object = t_ejection->ammo;
            data.interior = cell->IsInterior() ? cell : nullptr;
            data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
            if (!data.interior && !data.world) {
                return 0;
            }
            // No count means exactly one, and the ammo's own 3D then picks its single
            // round model and collision. Grabbing and pickup are the ordinary ones.
            data.forcePersist = true;
            data.clearStillLoadingFlag = true;
            data.initializeScripts = false;
            const auto handle = handler->CreateReferenceAtLocation(data);
            const auto reference = handle.get();
            if (!reference) {
                return 0;
            }
            const auto id = reference->GetFormID();
            // An exterior ejection can cross the shooter's cell boundary.
            auto* actualCell = reference->GetParentCell();
            if (!actualCell) {
                reference->Disable();
                reference->SetWantsDelete(true);
                return 0;
            }
            *launch = Launch{
                .reference = id,
                .ammo = t_ejection->ammo->formID,
                .cell = actualCell->formID,
                .velocity = velocity,
                .spin = spin,
            };
            t_ejection->reference = id;
            PAPER_LOG_INFO(
                Reload,
                "CHAMBER live round ref={:08X} ammo={:08X} at=({:.1f},{:.1f},{:.1f})",
                id,
                t_ejection->ammo->formID,
                position.x,
                position.y,
                position.z);
            return id;
        }

        // The eject function's own search and its three outputs, in its order. It finds
        // the P-Casing connect point on any node's model. The name it compares is made by
        // the eject function on first use, so this runs only inside that call.
        struct CasingPoint
        {
            alignas(16) RE::NiMatrix3 rotation{};
            RE::NiPoint3 position{};
            std::uint8_t found{ 0 };
        };
        static_assert(sizeof(RE::NiMatrix3) == 0x30);

        [[nodiscard]] bool findVisibleCasingPoint(CasingPoint& point) noexcept
        {
            auto* weapon = f4vr::getFirstPersonWeaponNode();
            if (!weapon) {
                return false;
            }
            struct Search
            {
                std::uint8_t* found;
                RE::NiPoint3* position;
                RE::NiMatrix3* rotation;
            } search{ &point.found, &point.position, &point.rotation };
            using Find = std::uint32_t (*)(RE::NiAVObject*, Search*);
            (void)reinterpret_cast<Find>(moduleBase() + offsets::kFunc_FindCasingPoint)(weapon, &search);
            return point.found != 0 && finite(point.position);
        }

        // Rows are a node's axes in the world, so a world vector turns by multiplying
        // on the right: the turn from `from` to `to` is from's transpose times to.
        [[nodiscard]] RE::NiMatrix3 turnBetween(const RE::NiMatrix3& from, const RE::NiMatrix3& to) noexcept
        {
            RE::NiMatrix3 turn;
            for (unsigned row = 0; row < 3; ++row) {
                for (unsigned column = 0; column < 3; ++column) {
                    float sum = 0.0f;
                    for (unsigned k = 0; k < 3; ++k) {
                        sum += from.entry[k][row] * to.entry[k][column];
                    }
                    turn.entry[row][column] = sum;
                }
            }
            return turn;
        }

        [[nodiscard]] RE::NiPoint3 turned(const RE::NiPoint3& value, const RE::NiMatrix3& turn) noexcept
        {
            return { value.x * turn.entry[0][0] + value.y * turn.entry[1][0] + value.z * turn.entry[2][0],
                value.x * turn.entry[0][1] + value.y * turn.entry[1][1] + value.z * turn.entry[2][1],
                value.x * turn.entry[0][2] + value.y * turn.entry[1][2] + value.z * turn.entry[2][2] };
        }

        // TRACE: temporary, measures where the casing and the live round really go.
        RE::NiPointer<RE::NiAVObject> s_traceNode{};
        int s_traceFrames{ 0 };
        unsigned s_traceCount{ 0 };
        std::uint32_t s_traceRound{ 0 };
        int s_traceRoundFrames{ 0 };

        void traceStep()
        {
            if (s_traceNode) {
                if (s_traceFrames % 6 == 0) {
                    const auto* parent = s_traceNode->parent;
                    PAPER_LOG_INFO(
                        Reload,
                        "TRACE casing frame={} world=({:.1f},{:.1f},{:.1f}) local=({:.1f},{:.1f},{:.1f}) parent={} '{}' flags={:X}",
                        150 - s_traceFrames,
                        s_traceNode->world.translate.x,
                        s_traceNode->world.translate.y,
                        s_traceNode->world.translate.z,
                        s_traceNode->local.translate.x,
                        s_traceNode->local.translate.y,
                        s_traceNode->local.translate.z,
                        static_cast<const void*>(parent),
                        parent && parent->name.c_str() ? parent->name.c_str() : "",
                        *reinterpret_cast<const std::uint64_t*>(reinterpret_cast<const std::byte*>(s_traceNode.get()) + 0x108));
                }
                if (--s_traceFrames <= 0) {
                    s_traceNode.reset();
                }
            }
            if (s_traceRound) {
                if (s_traceRoundFrames % 6 == 0) {
                    auto* reference = RE::TESForm::GetFormByID<RE::TESObjectREFR>(s_traceRound);
                    auto* root = reference ? reference->Get3D() : nullptr;
                    PAPER_LOG_INFO(
                        Reload,
                        "TRACE round frame={} ref={:08X} has3D={} at=({:.1f},{:.1f},{:.1f}) world=({:.1f},{:.1f},{:.1f})",
                        150 - s_traceRoundFrames,
                        s_traceRound,
                        root != nullptr,
                        reference ? reference->data.location.x : 0.0f,
                        reference ? reference->data.location.y : 0.0f,
                        reference ? reference->data.location.z : 0.0f,
                        root ? root->world.translate.x : 0.0f,
                        root ? root->world.translate.y : 0.0f,
                        root ? root->world.translate.z : 0.0f);
                }
                if (--s_traceRoundFrames <= 0) {
                    s_traceRound = 0;
                }
            }
        }

        // The eject function's one debris construction. Its storage comes from the game
        // heap and no constructor has run yet, and it accepts a null result.
        void* constructDebris(
            void* storage,
            RE::TESObjectCELL* cell,
            float lifetime,
            const char* model,
            RE::TESObjectREFR* actor,
            const RE::NiPoint3* position,
            const RE::NiMatrix3* rotation,
            const RE::NiPoint3* velocity,
            const RE::NiPoint3* spin,
            float scale,
            bool reserved,
            bool shell,
            bool firstPerson) noexcept
        {
            if (!t_ejection || t_ejection->actor != actor) {
                return s_debris(storage, cell, lifetime, model, actor, position, rotation, velocity, spin, scale, reserved, shell, firstPerson);
            }
            if (std::exchange(t_ejection->intercepted, true) || !position || !rotation || !velocity || !spin) {
                RE::free(storage);
                return nullptr;
            }
            CasingPoint visible{};
            RE::NiPoint3 placedVelocity{};
            RE::NiPoint3 placedSpin{};
            try {
                if (findVisibleCasingPoint(visible)) {
                    const auto turn = turnBetween(*rotation, visible.rotation);
                    placedVelocity = turned(*velocity, turn);
                    placedSpin = turned(*spin, turn);
                    PAPER_LOG_INFO(
                        Reload,
                        "CHAMBER casing point visible=({:.1f},{:.1f},{:.1f}) game=({:.1f},{:.1f},{:.1f}) apart={:.1f}gu firstPerson={}",
                        visible.position.x,
                        visible.position.y,
                        visible.position.z,
                        position->x,
                        position->y,
                        position->z,
                        visible.position.GetDistance(*position),
                        firstPerson);
                    position = &visible.position;
                    rotation = &visible.rotation;
                    velocity = &placedVelocity;
                    spin = &placedSpin;
                } else {
                    PAPER_LOG_WARN(Reload, "CHAMBER the visible weapon has no casing point; the game's own is used");
                }
                if (t_ejection->ammo) {
                    RE::free(storage);
                    t_ejection->reference = spawnRound(cell, *position, *rotation, *velocity, *spin);
                    s_traceRound = t_ejection->reference;
                    s_traceRoundFrames = 150;
                    return nullptr;
                }
            } catch (...) {
                OutputDebugStringA("PAPER: placing an ejected round failed\n");
                if (t_ejection->ammo) {
                    return nullptr;
                }
            }
            // TRACE: every second spent case is built as ordinary world debris.
            const bool flag = (s_traceCount++ % 2) == 0 ? firstPerson : false;
            auto* debris = s_debris(storage, cell, lifetime, model, actor, position, rotation, velocity, spin, scale, reserved, shell, flag);
            if (debris) {
                s_traceNode.reset(*reinterpret_cast<RE::NiAVObject**>(static_cast<std::byte*>(debris) + 0x30));
                s_traceFrames = 150;
                PAPER_LOG_INFO(Reload, "TRACE casing built firstPerson={} model={} velocity=({:.1f},{:.1f},{:.1f})", flag, model ? model : "", velocity->x, velocity->y, velocity->z);
            }
            return debris;
        }

        // The collector's first field is the base form, which the native aggregation
        // reads too. Only the HUD record is dropped; the inventory change stands.
        void removedMessage(void* collector, RE::ExtraDataList* extra, std::int32_t amount) noexcept
        {
            if (t_quietDebit && collector && *static_cast<RE::TESBoundObject**>(collector) == t_quietDebit) {
                return;
            }
            s_removedMessage(collector, extra, amount);
        }

        [[nodiscard]] std::int32_t inventoryCount(RE::Actor* player, RE::TESBoundObject* ammo) noexcept
        {
            using Count = std::int32_t (*)(RE::TESObjectREFR*, RE::TESBoundObject*);
            return reinterpret_cast<Count>(moduleBase() + offsets::kFunc_InventoryCount)(player, ammo);
        }

        void removeOneRound(RE::Actor* player, RE::TESAmmo* ammo) noexcept
        {
            const auto before = inventoryCount(player, ammo);
            RE::TESObjectREFR::RemoveItemData remove(ammo, 1);
            {
                Scope<const RE::TESBoundObject*> quiet(t_quietDebit, ammo);
                player->RemoveItem(remove);
            }
            const auto after = inventoryCount(player, ammo);
            if (before - after != 1) {
                PAPER_LOG_ERROR(Reload, "CHAMBER live round debit mismatch ammo={:08X} before={} after={}", ammo->formID, before, after);
            }
        }

        // Skipped when this thread already holds the world for the physics step.
        class WorldWriteLock
        {
        public:
            explicit WorldWriteLock(void* world) noexcept
            {
                const auto base = moduleBase();
                const auto* tls = reinterpret_cast<const std::uintptr_t*>(__readgsqword(0x58));
                const auto slot = tls ? tls[*reinterpret_cast<const std::uint32_t*>(base + offsets::kGlobal_ThreadStateTlsIndex)] : 0;
                if (slot && *reinterpret_cast<const std::uint8_t*>(slot + 0x1529)) {
                    return;
                }
                reinterpret_cast<void (*)(void*)>(base + offsets::kFunc_WorldLockWrite)(world);
                _world = world;
            }
            ~WorldWriteLock()
            {
                if (_world) {
                    reinterpret_cast<void (*)(void*)>(moduleBase() + offsets::kFunc_WorldUnlockWrite)(_world);
                }
            }
            WorldWriteLock(const WorldWriteLock&) = delete;
            WorldWriteLock& operator=(const WorldWriteLock&) = delete;

        private:
            void* _world{ nullptr };
        };

        // hknpWorld's motion array, the layout ROCK's Havok runtime qualifies.
        [[nodiscard]] std::byte* allocatedMotion(void* world, const std::uint32_t index) noexcept
        {
            const auto* bytes = static_cast<const std::byte*>(world);
            auto* data = *reinterpret_cast<std::byte* const*>(bytes + 0xE0);
            const auto count = *reinterpret_cast<const std::uint32_t*>(bytes + 0xE8);
            const auto capacity = *reinterpret_cast<const std::uint32_t*>(bytes + 0xEC) & 0x3fffffff;
            if (!data || reinterpret_cast<std::uintptr_t>(data) % 16 || !index || index >= count || count > capacity || capacity > 0x100000) {
                return nullptr;
            }
            auto* result = data + std::size_t(index) * 0x80;
            return *reinterpret_cast<const std::uint8_t*>(result + 0x3F) ? result : nullptr;
        }

        // True once the motion is applied. An ammo reference can have several collision
        // owners, and a dynamic root does not prove its visible single-round body is
        // dynamic, so each body is checked. Custom dynamic profiles stay; the static,
        // keyframed and frozen presets, which would ignore velocity and gravity, are
        // replaced on this new reference only.
        [[nodiscard]] bool applyRoundPhysics(RE::TESObjectREFR* reference, Launch& entry) noexcept
        {
            auto* root = reference->Get3D();
            auto* cell = reference->GetParentCell();
            if (!root || !cell) {
                return false;
            }
            const auto base = moduleBase();
            const auto* bhk = reinterpret_cast<void* (*)(RE::TESObjectCELL*)>(base + offsets::kFunc_Cell_GetBhkWorld)(cell);
            auto* world = bhk ? *reinterpret_cast<void* const*>(static_cast<const std::byte*>(bhk) + 0x60) : nullptr;
            if (!world) {
                return false;
            }
            WorldWriteLock lock(world);

            struct Body
            {
                RE::NiAVObject* node{ nullptr };
                std::uint32_t id{ 0 };
                std::uint32_t motion{ 0 };
                std::uint16_t properties{ 0 };
            };
            std::array<Body, 32> bodies{};
            std::size_t bodyCount = 0;
            std::array<RE::NiAVObject*, 128> nodes{};
            nodes[0] = root;
            std::size_t count = 1;
            bool dynamic = true;
            for (std::size_t i = 0; i < count; ++i) {
                auto* node = nodes[i];
                if (node != root && node->GetAppCulled()) {
                    continue;
                }
                if (auto* collision = node->collisionObject.get()) {
                    const auto table = *reinterpret_cast<std::uintptr_t* const*>(collision);
                    auto* object = reinterpret_cast<void* (*)(void*)>(table[0xD8 / 8])(collision);
                    if (!object) {
                        return false;
                    }
                    auto* system = *reinterpret_cast<void**>(static_cast<std::byte*>(object) + 0x20);
                    if (!system || reinterpret_cast<void* (*)(void*)>(base + offsets::kFunc_System_GetWorld)(system) != world) {
                        return false;
                    }
                    std::uint32_t id = 0x7fffffff;
                    reinterpret_cast<std::uint32_t* (*)(void*, std::uint32_t*)>(base + offsets::kFunc_CollisionObject_GetBodyId)(object, &id);
                    const auto* raw = static_cast<const std::byte*>(
                        reinterpret_cast<void* (*)(void*)>(base + offsets::kFunc_CollisionObject_GetBody)(object));
                    if (!raw || id == 0x7fffffff) {
                        return false;
                    }
                    const auto first = bodies.begin();
                    const auto last = first + static_cast<std::ptrdiff_t>(bodyCount);
                    if (std::none_of(first, last, [id](const Body& body) { return body.id == id; })) {
                        if (bodyCount == bodies.size()) {
                            return false;
                        }
                        const auto index = *reinterpret_cast<const std::uint32_t*>(raw + 0x68);
                        auto* motion = allocatedMotion(world, index);
                        dynamic = dynamic && roundBodyDynamic(*reinterpret_cast<const std::uint32_t*>(raw + 0x40), index) && motion;
                        bodies[bodyCount++] = Body{ node, id, index, motion ? *reinterpret_cast<std::uint16_t*>(motion + 0x38) : std::uint16_t(0xffff) };
                    }
                }
                if (auto* parent = node->IsNode()) {
                    for (const auto& child : parent->children) {
                        if (child) {
                            if (count == nodes.size() || child->parent != parent) {
                                return false;
                            }
                            nodes[count++] = child.get();
                        }
                    }
                }
            }
            if (!bodyCount) {
                return false;
            }
            if (!dynamic) {
                using SetMotionType = std::uint8_t (*)(RE::NiAVObject*, std::uint32_t, bool, bool, bool);
                if (reinterpret_cast<SetMotionType>(base + offsets::kFunc_SetMotionType)(root, 1, true, true, true)) {
                    reference->AddChange(4);
                }
                return false;
            }
            const auto* library = *reinterpret_cast<void* const*>(static_cast<const std::byte*>(world) + 0x5D0);
            if (!library || *reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(library) + 0x30) <= 1) {
                return false;
            }
            for (std::size_t i = 0; i < bodyCount; ++i) {
                auto& body = bodies[i];
                if (body.properties == 0 || body.properties == 2 || body.properties == 3) {
                    using SetProfile = void (*)(void*, std::uint32_t, std::uint16_t);
                    reinterpret_cast<SetProfile>(base + offsets::kFunc_World_SetMotionProperties)(world, body.id, 1);
                    auto* current = allocatedMotion(world, body.motion);
                    if (!current || *reinterpret_cast<std::uint16_t*>(current + 0x38) != 1) {
                        return false;
                    }
                    body.properties = 1;
                    reference->AddChange(4);
                }
            }
            if (entry.launched) {
                return true;
            }
            using SetVelocity = void (*)(RE::NiAVObject*, const RE::NiPoint3*, bool);
            reinterpret_cast<SetVelocity>(base + offsets::kFunc_SetLinearVelocity)(root, &entry.velocity, true);
            reinterpret_cast<SetVelocity>(base + offsets::kFunc_SetAngularVelocity)(root, &entry.spin, true);
            return true;
        }

        [[nodiscard]] bool callTargets(const std::uintptr_t site, const std::uintptr_t target) noexcept
        {
            const auto base = moduleBase();
            const auto* code = reinterpret_cast<const std::uint8_t*>(base + site);
            std::int32_t relative = 0;
            std::memcpy(&relative, code + 1, sizeof(relative));
            const bool match = code[0] == 0xE8 && base + site + 5 + relative == base + target;
            if (!match) {
                PAPER_LOG_ERROR(Reload, "CHAMBER native call at RVA {:X} does not reach RVA {:X}", site, target);
            }
            return match;
        }

        template <std::size_t N>
        [[nodiscard]] bool bytesMatch(const std::uintptr_t rva, const std::array<std::uint8_t, N>& expected) noexcept
        {
            const bool match = std::memcmp(reinterpret_cast<const void*>(moduleBase() + rva), expected.data(), N) == 0;
            if (!match) {
                PAPER_LOG_ERROR(Reload, "CHAMBER native code differs at RVA {:X}", rva);
            }
            return match;
        }

        [[nodiscard]] bool nativesMatch() noexcept
        {
            using std::to_array;
            // All of them, so a log names every difference at once. The memory manager's
            // own functions are not among them: a plugin may replace them, and the debris
            // storage is allocated and freed through whichever is in place.
            const std::array checks{
                callTargets(offsets::kCallsite_EjectConstructDebris, offsets::kFunc_ConstructDebris),
                callTargets(offsets::kCallsite_EjectAllocateDebris, offsets::kFunc_AllocateDebrisStorage),
                callTargets(offsets::kCallsite_EjectFindCasingPoint, offsets::kFunc_FindCasingPoint),
                bytesMatch(offsets::kFunc_FindCasingPoint, to_array<std::uint8_t>({ 0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB, 0x48, 0x8B, 0xFA, 0x48, 0x85, 0xC9 })),
                bytesMatch(offsets::kFunc_Actor_GetEquippedAmmo, to_array<std::uint8_t>({ 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x89, 0x00, 0x03, 0x00, 0x00 })),
                bytesMatch(offsets::kSignature_EjectDebrisResult, to_array<std::uint8_t>({ 0x48, 0x85, 0xC0, 0x74, 0x13 })),
                bytesMatch(offsets::kSignature_CreateReference, to_array<std::uint8_t>({ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41 })),
                bytesMatch(offsets::kSignature_CreateReferenceForcePersist, to_array<std::uint8_t>({ 0x45, 0x38, 0x7E, 0x6A })),
                bytesMatch(offsets::kFunc_Cell_GetBhkWorld, to_array<std::uint8_t>({ 0xF6, 0x41, 0x40, 0x01, 0x74, 0x09, 0x48, 0x8B, 0x49, 0x48 })),
                bytesMatch(offsets::kFunc_SetLinearVelocity, to_array<std::uint8_t>({ 0x48, 0x83, 0xEC, 0x48, 0xF3, 0x0F, 0x10, 0x15, 0xDC, 0xF8, 0x78, 0x04 })),
                bytesMatch(offsets::kFunc_SetAngularVelocity, to_array<std::uint8_t>({ 0x48, 0x83, 0xEC, 0x48, 0xF3, 0x0F, 0x10, 0x15, 0x3C, 0xF8, 0x78, 0x04 })),
                bytesMatch(offsets::kFunc_WorldLockWrite, to_array<std::uint8_t>({ 0x48, 0x81, 0xC1, 0xD8, 0x06, 0x00, 0x00, 0xE9, 0x74, 0xD3, 0xD9, 0xFF })),
                bytesMatch(offsets::kFunc_WorldUnlockWrite, to_array<std::uint8_t>({ 0x48, 0x81, 0xC1, 0xD8, 0x06, 0x00, 0x00, 0xE9, 0xA4, 0xD5, 0xD9, 0xFF })),
                bytesMatch(offsets::kFunc_System_GetWorld, to_array<std::uint8_t>({ 0x48, 0x8B, 0x41, 0x18, 0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x8B, 0x40, 0x18, 0xC3 })),
                bytesMatch(offsets::kFunc_CollisionObject_GetBodyId, to_array<std::uint8_t>({ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x20 })),
                bytesMatch(offsets::kFunc_CollisionObject_GetBody, to_array<std::uint8_t>({ 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20 })),
                bytesMatch(offsets::kFunc_World_SetMotionProperties, to_array<std::uint8_t>({ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x66, 0x44, 0x89, 0x44, 0x24, 0x18, 0x89, 0x54, 0x24, 0x10 })),
                bytesMatch(offsets::kFunc_SetMotionType, to_array<std::uint8_t>({ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60 })),
                bytesMatch(offsets::kSignature_CollisionObjectGetSystemCall, to_array<std::uint8_t>({ 0x41, 0xFF, 0x90, 0xD8, 0x00, 0x00, 0x00 })),
                bytesMatch(offsets::kFunc_InventoryCount, to_array<std::uint8_t>({ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA })),
                bytesMatch(offsets::kCallsite_RemoveItemHudMessage - 13, to_array<std::uint8_t>({ 0x44, 0x8B, 0x44, 0x24, 0x64, 0x49, 0x8B, 0x14, 0x24, 0x48, 0x8D, 0x4D, 0xF0, 0xE8, 0xA7, 0x95, 0x71, 0x00 })),
            };
            return std::all_of(checks.begin(), checks.end(), [](const bool match) { return match; });
        }
    }

    bool install() noexcept
    {
        if (s_checked) {
            return s_ready;
        }
        s_checked = true;
        if (!nativesMatch()) {
            PAPER_LOG_WARN(Reload, "CHAMBER the native calls differ: what the bolt throws out keeps the game's place, and a live round goes back to the reserve");
            return false;
        }
        const auto base = moduleBase();
        try {
            auto& trampoline = F4SE::GetTrampoline();
            s_debris = reinterpret_cast<Debris>(
                trampoline.write_call<5>(base + offsets::kCallsite_EjectConstructDebris, &constructDebris));
            s_removedMessage = reinterpret_cast<RemovedMessage>(
                trampoline.write_call<5>(base + offsets::kCallsite_RemoveItemHudMessage, &removedMessage));
            s_ready = true;
        } catch (...) {
            OutputDebugStringA("PAPER: hooking the live round failed\n");
        }
        return s_ready;
    }

    bool eject(RE::Actor* player, const void* instance, const Kind kind) noexcept
    {
        if (!player || !instance) {
            return false;
        }
        // The entry, so ROCK's casing grace still runs.
        const auto ejectFunction = reinterpret_cast<Eject>(moduleBase() + offsets::kFunc_EjectShellCasing);
        if (kind == Kind::SpentCase) {
            Ejection context{ .actor = player };
            Scope<Ejection*> scope(t_ejection, &context);
            ejectFunction(player, instance, 0);
            return true;
        }
        if (!s_ready) {
            return false;
        }
        using GetAmmo = RE::TESAmmo* (*)(RE::Actor*, std::uint32_t);
        auto* ammo = reinterpret_cast<GetAmmo>(moduleBase() + offsets::kFunc_Actor_GetEquippedAmmo)(player, 0);
        if (!ammo || ammo->formType != RE::ENUM_FORM_ID::kAMMO || ammo->model.empty()) {
            return false;
        }
        Ejection context{ .actor = player, .ammo = ammo };
        {
            Scope<Ejection*> scope(t_ejection, &context);
            ejectFunction(player, instance, 0);
        }
        if (!context.reference) {
            PAPER_LOG_WARN(Reload, "CHAMBER live round not created ammo={:08X}: it went back to the reserve", ammo->formID);
            return false;
        }
        removeOneRound(player, ammo);
        return true;
    }

    void update(RockApiClient& rock, const rock::api::animation::AnimationPhaseContext& context) noexcept
    {
        using Flag = rock::api::animation::AnimationPhaseContextFlag;
        const auto has = [&context](const Flag flag) {
            return (context.flags & static_cast<std::uint32_t>(flag)) != 0;
        };
        try {
            traceStep();
        } catch (...) {
        }
        if (context.worldGeneration != s_launchWorld) {
            s_launches = {};
            s_launchWorld = context.worldGeneration;
        }
        const bool anyWaiting = std::any_of(s_launches.begin(), s_launches.end(), [](const Launch& entry) {
            return entry.reference != 0;
        });
        if (!anyWaiting || !has(Flag::ProviderReady) || !has(Flag::SkeletonReady) || has(Flag::MenuBlocking) ||
            has(Flag::ConfigBlocking) || !has(Flag::VisualWritesAllowed)) {
            return;
        }
        // A hand reaching for the round wins over its delayed launch.
        std::array<std::uint32_t, 2> targeted{};
        bool handsReady = true;
        for (const auto hand : { rock::api::Hand::Right, rock::api::Hand::Left }) {
            rock::api::grab::HandInteractionState state{};
            if (!rock.queryHandInteractionState(hand, state) || state.worldGeneration != context.worldGeneration ||
                state.skeletonGeneration != context.skeletonGeneration || state.providerGeneration != context.providerGeneration ||
                state.frameIndex > context.frameIndex || context.frameIndex - state.frameIndex > 1) {
                handsReady = false;
                break;
            }
            targeted[hand == rock::api::Hand::Left ? 1 : 0] = state.targetFormId;
        }
        const float dt = std::isfinite(context.deltaSeconds) ? std::clamp(context.deltaSeconds, 0.0f, 0.1f) : 0.0f;
        for (auto& entry : s_launches) {
            if (!entry.reference) {
                continue;
            }
            entry.age += dt;
            const bool expired = entry.age >= kLaunchTimeoutSeconds;
            auto* reference = RE::TESForm::GetFormByID<RE::TESObjectREFR>(entry.reference);
            auto* object = reference ? reference->GetObjectReference() : nullptr;
            auto* cell = reference ? reference->GetParentCell() : nullptr;
            bool done = !reference || !object || object->formID != entry.ammo || reference->IsDeleted() || reference->IsDisabled() ||
                !cell || cell->formID != entry.cell || targeted[0] == entry.reference || targeted[1] == entry.reference;
            if (!done && entry.launched) {
                done = entry.age - entry.launchAge >= kAfterLaunchSeconds && applyRoundPhysics(reference, entry);
            } else if (!done && handsReady && applyRoundPhysics(reference, entry)) {
                entry.launched = true;
                entry.launchAge = entry.age;
            }
            if (expired && !entry.launched && !done) {
                PAPER_LOG_WARN(Reload, "CHAMBER live round ref={:08X} did not launch; it stays where it is", entry.reference);
            }
            if (done || expired) {
                entry = {};
            }
        }
    }

    void reset() noexcept
    {
        s_launches = {};
    }
}
