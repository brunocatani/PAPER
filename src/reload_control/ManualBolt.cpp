#include "reload_control/ManualBolt.h"

#include "PaperLog.h"
#include "api/ApiTransform.h"
#include "api/RockApiClient.h"
#include "native/NativeOffsets.h"
#include "reload_control/BoltHold.h"
#include "reload_control/ManualReloadOnly.h"
#include "support/TransformMath.h"

#include <F4SE/API.h>
#include <F4SE/Interfaces.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numbers>
#include <span>
#include <vector>

namespace paper::manual_bolt
{
    namespace
    {
        namespace policy = bolt_hold_policy;
        namespace parts = rock::api::weaponparts;
        using WeaponStateFlag = rock::api::weapon::WeaponStateFlag;
        using Marker = rock::api::weapon::ChamberMarker;
        using rock::api::Hand;

        enum class Chamber : std::uint32_t
        {
            Empty = 0,
            Live = 1,
            Spent = 2,
        };

        constexpr std::size_t kBoneCount = policy::kActionBoneNames.size();
        constexpr std::uint32_t kGroupId = 1;
        constexpr std::uint32_t kDriveLeaseFrames = 2;
        // The bolt counts as home or back within this much of its stroke.
        constexpr float kClosedAt = 0.03f;
        constexpr float kBackAt = 0.97f;
        // How much of the pull lifts a bolt that turns, before it slides.
        constexpr float kLiftFraction = 0.3f;
        constexpr float kMinimumLiftDegrees = 10.0f;
        // Around the bolt's collider, for the free hand's palm or fingers.
        constexpr float kGrabRadiusGameUnits = 8.0f;
        // A round leaves the gun only while the trigger is held or just was, so
        // the native count falling for any other reason, such as the weapon
        // going away, is never taken for a shot.
        constexpr std::uint32_t kShotTriggerFrames = 30;
        // The game can fill the magazine again for some frames after an equip.
        constexpr std::uint32_t kRestoreFrames = 90;
        constexpr std::size_t kMaxSavedItems = 1024;

        // Fallout4.esm, the hunting rifle's own bolt: WPNRifleHuntingAReloadBoltOpen, -BoltClose.
        constexpr std::uint32_t kSoundBoltOpen = 0x5A29B;
        constexpr std::uint32_t kSoundBoltClose = 0x5A29D;

        // Version 1 layout; a change needs a new version.
        struct SavedItem
        {
            std::uint32_t weapon{ 0 };
            std::uint32_t owner{ 0 };
            std::uint32_t unique{ 0 };
            std::uint32_t loaded{ 0 };
            std::uint32_t chamber{ 0 };
        };
        static_assert(sizeof(SavedItem) == 20);
        constexpr std::uint32_t kSerializationId = 0x50415052;  // 'PAPR'
        constexpr std::uint32_t kRecordType = 0x424F4C54;  // 'BOLT'
        constexpr std::uint32_t kRecordVersion = 1;

        std::mutex s_savedMutex;
        std::vector<SavedItem> s_saved;
        // A load or revert replaced the table: the equipped weapon binds again.
        std::atomic<std::uint32_t> s_savedGeneration{ 0 };
        std::atomic<bool> s_ownsCycle{ false };

        struct Gun
        {
            std::uint64_t generation{ 0 };
            std::uint32_t form{ 0 };
            std::uint32_t savedGeneration{ 0 };
            bool tracked{ false };
            SavedItem item{};
            Chamber chamber{ Chamber::Empty };
            std::uint32_t lastLoaded{ 0 };
            bool droppedWithoutShot{ false };
            bool stored{ false };
            std::uint32_t restoreTarget{ 0 };
            std::uint32_t restoreFrames{ 0 };
            bool restoreFirstFrame{ false };

            bool handCycle{ false };
            bool handCycleRefused{ false };
            bolt_hold::ManualAction action{};
            RE::NiPoint3 axis{};
            float liftFraction{ 0.0f };

            float pull{ 0.0f };
            bool atBack{ false };
            bool driving{ false };
            bool gripHeld{ false };
            std::uint64_t gripSequence{ 0 };
            RE::NiPoint3 gripStart{};
            float gripStartPull{ 0.0f };

            bool triggerHeld{ false };
            std::uint32_t triggerFrames{ 0 };
            bool fireBlocked{ false };
            bool reloading{ false };
            bool reloadKeepsRound{ false };
            std::uint32_t loadedAtReload{ 0 };
            bool driveRefusalLogged{ false };
            bool blockRefusalLogged{ false };
            bool markerRefusalLogged{ false };
        };

        Gun s_gun{};
        std::vector<parts::EvidenceDetail> s_evidence;

        // Each checked once, byte for byte, before the first call. A weapon without
        // what it needs loses that one effect and keeps working.
        struct Natives
        {
            bool checked{ false };
            bool setLoaded{ false };
            bool identity{ false };
            bool casing{ false };
        };
        Natives s_natives{};

        template <std::size_t N>
        [[nodiscard]] bool bytesMatch(const std::uintptr_t rva, const std::array<std::uint8_t, N>& expected)
        {
            const bool match =
                std::memcmp(reinterpret_cast<const void*>(REL::Module::get().base() + rva), expected.data(), N) == 0;
            if (!match) {
                PAPER_LOG_ERROR(Reload, "MANUAL_BOLT native signature differs at RVA {:X}", rva);
            }
            return match;
        }

        [[nodiscard]] bool instanceFetchReady()
        {
            return bytesMatch(
                offsets::kFunc_Actor_GetEquippedInstance,
                std::to_array<std::uint8_t>({ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0x02, 0xEB, 0x00, 0x00 }));
        }

        // The eject function's entry belongs to ROCK's casing hook, so its one
        // native caller is what identifies it.
        [[nodiscard]] bool casingCallReady()
        {
            const auto base = REL::Module::get().base();
            const auto site = base + offsets::kCallsite_EjectShellCasingHandler;
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(site);
            constexpr std::array<std::uint8_t, 11> kSetup{ 0x48, 0x8D, 0x54, 0x24, 0x28, 0x44, 0x8B, 0xC3, 0x48, 0x8B, 0xCD };
            std::int32_t relative = 0;
            std::memcpy(&relative, bytes + 1, sizeof(relative));
            const bool match = bytes[0] == 0xE8 && site + 5 + relative == base + offsets::kFunc_EjectShellCasing &&
                std::memcmp(bytes - kSetup.size(), kSetup.data(), kSetup.size()) == 0;
            if (!match) {
                PAPER_LOG_WARN(Reload, "MANUAL_BOLT no casing from the bolt: the native eject call differs or another plugin routed it");
            }
            return match;
        }

        void checkNatives()
        {
            if (s_natives.checked) {
                return;
            }
            s_natives.checked = true;
            s_natives.setLoaded = bytesMatch(
                offsets::kFunc_AIProcess_SetLoadedRounds,
                std::to_array<std::uint8_t>({ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x41, 0x8B, 0xD8, 0x4C, 0x8D, 0x44, 0x24, 0x20 }));
            s_natives.identity =
                bytesMatch(
                    offsets::kFunc_ExtraDataList_ReadUniqueId,
                    std::to_array<std::uint8_t>({ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA })) &&
                bytesMatch(
                    offsets::kFunc_ExtraDataList_WriteUniqueId,
                    std::to_array<std::uint8_t>({ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x41, 0x56, 0x41, 0x57 })) &&
                bytesMatch(
                    offsets::kFunc_Reference_AllocateUniqueId,
                    std::to_array<std::uint8_t>({ 0x4C, 0x8B, 0xDC, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x33, 0xF6 }));
            s_natives.casing = instanceFetchReady() && casingCallReady();
        }

        [[nodiscard]] RE::EquippedItem* equippedItem(const std::uint32_t form)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* process = player ? player->currentProcess : nullptr;
            auto* data = process ? process->middleHigh : nullptr;
            if (!data || data->equippedItems.empty() || data->equippedItems.size() > 8) {
                return nullptr;
            }
            auto& item = data->equippedItems[0];
            if (!item.item.object || item.item.object->formID != form ||
                item.item.object->formType != RE::ENUM_FORM_ID::kWEAP || !item.data) {
                return nullptr;
            }
            return &item;
        }

        // Sets the native count, which the next frame's weapon state reports.
        [[nodiscard]] bool setLoadedRounds(const std::uint32_t form, const std::uint32_t rounds)
        {
            if (!s_natives.setLoaded || !equippedItem(form)) {
                return false;
            }
            using Set = void (*)(RE::AIProcess*, std::uint32_t, std::uint32_t);
            reinterpret_cast<Set>(REL::Module::get().base() + offsets::kFunc_AIProcess_SetLoadedRounds)(
                RE::PlayerCharacter::GetSingleton()->currentProcess,
                0,
                rounds);
            return true;
        }

        // The inventory item in slot 0, by weapon, owner and ExtraUniqueID, giving
        // it an ID from the engine's allocator if it has none. A stack of more than
        // one, or an equipped item that cannot be told from another, has no identity.
        [[nodiscard]] SavedItem equippedItemIdentity(const std::uint32_t form)
        {
            auto* item = equippedItem(form);
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!s_natives.identity || !item || !player->inventoryList ||
                player->inventoryList->data.size() > 65535) {
                return {};
            }
            RE::BSTSmartPointer<RE::ExtraDataList> extra;
            RE::BSTSmartPointer<RE::ExtraDataList> fallback;
            std::uint32_t matches = 0;
            std::uint32_t exactMatches = 0;
            std::uint32_t fallbackCount = 0;
            {
                const RE::BSAutoReadLock lock{ player->inventoryList->rwLock };
                for (const auto& inventory : player->inventoryList->data) {
                    if (inventory.object != item->item.object) {
                        continue;
                    }
                    std::uint32_t visited = 0;
                    for (auto* stack = inventory.stackData.get(); stack && visited++ < 65535; stack = stack->nextStack.get()) {
                        if (!stack->IsEquipped()) {
                            continue;
                        }
                        ++matches;
                        fallback = stack->extra;
                        fallbackCount = stack->count;
                        if (stack->extra && item->item.instanceData) {
                            const auto* instance = stack->extra->GetByType<RE::ExtraInstanceData>();
                            if (instance && instance->data.get() == item->item.instanceData.get() && stack->count == 1) {
                                extra = stack->extra;
                                ++exactMatches;
                            }
                        }
                    }
                }
            }
            // Equipped data can be a native copy, so one equipped stack is enough.
            if (!extra && matches == 1 && fallbackCount == 1) {
                extra = fallback;
            }
            if (exactMatches > 1 || !extra) {
                PAPER_LOG_WARN(
                    Reload,
                    "MANUAL_BOLT cannot tell the equipped item apart weapon={:08X} matches={} exact={} count={}; its rounds are not kept",
                    form,
                    matches,
                    exactMatches,
                    fallbackCount);
                return {};
            }
            const auto base = REL::Module::get().base();
            using Read = void (*)(RE::ExtraDataList*, std::uint16_t*, std::uint32_t*);
            const auto read = reinterpret_cast<Read>(base + offsets::kFunc_ExtraDataList_ReadUniqueId);
            std::uint16_t unique = 0;
            std::uint32_t owner = 0;
            read(extra.get(), &unique, &owner);
            if (!unique) {
                using Allocate = std::uint16_t (*)(RE::TESObjectREFR*);
                using Write = void (*)(RE::ExtraDataList*, std::uint16_t, std::uint32_t);
                unique = reinterpret_cast<Allocate>(base + offsets::kFunc_Reference_AllocateUniqueId)(player);
                if (!unique) {
                    return {};
                }
                reinterpret_cast<Write>(base + offsets::kFunc_ExtraDataList_WriteUniqueId)(
                    extra.get(),
                    unique,
                    player->GetFormID());
                player->AddChange(0x20);
                read(extra.get(), &unique, &owner);
            }
            return SavedItem{ .weapon = form, .owner = owner, .unique = unique };
        }

        void ejectCasing(const std::uint32_t form)
        {
            if (!s_natives.casing || !equippedItem(form)) {
                return;
            }
            struct Instance
            {
                RE::TESForm* object{ nullptr };
                RE::TBO_InstanceData* data{ nullptr };
                ~Instance()
                {
                    if (data && data->DecRef() == 0) {
                        delete data;
                    }
                }
            } instance;
            static_assert(sizeof(Instance) == 0x10);
            const auto base = REL::Module::get().base();
            auto* player = RE::PlayerCharacter::GetSingleton();
            using Get = Instance* (*)(RE::Actor*, Instance*, std::uint32_t);
            reinterpret_cast<Get>(base + offsets::kFunc_Actor_GetEquippedInstance)(player, &instance, 0);
            if (!instance.object || instance.object->formID != form) {
                return;
            }
            using Eject = void (*)(RE::TESObjectREFR*, const void*, std::uint32_t);
            reinterpret_cast<Eject>(base + offsets::kFunc_EjectShellCasing)(player, &instance, 0);
        }

        [[nodiscard]] bool sameItem(const SavedItem& left, const SavedItem& right)
        {
            return left.weapon == right.weapon && left.owner == right.owner && left.unique == right.unique;
        }

        [[nodiscard]] bool findSaved(const SavedItem& key, SavedItem& out)
        {
            std::scoped_lock lock(s_savedMutex);
            const auto found = std::find_if(s_saved.begin(), s_saved.end(), [&](const SavedItem& entry) {
                return sameItem(entry, key);
            });
            if (found == s_saved.end()) {
                return false;
            }
            out = *found;
            return true;
        }

        void store(Gun& gun, const std::uint32_t loaded)
        {
            if (!gun.item.unique) {
                return;
            }
            const auto chamber = static_cast<std::uint32_t>(gun.chamber);
            if (gun.stored && gun.item.loaded == loaded && gun.item.chamber == chamber) {
                return;
            }
            gun.stored = true;
            gun.item.loaded = loaded;
            gun.item.chamber = chamber;
            std::scoped_lock lock(s_savedMutex);
            const auto found = std::find_if(s_saved.begin(), s_saved.end(), [&](const SavedItem& entry) {
                return sameItem(entry, gun.item);
            });
            if (found != s_saved.end()) {
                *found = gun.item;
                return;
            }
            if (s_saved.size() == kMaxSavedItems) {
                s_saved.erase(s_saved.begin());
            }
            s_saved.push_back(gun.item);
        }

        void identify(Gun& gun, const rock::api::weapon::WeaponState& state)
        {
            gun.tracked = true;
            gun.lastLoaded = state.loadedRounds;
            gun.chamber = state.loadedRounds > 0 ? Chamber::Live : Chamber::Empty;
            gun.item = equippedItemIdentity(gun.form);
            SavedItem saved{};
            const bool restored = gun.item.unique && findSaved(gun.item, saved);
            if (restored) {
                gun.stored = true;
                gun.item.loaded = saved.loaded;
                gun.item.chamber = saved.chamber;
                gun.chamber = saved.chamber <= static_cast<std::uint32_t>(Chamber::Spent) ?
                    static_cast<Chamber>(saved.chamber) :
                    Chamber::Empty;
                // Loaded rounds are part of the inventory count, so this creates none.
                gun.restoreTarget = (std::min)(saved.loaded, state.loadedRounds + state.reserveRounds);
                gun.restoreFrames = kRestoreFrames;
                gun.restoreFirstFrame = true;
            }
            PAPER_LOG_INFO(
                Reload,
                "MANUAL_BOLT tracking weapon={:08X}/{:016X} unique={} restored={} savedLoaded={} chamber={} loaded={} reserve={}",
                gun.form,
                gun.generation,
                gun.item.unique,
                restored,
                saved.loaded,
                static_cast<std::uint32_t>(gun.chamber),
                state.loadedRounds,
                state.reserveRounds);
        }

        [[nodiscard]] float turnDegrees(const RE::NiTransform& from, const RE::NiTransform& to)
        {
            double trace = 0.0;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    trace += static_cast<double>(from.rotate.entry[row][column]) * to.rotate.entry[row][column];
                }
            }
            return static_cast<float>(std::acos(std::clamp((trace - 1.0) * 0.5, -1.0, 1.0)) * 180.0 / std::numbers::pi);
        }

        void refuseHandCycle(Gun& gun, const char* reason, const std::uint32_t status)
        {
            gun.handCycleRefused = true;
            PAPER_LOG_INFO(
                Reload,
                "MANUAL_BOLT weapon={:08X} keeps its native cycle: {} status={}",
                gun.form,
                reason,
                status);
        }

        // Once the clips are read: the part the free hand holds, where it reaches
        // it, and the line the hand moves along.
        void prepareHandCycle(RockApiClient& rock, Gun& gun, const Hand firingHand)
        {
            if (gun.handCycle || gun.handCycleRefused || !bolt_hold::queryManualAction(gun.generation, gun.action)) {
                return;
            }
            const auto boltKey = gun.action.keys[policy::kBoltBone];
            parts::SourcePose bolt{};
            if (!rock.queryWeaponSourcePose(0, gun.generation, boltKey, bolt) || !bolt.valid) {
                return;
            }

            s_evidence.resize(parts::kMaxEvidenceDetails);
            std::uint32_t total = 0;
            const auto count = rock.copyWeaponEvidenceDetails(
                0,
                s_evidence.data(),
                static_cast<std::uint32_t>(s_evidence.size()),
                total);
            std::vector<parts::Target> targets;
            RE::NiPoint3 boundsMin{ 1e9f, 1e9f, 1e9f };
            RE::NiPoint3 boundsMax{ -1e9f, -1e9f, -1e9f };
            for (const auto& detail : std::span(s_evidence).first(count)) {
                const bool boltPart = detail.partKind == static_cast<std::uint32_t>(parts::PartKind::Bolt) ||
                    detail.actionRole == static_cast<std::uint32_t>(parts::ActionRole::Bolt);
                if (!boltPart || !detail.sourceKey || detail.weaponGenerationKey != gun.generation) {
                    continue;
                }
                boundsMin.x = (std::min)(boundsMin.x, detail.localBoundsGame.min.x);
                boundsMin.y = (std::min)(boundsMin.y, detail.localBoundsGame.min.y);
                boundsMin.z = (std::min)(boundsMin.z, detail.localBoundsGame.min.z);
                boundsMax.x = (std::max)(boundsMax.x, detail.localBoundsGame.max.x);
                boundsMax.y = (std::max)(boundsMax.y, detail.localBoundsGame.max.y);
                boundsMax.z = (std::max)(boundsMax.z, detail.localBoundsGame.max.z);
                const bool known = std::any_of(targets.begin(), targets.end(), [&](const parts::Target& target) {
                    return target.sourceKey == detail.sourceKey;
                });
                if (known || targets.size() == parts::kMaxTargets) {
                    continue;
                }
                auto& target = targets.emplace_back();
                // NonExclusive: the rest of the gun can still be held as usual.
                target.flags = static_cast<std::uint32_t>(parts::TargetFlag::MatchSourceKey) |
                    static_cast<std::uint32_t>(parts::TargetFlag::NonExclusive);
                target.grabMode = parts::GrabMode::AttachOnly;
                target.weaponGenerationKey = gun.generation;
                target.sourceKey = detail.sourceKey;
                target.groupId = kGroupId;
                target.priority = 100;
            }
            if (targets.empty()) {
                refuseHandCycle(gun, "no bolt collider to hold", 0);
                return;
            }

            const auto rootLocal = api_transform::toNi(bolt.weaponRootLocal);
            const auto parentInRoot = transform_math::composeTransforms(
                rootLocal,
                transform_math::invertTransform(api_transform::toNi(bolt.sourceParentLocal)));
            const auto closedInRoot =
                transform_math::localPointToWorld(parentInRoot, gun.action.closed[policy::kBoltBone].translate);
            const auto openInRoot =
                transform_math::localPointToWorld(parentInRoot, gun.action.open[policy::kBoltBone].translate);
            const RE::NiPoint3 stroke{ openInRoot.x - closedInRoot.x, openInRoot.y - closedInRoot.y, openInRoot.z - closedInRoot.z };
            const float strokeLength = std::sqrt(stroke.x * stroke.x + stroke.y * stroke.y + stroke.z * stroke.z);
            if (!std::isfinite(strokeLength) || strokeLength < policy::kMinimumOpenTravelGameUnits) {
                refuseHandCycle(gun, "the bolt has no stroke", 0);
                return;
            }
            gun.axis = { stroke.x / strokeLength, stroke.y / strokeLength, stroke.z / strokeLength };

            float largestTurn = 0.0f;
            for (std::size_t index = 0; index < kBoneCount; ++index) {
                if (gun.action.posed[index]) {
                    largestTurn = (std::max)(largestTurn, turnDegrees(gun.action.closed[index], gun.action.open[index]));
                }
            }
            gun.liftFraction = largestTurn >= kMinimumLiftDegrees ? kLiftFraction : 0.0f;

            if (const auto status = rock.setWeaponPartTargets(targets); status != rock::api::Status::Ok) {
                refuseHandCycle(gun, "ROCK refused the bolt as a target", static_cast<std::uint32_t>(status));
                return;
            }
            const RE::NiPoint3 centerInRoot{
                (boundsMin.x + boundsMax.x) * 0.5f,
                (boundsMin.y + boundsMax.y) * 0.5f,
                (boundsMin.z + boundsMax.z) * 0.5f,
            };
            const auto centerInBolt = transform_math::worldPointToLocal(rootLocal, centerInRoot);
            parts::GrabPoint point{};
            point.pointId = 1;
            point.groupId = kGroupId;
            point.weaponGenerationKey = gun.generation;
            point.sourceKey = boltKey;
            point.handMask = firingHand == Hand::Left ? 2u : 1u;
            point.radiusGame = kGrabRadiusGameUnits;
            point.pointInSource = { centerInBolt.x, centerInBolt.y, centerInBolt.z };
            if (const auto status = rock.setWeaponGrabPoints({ &point, 1 }); status != rock::api::Status::Ok) {
                rock.clearWeaponPartTargets();
                refuseHandCycle(gun, "ROCK refused the grab point", static_cast<std::uint32_t>(status));
                return;
            }
            gun.handCycle = true;
            PAPER_LOG_INFO(
                Reload,
                "MANUAL_BOLT hand cycle ready weapon={:08X} boltBodies={} stroke={:.2f}gu turn={:.1f}deg lift={:.2f}",
                gun.form,
                targets.size(),
                strokeLength,
                largestTurn,
                gun.liftFraction);
        }

        void endDrive(RockApiClient& rock, Gun& gun)
        {
            if (gun.driving) {
                rock.clearWeaponDrives(0);
                gun.driving = false;
            }
        }

        void endFireBlock(RockApiClient& rock, Gun& gun)
        {
            if (gun.fireBlocked) {
                rock.clearFireBlocked();
                gun.fireBlocked = false;
            }
        }

        void forget(RockApiClient& rock)
        {
            auto& gun = s_gun;
            s_ownsCycle.store(false, std::memory_order_release);
            endDrive(rock, gun);
            endFireBlock(rock, gun);
            if (gun.handCycle) {
                rock.clearWeaponGrabPoints();
                rock.clearWeaponPartTargets();
            }
            gun = {};
        }

        // The bolt's position as closed, lifted, back: it turns over the first part
        // of the pull, then slides. Parts that move with it follow the same split.
        [[nodiscard]] RE::NiTransform poseAt(const Gun& gun, const std::size_t bone)
        {
            const auto& closed = gun.action.closed[bone];
            const auto& open = gun.action.open[bone];
            const float lift = gun.liftFraction;
            const float turned = lift > 0.0f ? (std::min)(1.0f, gun.pull / lift) : gun.pull;
            const float slid = lift > 0.0f ? (std::max)(0.0f, (gun.pull - lift) / (1.0f - lift)) : gun.pull;
            auto pose = transform_math::interpolateTransforms(closed, open, turned);
            pose.translate = {
                closed.translate.x + (open.translate.x - closed.translate.x) * slid,
                closed.translate.y + (open.translate.y - closed.translate.y) * slid,
                closed.translate.z + (open.translate.z - closed.translate.z) * slid,
            };
            return pose;
        }

        void driveBolt(RockApiClient& rock, Gun& gun)
        {
            std::array<parts::DriveTarget, kBoneCount> drives{};
            std::uint32_t count = 0;
            for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                if (!gun.action.posed[bone] || !gun.action.keys[bone]) {
                    continue;
                }
                auto& drive = drives[count++];
                drive.flags = static_cast<std::uint32_t>(parts::TargetFlag::MatchSourceKey);
                drive.driveSpace = parts::DriveSpace::SourceParentLocal;
                drive.weaponGenerationKey = gun.generation;
                drive.sourceKey = gun.action.keys[bone];
                drive.leaseFrames = kDriveLeaseFrames;
                api_transform::fromNi(poseAt(gun, bone), drive.targetTransform);
            }
            if (rock.setWeaponDrives(0, std::span(drives).first(count))) {
                gun.driving = true;
            } else if (!gun.driveRefusalLogged) {
                gun.driveRefusalLogged = true;
                PAPER_LOG_WARN(Reload, "MANUAL_BOLT ROCK refused the bolt drive weapon={:08X}", gun.form);
            }
        }

        // Follows the free hand while it holds the bolt. The hand is measured in
        // the weapon's frame, so moving the gun itself pulls nothing.
        void followHand(RockApiClient& rock, Gun& gun, const Hand firingHand, const float pullDistance)
        {
            const Hand freeHand = firingHand == Hand::Left ? Hand::Right : Hand::Left;
            parts::GripState grip{};
            const bool held = rock.queryWeaponPartGripState(freeHand, grip) && grip.active && grip.attachOnly &&
                grip.gripSequence && grip.weaponGenerationKey == gun.generation &&
                grip.targetOwner == rock.ownerToken() && grip.targetGroupId == kGroupId;
            if (held) {
                rock::api::hands::HandFrame frame{};
                parts::SourcePose bolt{};
                if (rock.queryHandFrame(freeHand, frame) &&
                    rock.queryWeaponSourcePose(0, gun.generation, gun.action.keys[policy::kBoltBone], bolt) && bolt.valid) {
                    const auto root = transform_math::composeTransforms(
                        api_transform::toNi(bolt.world),
                        transform_math::invertTransform(api_transform::toNi(bolt.weaponRootLocal)));
                    const auto hand = transform_math::worldPointToLocal(root, api_transform::toNi(frame.transform).translate);
                    if (!gun.gripHeld || grip.gripSequence != gun.gripSequence) {
                        gun.gripSequence = grip.gripSequence;
                        gun.gripStart = hand;
                        gun.gripStartPull = gun.pull;
                    }
                    const float along = (hand.x - gun.gripStart.x) * gun.axis.x + (hand.y - gun.gripStart.y) * gun.axis.y +
                        (hand.z - gun.gripStart.z) * gun.axis.z;
                    gun.pull = std::clamp(gun.gripStartPull + along / pullDistance, 0.0f, 1.0f);
                }
            } else if (gun.pull <= kClosedAt) {
                gun.pull = 0.0f;
            }
            gun.gripHeld = held;
        }

        // The ends of the stroke: back throws out whatever the chamber holds, and
        // home feeds the next round. A bolt that stops short does neither.
        void applyStroke(Gun& gun, std::uint32_t& loaded)
        {
            if (gun.pull >= kBackAt && !gun.atBack) {
                gun.atBack = true;
                manual_reload_only::playSound(kSoundBoltOpen);
                if (gun.chamber == Chamber::Live && loaded > 0 && setLoadedRounds(gun.form, loaded - 1)) {
                    // Back to the reserve: loaded rounds are part of the inventory count.
                    --loaded;
                    gun.lastLoaded = loaded;
                }
                if (gun.chamber != Chamber::Empty) {
                    ejectCasing(gun.form);
                }
                PAPER_LOG_INFO(
                    Reload,
                    "MANUAL_BOLT back weapon={:08X} threw={} loaded={}",
                    gun.form,
                    static_cast<std::uint32_t>(gun.chamber),
                    loaded);
                gun.chamber = Chamber::Empty;
            } else if (gun.pull <= kClosedAt && gun.atBack) {
                gun.atBack = false;
                manual_reload_only::playSound(kSoundBoltClose);
                if (gun.chamber == Chamber::Empty && loaded > 0) {
                    gun.chamber = Chamber::Live;
                }
                PAPER_LOG_INFO(
                    Reload,
                    "MANUAL_BOLT home weapon={:08X} chamber={} loaded={}",
                    gun.form,
                    static_cast<std::uint32_t>(gun.chamber),
                    loaded);
            }
        }

        void applyRestore(Gun& gun, std::uint32_t& loaded)
        {
            if (!gun.restoreFrames) {
                return;
            }
            --gun.restoreFrames;
            // Only downwards after the first frame: a shot may already have been taken.
            const bool restoring = loaded > gun.restoreTarget || (gun.restoreFirstFrame && loaded != gun.restoreTarget);
            gun.restoreFirstFrame = false;
            if (restoring && setLoadedRounds(gun.form, gun.restoreTarget)) {
                PAPER_LOG_INFO(
                    Reload,
                    "MANUAL_BOLT restored weapon={:08X} loaded {} -> {}",
                    gun.form,
                    loaded,
                    gun.restoreTarget);
                loaded = gun.restoreTarget;
                gun.lastLoaded = loaded;
            }
        }

        // A reload the game completed leaves a live round; one that began with a
        // live round keeps it on top of the magazine, within what the inventory holds.
        void applyReload(RockApiClient& rock, Gun& gun, const bool reloading, std::uint32_t& loaded, const std::uint32_t reserve)
        {
            if (reloading && !gun.reloading) {
                gun.reloading = true;
                gun.reloadKeepsRound = gun.chamber == Chamber::Live && loaded > 0;
                gun.loadedAtReload = loaded;
                gun.pull = 0.0f;
                gun.atBack = false;
                gun.gripHeld = false;
                endDrive(rock, gun);
                endFireBlock(rock, gun);
            } else if (!reloading && gun.reloading) {
                gun.reloading = false;
                if (loaded > gun.loadedAtReload) {
                    gun.chamber = Chamber::Live;
                    gun.droppedWithoutShot = false;
                    const bool topUp = gun.reloadKeepsRound && reserve > 0 && setLoadedRounds(gun.form, loaded + 1);
                    PAPER_LOG_INFO(
                        Reload,
                        "MANUAL_BOLT reloaded weapon={:08X} loaded {} -> {} keptChambered={} reserve={}",
                        gun.form,
                        gun.loadedAtReload,
                        loaded + (topUp ? 1 : 0),
                        topUp,
                        reserve);
                    if (topUp) {
                        ++loaded;
                    }
                }
            }
        }

        void observeRounds(Gun& gun, const bool reloading, const std::uint32_t loaded)
        {
            const bool fell = loaded < gun.lastLoaded;
            if (!reloading && fell) {
                if (gun.triggerFrames > 0) {
                    gun.droppedWithoutShot = false;
                    gun.chamber = gun.handCycle ? Chamber::Spent : (loaded > 0 ? Chamber::Live : Chamber::Empty);
                    PAPER_LOG_INFO(
                        Reload,
                        "MANUAL_BOLT shot weapon={:08X} loaded={} chamber={}",
                        gun.form,
                        loaded,
                        static_cast<std::uint32_t>(gun.chamber));
                } else {
                    gun.droppedWithoutShot = true;
                }
            }
            // A gun on its native cycle chambers its next round by itself.
            if (!reloading && loaded > gun.lastLoaded && !gun.handCycle && gun.chamber == Chamber::Empty) {
                gun.chamber = Chamber::Live;
            }
            // So does one that turned out to have no bolt to hold, whatever was saved.
            if (gun.handCycleRefused && loaded > 0) {
                gun.chamber = Chamber::Live;
            }
            if (gun.chamber == Chamber::Live && loaded == 0) {
                gun.chamber = Chamber::Empty;
            }
            gun.lastLoaded = loaded;
        }

        void updateFireBlock(RockApiClient& rock, Gun& gun, const Hand firingHand, const std::uint32_t loaded, const bool triggerPressed)
        {
            // An empty gun stays the game's: its click or automatic reload.
            const bool block = gun.handCycle && !gun.reloading && loaded > 0 &&
                (gun.chamber != Chamber::Live || gun.pull > kClosedAt);
            if (!block) {
                endFireBlock(rock, gun);
                return;
            }
            const auto status = rock.setFireBlocked(gun.form, gun.generation, firingHand);
            gun.fireBlocked = status == rock::api::Status::Ok;
            if (!gun.fireBlocked && !gun.blockRefusalLogged) {
                gun.blockRefusalLogged = true;
                PAPER_LOG_WARN(
                    Reload,
                    "MANUAL_BOLT ROCK refused the trigger block weapon={:08X} status={}",
                    gun.form,
                    static_cast<std::uint32_t>(status));
            }
            if (triggerPressed) {
                manual_reload_only::playDryFire();
            }
        }

        void standDown(RockApiClient& rock, Gun& gun)
        {
            s_ownsCycle.store(false, std::memory_order_release);
            endFireBlock(rock, gun);
            endDrive(rock, gun);
            gun.gripHeld = false;
        }
    }

    void update(RockApiClient& rock, const FrameInput& input)
    {
        auto& gun = s_gun;
        rock::api::weapon::WeaponState state{};
        const bool present = input.enabled && manual_reload_only::physicalCycleReady() && rock.queryWeaponState(0, state) &&
            (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::WeaponEquipped)) != 0 &&
            state.weaponGenerationKey != 0;
        if (!present) {
            if (gun.generation) {
                forget(rock);
            }
            return;
        }
        checkNatives();
        const auto savedGeneration = s_savedGeneration.load(std::memory_order_acquire);
        if (state.weaponGenerationKey != gun.generation || state.weaponFormId != gun.form ||
            savedGeneration != gun.savedGeneration) {
            forget(rock);
            gun.generation = state.weaponGenerationKey;
            gun.form = state.weaponFormId;
            gun.savedGeneration = savedGeneration;
        }

        const bool ammoKnown = (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::AmmoKnown)) != 0;
        if (!gun.tracked) {
            if (!ammoKnown || bolt_hold::slotZeroFamily(gun.generation) != bolt_hold::BoltFamily::BoltAction) {
                return;
            }
            identify(gun, state);
        }

        const bool drawn = (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::WeaponDrawn)) != 0;
        const bool settled = (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::TransitionActive)) == 0;
        const Hand firingHand = state.firingHand;
        if (!ammoKnown || !drawn || !settled || !input.cycleAllowed || input.physicalReloadActive ||
            (firingHand != Hand::Left && firingHand != Hand::Right)) {
            standDown(rock, gun);
            return;
        }

        prepareHandCycle(rock, gun, firingHand);
        s_ownsCycle.store(gun.handCycle, std::memory_order_release);

        bool triggerHeld = false;
        (void)rock.queryTriggerHeld(firingHand, triggerHeld);
        const bool triggerPressed = triggerHeld && !gun.triggerHeld;
        gun.triggerHeld = triggerHeld;
        gun.triggerFrames = triggerHeld ? kShotTriggerFrames : (gun.triggerFrames > 0 ? gun.triggerFrames - 1 : 0);

        auto loaded = state.loadedRounds;
        const bool reloading = input.playerReloadActive;
        applyRestore(gun, loaded);
        applyReload(rock, gun, reloading, loaded, state.reserveRounds);
        observeRounds(gun, reloading, loaded);

        if (gun.handCycle && !reloading) {
            followHand(rock, gun, firingHand, input.pullDistance);
            applyStroke(gun, loaded);
            if (gun.pull > 0.0f || gun.gripHeld) {
                driveBolt(rock, gun);
            } else {
                endDrive(rock, gun);
            }
        }
        updateFireBlock(rock, gun, firingHand, loaded, triggerPressed);

        const auto marker = rock.setChamberMarker(gun.chamber == Chamber::Live ? Marker::Chambered : Marker::Empty);
        if (marker != rock::api::Status::Ok && !gun.markerRefusalLogged) {
            gun.markerRefusalLogged = true;
            PAPER_LOG_WARN(
                Reload,
                "MANUAL_BOLT ROCK refused the chamber marker weapon={:08X} status={}",
                gun.form,
                static_cast<std::uint32_t>(marker));
        }

        // The count falling with no shot is the weapon going away, not rounds spent.
        if (!gun.droppedWithoutShot && !reloading) {
            store(gun, loaded);
        }
    }

    void reset(RockApiClient& rock)
    {
        forget(rock);
    }

    bool ownsCycle() noexcept
    {
        return s_ownsCycle.load(std::memory_order_acquire);
    }

    namespace
    {
        // No exception crosses the F4SE callback boundary.
        template <class Body>
        void guarded(const char* what, Body&& body) noexcept
        {
            try {
                body();
            } catch (...) {
                OutputDebugStringA(what);
            }
        }

        void onSave(const F4SE::SerializationInterface* serialization)
        {
            guarded("PAPER: saving kept rounds failed\n", [&] {
                std::scoped_lock lock(s_savedMutex);
                if (!s_saved.empty() &&
                    !serialization->WriteRecord(
                        kRecordType,
                        kRecordVersion,
                        s_saved.data(),
                        static_cast<std::uint32_t>(s_saved.size() * sizeof(SavedItem)))) {
                    PAPER_LOG_WARN(Reload, "MANUAL_BOLT kept rounds not written: the co-save refused the record");
                }
            });
        }

        void onRevert(const F4SE::SerializationInterface*)
        {
            guarded("PAPER: clearing kept rounds failed\n", [] {
                std::scoped_lock lock(s_savedMutex);
                s_saved.clear();
                s_savedGeneration.fetch_add(1, std::memory_order_acq_rel);
            });
        }

        void onLoad(const F4SE::SerializationInterface* serialization)
        {
            guarded("PAPER: loading kept rounds failed\n", [&] {
                std::uint32_t type = 0;
                std::uint32_t version = 0;
                std::uint32_t length = 0;
                while (serialization->GetNextRecordInfo(type, version, length)) {
                    if (type != kRecordType) {
                        continue;
                    }
                    if (version != kRecordVersion || length % sizeof(SavedItem) != 0) {
                        PAPER_LOG_WARN(Reload, "MANUAL_BOLT kept rounds unreadable version={} length={}", version, length);
                        continue;
                    }
                    std::vector<SavedItem> items(length / sizeof(SavedItem));
                    if (serialization->ReadRecordData(items.data(), length) != length) {
                        PAPER_LOG_WARN(Reload, "MANUAL_BOLT kept rounds cut short length={}", length);
                        continue;
                    }
                    std::scoped_lock lock(s_savedMutex);
                    s_saved.clear();
                    for (auto item : items) {
                        // A form of a plugin no longer loaded resolves to nothing.
                        const auto weapon = serialization->ResolveFormID(item.weapon);
                        const auto owner = serialization->ResolveFormID(item.owner);
                        if (weapon && *weapon && owner && *owner) {
                            item.weapon = *weapon;
                            item.owner = *owner;
                            s_saved.push_back(item);
                        }
                    }
                    s_savedGeneration.fetch_add(1, std::memory_order_acq_rel);
                }
            });
        }
    }

    void installSerialization()
    {
        const auto* serialization = F4SE::GetSerializationInterface();
        if (!serialization) {
            PAPER_LOG_WARN(Reload, "MANUAL_BOLT no co-save interface: kept rounds last only until the game closes");
            return;
        }
        serialization->SetUniqueID(kSerializationId);
        serialization->SetSaveCallback(onSave);
        serialization->SetLoadCallback(onLoad);
        serialization->SetRevertCallback(onRevert);
    }
}
