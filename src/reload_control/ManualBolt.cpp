#include "reload_control/ManualBolt.h"

#include "PaperLog.h"
#include "api/ApiTransform.h"
#include "api/RockApiClient.h"
#include "native/NativeOffsets.h"
#include "reload_control/BoltHold.h"
#include "reload_control/BoltStroke.h"
#include "reload_control/ChamberEjection.h"
#include "reload_control/ManualReloadOnly.h"
#include "weapon_classification/WeaponClassification.h"
#include "support/Fo4VrRuntime.h"
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
        // The bolt counts as back or home this far short of the end of its stroke, where
        // it plainly is by eye: a quick hand can turn it around before the very end.
        constexpr float kClosedAt = 0.25f;
        constexpr float kBackAt = 0.75f;
        // A manual bolt has two resting positions. Crossing this much of the pull commits
        // the whole stroke, and letting go never moves it.
        constexpr float kCommitAt = 0.5f;
        // ROCK builds a weapon's colliders over many frames after an equip. By then a
        // rifle with none on its bolt offers only the clip's hand its point, and with
        // that hand off the animation, keeps its native cycle.
        constexpr std::uint32_t kBoltColliderWaitFrames = 900;
        // Around the point the clip's hand holds the bolt, for the palm or fingers.
        constexpr float kGrabRadiusGameUnits = 8.0f;
        // PAPER's native cycle hand uses the same, above ROCK's own grip pose.
        constexpr std::int32_t kHandVisualPriority = 120;
        // A round leaves the gun only while the trigger is held or just was, so
        // the native count falling for any other reason, such as the weapon
        // going away, is never taken for a shot.
        constexpr std::uint32_t kShotTriggerFrames = 30;
        // The game can fill the magazine again for some frames after an equip.
        constexpr std::uint32_t kRestoreFrames = 90;
        constexpr std::size_t kMaxSavedItems = 1024;

        // Fallout4.esm, the hunting rifle's own bolt: WPNRifleHuntingAReloadBoltOpen, -BoltClose.

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
            std::uint32_t slot{ 0 };
            std::uint64_t generation{ 0 };
            std::uint32_t form{ 0 };
            std::uint32_t savedGeneration{ 0 };
            bool tracked{ false };
            // A bolt-action rifle a hand can work, only ever on slot 0. Every other gun
            // here fires cartridges and cycles itself.
            bool boltRifle{ false };
            bool noChamberLogged{ false };
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
            std::uint32_t boltColliderWaitFrames{ 0 };
            // What ROCK was last told a hand can take: the key the clip hand's point is on
            // and how many bolt colliders are on offer to the other hand.
            std::uint64_t publishedGeneration{ 0 };
            std::size_t publishedBoltParts{ 0 };
            bolt_hold::ManualAction action{};
            // The hand the clip works the bolt with.
            Hand workingHand{ Hand::None };
            // The hand holding it now. Only the clip's hand, at the clip's point, takes
            // its wrist and fingers from the clip; ROCK poses any other.
            Hand gripHand{ Hand::None };
            bool gripFromClip{ false };
            bolt_stroke::Pose pose{};

            float handBlendSeconds{ 0.0f };
            float deltaSeconds{ 0.0f };
            float handWeight{ 0.0f };
            bool handPublished{ false };
            bool handRefusalLogged{ false };
            // The hand as ROCK shows it when the grip begins, relative to the control
            // frame: PAPER's own publication would feed back into a presented read.
            bool handRestValid{ false };
            RE::NiTransform handFromControl{};
            std::uint32_t restFingerMask{ 0 };
            std::array<RE::NiTransform, bolt_stroke::kFingerJointCount> restFingers{};

            bool standingDown{ false };
            // Where the stroke is, and the end it is going to or resting at.
            float pull{ 0.0f };
            float pullTarget{ 0.0f };
            bool atBack{ false };
            // What the bolt reached the back with, thrown out once ROCK has placed the gun.
            Chamber ejecting{ Chamber::Empty };
            bool driving{ false };
            bool gripHeld{ false };
            std::uint64_t gripSequence{ 0 };
            // Each hand's controller in the weapon's frame, left then right, taken once
            // the weapon is placed for the frame. A hand is locked to the bolt while it
            // holds it, so only its controller can show the pull.
            std::array<bool, 2> controllerValid{};
            std::array<RE::NiPoint3, 2> controllerInWeapon{};
            RE::NiPoint3 gripStart{};
            float gripStartPull{ 0.0f };
            RE::NiPoint3 gripMoved{};
            float gripRequested{ 0.0f };

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

        std::array<Gun, 2> s_guns{ Gun{ .slot = 0 }, Gun{ .slot = 1 } };
        std::vector<parts::EvidenceDetail> s_evidence;

        // Each checked once, byte for byte, before the first call. A weapon without
        // what it needs loses that one effect and keeps working.
        struct Natives
        {
            bool checked{ false };
            bool setLoaded{ false };
            bool identity{ false };
            bool casing{ false };
            bool ammo{ false };
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
            // ROCK's trampoline holds the setter's first 14 bytes and clamps a refill to
            // what the other equipped weapon's magazine leaves. Calls go through it, so
            // this checks the body after it: the setup and the weapon-data call.
            s_natives.setLoaded = bytesMatch(
                offsets::kFunc_AIProcess_SetLoadedRounds + 0x0E,
                std::to_array<std::uint8_t>({
                    0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00,
                    0xE8, 0x64, 0xE7, 0x01, 0x00,
                    0x48, 0x8B, 0x4C, 0x24, 0x20, 0x84, 0xC0, 0x74, 0x03, 0x89, 0x59, 0x18,
                }));
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
            s_natives.ammo = bytesMatch(
                offsets::kFunc_Actor_GetEquippedAmmo,
                std::to_array<std::uint8_t>({
                    0x48, 0x83, 0xEC, 0x28,
                    0x48, 0x8B, 0x89, 0x00, 0x03, 0x00, 0x00,
                    0x48, 0x85, 0xC9, 0x74, 0x0A,
                    0xE8, 0xBB, 0xEC, 0x0B, 0x00,
                }));
            (void)chamber_ejection::install();
        }

        [[nodiscard]] RE::EquippedItem* equippedItem(const Gun& gun)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* process = player ? player->currentProcess : nullptr;
            auto* data = process ? process->middleHigh : nullptr;
            if (!data || data->equippedItems.empty() || data->equippedItems.size() > 8) {
                return nullptr;
            }
            for (auto& item : data->equippedItems) {
                if (item.equipIndex.index == gun.slot && item.item.object && item.item.object->formID == gun.form &&
                    item.item.object->formType == RE::ENUM_FORM_ID::kWEAP && item.data) {
                    return &item;
                }
            }
            return nullptr;
        }

        // Sets the native count, which the next frame's weapon state reports.
        [[nodiscard]] bool setLoadedRounds(const Gun& gun, const std::uint32_t rounds)
        {
            if (!s_natives.setLoaded || !equippedItem(gun)) {
                return false;
            }
            using Set = void (*)(RE::AIProcess*, std::uint32_t, std::uint32_t);
            reinterpret_cast<Set>(REL::Module::get().base() + offsets::kFunc_AIProcess_SetLoadedRounds)(
                RE::PlayerCharacter::GetSingleton()->currentProcess,
                gun.slot,
                rounds);
            return true;
        }

        // The game throws a case out only when the weapon's ammo names a casing model,
        // and no energy ammo does. The ballistic keyword then leaves out the launchers and
        // soakers whose ammo has one. Both come from the equipped weapon, not its record,
        // because weapon mods swap ammo and keywords. Neither waits for colliders.
        [[nodiscard]] bool firesCartridge(RockApiClient& rock, Gun& gun)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!s_natives.ammo || !player) {
                return false;
            }
            using GetAmmo = const RE::TESAmmo* (*)(RE::Actor*, std::uint32_t);
            const auto* ammo =
                reinterpret_cast<GetAmmo>(REL::Module::get().base() + offsets::kFunc_Actor_GetEquippedAmmo)(player, gun.slot);
            if (!ammo) {
                return false;
            }
            const bool casing = !ammo->shellCasing.model.empty();
            rock::api::weapon::Classification classification{};
            const bool keywordsKnown = casing && rock.queryWeaponClassification(gun.slot, classification) &&
                classification.weaponFormId == gun.form && classification.keywordFlags != 0;
            const bool ballistic = keywordsKnown &&
                (classification.keywordFlags & static_cast<std::uint64_t>(rock::api::weapon::KeywordFlag::Ballistic)) != 0;
            if (ballistic) {
                return true;
            }
            if ((!casing || keywordsKnown) && !gun.noChamberLogged) {
                gun.noChamberLogged = true;
                PAPER_LOG_INFO(
                    Reload,
                    "CHAMBER slot={} weapon={:08X} has no chamber: ammo={:08X} casing={} ballistic={}",
                    gun.slot,
                    gun.form,
                    ammo->formID,
                    casing,
                    ballistic);
            }
            return false;
        }

        [[nodiscard]] bool keepsChamberedRound(RockApiClient& rock, const Gun& gun)
        {
            rock::api::weapon::Classification classification{};
            return rock.queryWeaponClassification(gun.slot, classification) &&
                policy::familyKeepsChamberedRound(weapon_classification::classifyWeaponFamilyFlags(gun.form, classification, {}));
        }

        // The inventory item in the gun's slot, by weapon, owner and ExtraUniqueID, giving
        // it an ID from the engine's allocator if it has none. A stack of more than
        // one, or an equipped item that cannot be told from another, has no identity.
        [[nodiscard]] SavedItem equippedItemIdentity(const Gun& gun)
        {
            const auto form = gun.form;
            auto* item = equippedItem(gun);
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
                    "CHAMBER cannot tell the equipped item apart slot={} weapon={:08X} matches={} exact={} count={}; its rounds are not kept",
                    gun.slot,
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

        void throwOut(const Gun& gun, const Chamber what)
        {
            if (!s_natives.casing || !equippedItem(gun)) {
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
            reinterpret_cast<Get>(base + offsets::kFunc_Actor_GetEquippedInstance)(player, &instance, gun.slot);
            if (!instance.object || instance.object->formID != gun.form) {
                return;
            }
            const auto kind = what == Chamber::Live ? chamber_ejection::Kind::LiveRound : chamber_ejection::Kind::SpentCase;
            (void)chamber_ejection::eject(player, &instance, kind);
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
            gun.item = equippedItemIdentity(gun);
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
                "CHAMBER tracking slot={} weapon={:08X}/{:016X} boltRifle={} unique={} restored={} savedLoaded={} chamber={} loaded={} reserve={}",
                gun.slot,
                gun.form,
                gun.generation,
                gun.boltRifle,
                gun.item.unique,
                restored,
                saved.loaded,
                static_cast<std::uint32_t>(gun.chamber),
                state.loadedRounds,
                state.reserveRounds);
        }

        void refuseHandCycle(RockApiClient& rock, Gun& gun, const char* reason, const std::uint32_t status)
        {
            if (gun.handCycle) {
                rock.clearWeaponGrabPoints();
                rock.clearWeaponPartTargets();
                gun.handCycle = false;
            }
            gun.handCycleRefused = true;
            PAPER_LOG_INFO(
                Reload,
                "MANUAL_BOLT weapon={:08X} keeps its native cycle: {} status={}",
                gun.form,
                reason,
                status);
        }

        // Once the clips are read: what each hand can take. The clip's hand takes the
        // point the clip holds the bolt at, which asks nothing of the colliders, so it is
        // there as soon as the rifle is known. Any other hand takes the bolt's colliders
        // with ROCK's dynamic grip, once they exist, so this comes back for them.
        void prepareHandCycle(RockApiClient& rock, Gun& gun)
        {
            if (gun.handCycleRefused) {
                return;
            }
            const bool upToDate = gun.publishedGeneration == gun.generation;
            if (upToDate && (gun.publishedBoltParts != 0 || gun.boltColliderWaitFrames >= kBoltColliderWaitFrames)) {
                return;
            }
            const auto action = bolt_hold::queryManualAction(gun.generation, gun.action);
            if (action == bolt_hold::ManualActionState::Pending) {
                return;
            }
            if (action == bolt_hold::ManualActionState::None) {
                refuseHandCycle(rock, gun, "its cycle clip shows no hand working the bolt", 0);
                return;
            }
            const auto& stroke = *gun.action.stroke;

            s_evidence.resize(parts::kMaxEvidenceDetails);
            std::uint32_t total = 0;
            const auto count = rock.copyWeaponEvidenceDetails(
                0,
                s_evidence.data(),
                static_cast<std::uint32_t>(s_evidence.size()),
                total);
            std::vector<parts::Target> targets;
            const auto addTarget = [&](const std::uint64_t sourceKey) {
                const bool known = std::any_of(targets.begin(), targets.end(), [&](const parts::Target& target) {
                    return target.sourceKey == sourceKey;
                });
                if (known || targets.size() == parts::kMaxTargets) {
                    return;
                }
                auto& target = targets.emplace_back();
                // NonExclusive: the rest of the gun can still be held as usual.
                target.flags = static_cast<std::uint32_t>(parts::TargetFlag::MatchSourceKey) |
                    static_cast<std::uint32_t>(parts::TargetFlag::NonExclusive);
                target.grabMode = parts::GrabMode::AttachOnly;
                target.weaponGenerationKey = gun.generation;
                target.sourceKey = sourceKey;
                target.groupId = kGroupId;
                target.priority = 100;
            };
            for (const auto& detail : std::span(s_evidence).first(count)) {
                const bool boltPart = detail.partKind == static_cast<std::uint32_t>(parts::PartKind::Bolt) ||
                    detail.actionRole == static_cast<std::uint32_t>(parts::ActionRole::Bolt);
                if (boltPart && detail.sourceKey && detail.weaponGenerationKey == gun.generation) {
                    addTarget(detail.sourceKey);
                }
            }
            const auto boltParts = targets.size();
            // Nothing new to tell ROCK: the colliders are still to come, if they ever do.
            if (upToDate && boltParts == gun.publishedBoltParts) {
                if (++gun.boltColliderWaitFrames >= kBoltColliderWaitFrames) {
                    PAPER_LOG_INFO(Reload, "MANUAL_BOLT weapon={:08X} has no bolt collider for another hand to hold", gun.form);
                }
                return;
            }
            addTarget(gun.action.keys[policy::kBoltBone]);
            if (targets.empty()) {
                if (++gun.boltColliderWaitFrames >= kBoltColliderWaitFrames) {
                    refuseHandCycle(rock, gun, "no bolt collider to hold", 0);
                }
                return;
            }

            if (const auto status = rock.setWeaponPartTargets(targets); status != rock::api::Status::Ok) {
                refuseHandCycle(rock, gun, "ROCK refused the bolt as a target", static_cast<std::uint32_t>(status));
                return;
            }
            gun.workingHand = stroke.hand == bolt_stroke::kLeftHand ? Hand::Left : Hand::Right;
            {
                parts::GrabPoint point{};
                point.pointId = 1;
                point.groupId = kGroupId;
                point.weaponGenerationKey = gun.generation;
                point.sourceKey = gun.action.keys[policy::kBoltBone];
                point.handMask = stroke.hand == bolt_stroke::kLeftHand ? 1u : 2u;
                point.radiusGame = kGrabRadiusGameUnits;
                point.pointInSource = { stroke.graspInBolt.x, stroke.graspInBolt.y, stroke.graspInBolt.z };
                if (const auto status = rock.setWeaponGrabPoints({ &point, 1 }); status != rock::api::Status::Ok) {
                    rock.clearWeaponPartTargets();
                    refuseHandCycle(rock, gun, "ROCK refused the grab point", static_cast<std::uint32_t>(status));
                    return;
                }
            }
            gun.handCycle = true;
            gun.publishedGeneration = gun.generation;
            gun.publishedBoltParts = boltParts;
            PAPER_LOG_INFO(
                Reload,
                "MANUAL_BOLT hand cycle ready weapon={:08X} boltBodies={} hand={} travel={:.2f}gu turn={:.1f}deg",
                gun.form,
                boltParts,
                stroke.hand == bolt_stroke::kLeftHand ? "left" : "right",
                stroke.travelGameUnits,
                stroke.turnDegrees);
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

        void forget(RockApiClient& rock, Gun& gun)
        {
            if (gun.slot == 0) {
                s_ownsCycle.store(false, std::memory_order_release);
            }
            endDrive(rock, gun);
            endFireBlock(rock, gun);
            if (gun.handPublished) {
                rock.clearHandVisualAuthority(gun.workingHand);
            }
            if (gun.handCycle) {
                rock.clearWeaponGrabPoints();
                rock.clearWeaponPartTargets();
            }
            gun = Gun{ .slot = gun.slot };
        }

        void driveBolt(RockApiClient& rock, Gun& gun)
        {
            const auto& stroke = *gun.action.stroke;
            std::array<parts::DriveTarget, kBoneCount> drives{};
            std::uint32_t count = 0;
            for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                if (!stroke.moved[bone] || !gun.action.keys[bone]) {
                    continue;
                }
                auto& drive = drives[count++];
                drive.flags = static_cast<std::uint32_t>(parts::TargetFlag::MatchSourceKey);
                drive.driveSpace = parts::DriveSpace::SourceParentLocal;
                drive.weaponGenerationKey = gun.generation;
                drive.sourceKey = gun.action.keys[bone];
                drive.leaseFrames = kDriveLeaseFrames;
                api_transform::fromNi(gun.pose.bones[bone], drive.targetTransform);
            }
            if (rock.setWeaponDrives(0, std::span(drives).first(count))) {
                gun.driving = true;
            } else if (!gun.driveRefusalLogged) {
                gun.driveRefusalLogged = true;
                PAPER_LOG_WARN(Reload, "MANUAL_BOLT ROCK refused the bolt drive weapon={:08X}", gun.form);
            }
        }

        [[nodiscard]] RE::NiTransform weaponRootWorld(const parts::SourcePose& source)
        {
            return transform_math::composeTransforms(
                api_transform::toNi(source.world),
                transform_math::invertTransform(api_transform::toNi(source.weaponRootLocal)));
        }

        // The hand as ROCK shows it now, to blend from and back to. Read before PAPER
        // takes the hand: afterwards the presented hand is PAPER's own.
        void captureHandRest(RockApiClient& rock, Gun& gun)
        {
            gun.handRestValid = false;
            gun.restFingerMask = 0;
            rock::api::hands::HandFrame control{};
            rock::api::hands::PresentedHandPose presented{};
            if (gun.handPublished || !rock.queryHandFrame(gun.workingHand, control) ||
                !rock.queryPresentedHandPose(gun.workingHand, presented)) {
                return;
            }
            gun.handFromControl = transform_math::composeTransforms(
                transform_math::invertTransform(api_transform::toNi(control.transform)),
                api_transform::toNi(presented.handWorld));
            gun.handRestValid = true;
            gun.restFingerMask = presented.fingerLocalTransformMask;
            for (std::size_t joint = 0; joint < gun.restFingers.size(); ++joint) {
                if ((gun.restFingerMask & (1u << joint)) != 0) {
                    gun.restFingers[joint] = api_transform::toNi(presented.fingerLocalTransforms[joint]);
                }
            }
        }

        [[nodiscard]] std::size_t handIndex(const Hand hand) noexcept
        {
            return hand == Hand::Left ? 0u : 1u;
        }

        // The controller of the hand on the bolt decides which end the bolt goes to while
        // it holds it, and the bolt gets there at the clip's speed, held or not. The
        // controller is measured in the weapon's frame, so moving the gun itself pulls
        // nothing. The grab point stays fixed so a slow pull adds up.
        void followHand(RockApiClient& rock, Gun& gun, const float pullDistance)
        {
            parts::GripState grip{};
            const auto holds = [&](const Hand hand) {
                return rock.queryWeaponPartGripState(hand, grip) && grip.active && grip.attachOnly &&
                    grip.gripSequence && grip.weaponGenerationKey == gun.generation &&
                    grip.targetOwner == rock.ownerToken() && grip.targetGroupId == kGroupId;
            };
            const Hand otherHand = gun.workingHand == Hand::Left ? Hand::Right : Hand::Left;
            const Hand holder = gun.gripHeld && holds(gun.gripHand) ? gun.gripHand :
                holds(gun.workingHand)                              ? gun.workingHand :
                holds(otherHand)                                    ? otherHand :
                                                                      Hand::None;
            const bool held = holder != Hand::None;
            if (held && gun.controllerValid[handIndex(holder)]) {
                const auto& controller = gun.controllerInWeapon[handIndex(holder)];
                if (!gun.gripHeld || grip.gripSequence != gun.gripSequence) {
                    gun.gripSequence = grip.gripSequence;
                    gun.gripHand = holder;
                    gun.gripFromClip = holder == gun.workingHand;
                    gun.gripStart = controller;
                    gun.gripStartPull = gun.pull;
                    if (gun.gripFromClip) {
                        captureHandRest(rock, gun);
                    }
                    PAPER_LOG_INFO(
                        Reload,
                        "MANUAL_BOLT grip weapon={:08X} pull={:.2f} hand={} fromClip={} body={}",
                        gun.form,
                        gun.pull,
                        holder == Hand::Left ? "left" : "right",
                        gun.gripFromClip,
                        grip.bodyId);
                }
                gun.gripHeld = true;
                gun.gripMoved = { controller.x - gun.gripStart.x, controller.y - gun.gripStart.y, controller.z - gun.gripStart.z };
                const auto& axis = gun.action.stroke->backInWeapon;
                const float along = gun.gripMoved.x * axis.x + gun.gripMoved.y * axis.y + gun.gripMoved.z * axis.z;
                gun.gripRequested = gun.gripStartPull + along / pullDistance;
                gun.pullTarget = gun.gripRequested > kCommitAt ? 1.0f : 0.0f;
            } else if (!held && gun.gripHeld) {
                gun.gripHeld = false;
                gun.gripFromClip = false;
                PAPER_LOG_INFO(
                    Reload,
                    "MANUAL_BOLT let go weapon={:08X} moved=({:.2f},{:.2f},{:.2f})gu requested={:.2f} pull={:.2f} target={:.0f}",
                    gun.form,
                    gun.gripMoved.x,
                    gun.gripMoved.y,
                    gun.gripMoved.z,
                    gun.gripRequested,
                    gun.pull,
                    gun.pullTarget);
            }

            const float step = gun.deltaSeconds / gun.action.stroke->seconds;
            gun.pull = gun.pullTarget < gun.pull ? (std::max)(gun.pullTarget, gun.pull - step) :
                                                   (std::min)(gun.pullTarget, gun.pull + step);
        }

        // The rifle's own sounds, each as the bolt passes its point in its direction.
        void soundStroke(const Gun& gun, const float pullBefore)
        {
            for (const auto& cue : gun.action.stroke->sounds) {
                const bool passed = cue.closing ? gun.pull <= cue.pull && pullBefore > cue.pull :
                                                  gun.pull >= cue.pull && pullBefore < cue.pull;
                if (passed) {
                    manual_reload_only::playSound(cue.form);
                }
            }
        }

        // The ends of the stroke: back throws out whatever the chamber holds, and
        // home feeds the next round. A bolt that stops short does neither.
        void applyStroke(Gun& gun, std::uint32_t& loaded)
        {
            if (gun.pull >= kBackAt && !gun.atBack) {
                gun.atBack = true;
                gun.ejecting = gun.chamber;
                if (gun.chamber == Chamber::Live) {
                    if (loaded > 0 && setLoadedRounds(gun, loaded - 1)) {
                        // Loaded rounds are part of the inventory count, so this round is
                        // in the reserve now, until the throw takes it out as a real round.
                        --loaded;
                        gun.lastLoaded = loaded;
                    } else {
                        gun.ejecting = Chamber::Empty;
                    }
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
            if (restoring && setLoadedRounds(gun, gun.restoreTarget)) {
                PAPER_LOG_INFO(
                    Reload,
                    "CHAMBER restored slot={} weapon={:08X} loaded {} -> {}",
                    gun.slot,
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
                gun.pullTarget = 0.0f;
                gun.atBack = false;
                gun.gripHeld = false;
                endDrive(rock, gun);
                endFireBlock(rock, gun);
            } else if (!reloading && gun.reloading) {
                gun.reloading = false;
                if (loaded > gun.loadedAtReload) {
                    gun.chamber = Chamber::Live;
                    gun.droppedWithoutShot = false;
                    const bool topUp = gun.reloadKeepsRound && reserve > 0 && keepsChamberedRound(rock, gun) &&
                        setLoadedRounds(gun, loaded + 1);
                    PAPER_LOG_INFO(
                        Reload,
                        "CHAMBER reloaded slot={} weapon={:08X} loaded {} -> {} keptChambered={} reserve={}",
                        gun.slot,
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

        // handWorks: the hand owns this gun's cycle now. nativeCycle: it never will, so what
        // was saved says nothing about the chamber.
        void observeRounds(Gun& gun, const bool reloading, const std::uint32_t loaded, const bool handWorks, const bool nativeCycle)
        {
            const bool fell = loaded < gun.lastLoaded;
            if (!reloading && fell) {
                if (gun.triggerFrames > 0) {
                    gun.droppedWithoutShot = false;
                    gun.chamber = handWorks ? Chamber::Spent : (loaded > 0 ? Chamber::Live : Chamber::Empty);
                    // An automatic fires too fast for a line per shot.
                    if (handWorks) {
                        PAPER_LOG_INFO(
                            Reload,
                            "MANUAL_BOLT shot weapon={:08X} loaded={} chamber={}",
                            gun.form,
                            loaded,
                            static_cast<std::uint32_t>(gun.chamber));
                    }
                } else {
                    gun.droppedWithoutShot = true;
                }
            }
            // A gun on its native cycle chambers its next round by itself.
            if (loaded > 0 && (nativeCycle || (!reloading && loaded > gun.lastLoaded && !handWorks && gun.chamber == Chamber::Empty))) {
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
                (gun.chamber != Chamber::Live || gun.pull > 0.0f);
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
            if (gun.slot == 0) {
                s_ownsCycle.store(false, std::memory_order_release);
            }
            endFireBlock(rock, gun);
            endDrive(rock, gun);
            gun.gripHeld = false;
        }

        void updateGun(RockApiClient& rock, const FrameInput& input, Gun& gun)
        {
            rock::api::weapon::WeaponState state{};
            const auto key = rock.queryWeaponInteractionKey(gun.slot);
            const bool boltAllowed = gun.slot == 0 && input.boltEnabled && manual_reload_only::physicalCycleReady();
            const bool present = (input.chamberEnabled || boltAllowed) && rock.queryWeaponState(gun.slot, state) &&
                (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::WeaponEquipped)) != 0 && key != 0;
            if (!present) {
                if (gun.generation) {
                    forget(rock, gun);
                }
                return;
            }
            checkNatives();
            const auto savedGeneration = s_savedGeneration.load(std::memory_order_acquire);
            const auto startOver = [&] {
                forget(rock, gun);
                gun.generation = key;
                gun.form = state.weaponFormId;
                gun.savedGeneration = savedGeneration;
            };
            if (key != gun.generation || state.weaponFormId != gun.form || savedGeneration != gun.savedGeneration) {
                // The same gun under a new key: ROCK's colliders arrived or were rebuilt.
                // The bolt, the chamber and a hand on the bolt all carry on. The game can fill
                // the magazine for as long after that as it did before ROCK knew the gun. A
                // rebuild can also come from a weapon mod that changed its ammo.
                const bool sameGun = gun.tracked && state.weaponFormId == gun.form &&
                    savedGeneration == gun.savedGeneration && equippedItemIdentity(gun).unique == gun.item.unique &&
                    (gun.boltRifle || firesCartridge(rock, gun));
                if (sameGun) {
                    gun.generation = key;
                    gun.boltColliderWaitFrames = 0;
                    if (gun.restoreTarget) {
                        gun.restoreFrames = kRestoreFrames;
                    }
                } else {
                    startOver();
                }
            }

            const auto family = gun.slot == 0 ? bolt_hold::slotZeroFamily(gun.generation) : bolt_hold::BoltFamily::NotBoltAction;
            // Its parts, once the colliders show them, can still say it is no rifle after all.
            if (gun.boltRifle && (!boltAllowed || family == bolt_hold::BoltFamily::NotBoltAction)) {
                startOver();
                return;
            }
            if (gun.tracked && !gun.boltRifle && !input.chamberEnabled) {
                startOver();
                return;
            }

            gun.handBlendSeconds = input.handBlendSeconds;
            gun.deltaSeconds = input.deltaSeconds;

            const bool ammoKnown = (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::AmmoKnown)) != 0;
            // A cartridge gun is known at once. It can still turn out to be a rifle, which a
            // hand then works from there on, with the rounds and chamber it already has.
            gun.boltRifle = gun.boltRifle || (boltAllowed && family == bolt_hold::BoltFamily::BoltAction);
            if (!gun.tracked) {
                if (!ammoKnown || (!gun.boltRifle && !(input.chamberEnabled && firesCartridge(rock, gun)))) {
                    return;
                }
                identify(gun, state);
            }

            const bool drawn = (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::WeaponDrawn)) != 0;
            const Hand firingHand = state.firingHand;
            // The rounds and the hand on the bolt both wait for these.
            const char* roundsWaitFor = !ammoKnown ? "ammo unknown" :
                !drawn ? "weapon not drawn" :
                gun.slot == 0 && input.physicalReloadActive ? "a physical reload owns the weapon" :
                firingHand != Hand::Left && firingHand != Hand::Right ? "no firing hand" :
                nullptr;
            // The hand also waits for this one, while the game cycles the gun.
            const char* standDownReason = roundsWaitFor != nullptr ? roundsWaitFor :
                gun.boltRifle && !input.cycleAllowed ? "PAPER does not animate this weapon's cycle now" :
                nullptr;
            if (standDownReason != nullptr) {
                if (!gun.standingDown) {
                    gun.standingDown = true;
                    PAPER_LOG_INFO(Reload, "CHAMBER stands down slot={} weapon={:08X}: {}", gun.slot, gun.form, standDownReason);
                }
                standDown(rock, gun);
            } else if (gun.standingDown) {
                gun.standingDown = false;
                PAPER_LOG_INFO(Reload, "CHAMBER resumes slot={} weapon={:08X}", gun.slot, gun.form);
            }
            if (roundsWaitFor != nullptr) {
                return;
            }

            const bool handAvailable = gun.boltRifle && !gun.standingDown;
            if (handAvailable) {
                prepareHandCycle(rock, gun);
            }
            const bool handWorks = handAvailable && gun.handCycle;
            if (gun.slot == 0) {
                s_ownsCycle.store(handWorks, std::memory_order_release);
            }

            bool triggerHeld = false;
            (void)rock.queryTriggerHeld(firingHand, triggerHeld);
            const bool triggerPressed = triggerHeld && !gun.triggerHeld;
            gun.triggerHeld = triggerHeld;
            gun.triggerFrames = triggerHeld ? kShotTriggerFrames : (gun.triggerFrames > 0 ? gun.triggerFrames - 1 : 0);

            auto loaded = state.loadedRounds;
            const bool reloading = gun.slot == 0 ?
                input.playerReloadActive :
                (state.flags & static_cast<std::uint32_t>(WeaponStateFlag::SecondaryReloadActive)) != 0;
            const bool nativeCycle = !boltAllowed || family == bolt_hold::BoltFamily::NotBoltAction || gun.handCycleRefused;
            applyRestore(gun, loaded);
            applyReload(rock, gun, reloading, loaded, state.reserveRounds);
            observeRounds(gun, reloading, loaded, handWorks, nativeCycle);

            if (handWorks && !reloading) {
                const float pullBefore = gun.pull;
                followHand(rock, gun, input.pullDistance);
                soundStroke(gun, pullBefore);
                bolt_stroke::sample(*gun.action.stroke, gun.pull, gun.pose);
                applyStroke(gun, loaded);
                if ((gun.pull > 0.0f || gun.gripHeld) && gun.publishedGeneration == gun.generation) {
                    driveBolt(rock, gun);
                } else {
                    endDrive(rock, gun);
                }
            }
            if (handAvailable) {
                updateFireBlock(rock, gun, firingHand, loaded, triggerPressed);
            }

            const auto marker = rock.setChamberMarker(gun.slot, gun.chamber == Chamber::Live ? Marker::Chambered : Marker::Empty);
            if (marker != rock::api::Status::Ok && !gun.markerRefusalLogged) {
                gun.markerRefusalLogged = true;
                PAPER_LOG_WARN(
                    Reload,
                    "CHAMBER ROCK refused the chamber marker slot={} weapon={:08X} status={}",
                    gun.slot,
                    gun.form,
                    static_cast<std::uint32_t>(marker));
            }

            // The count falling with no shot is the weapon going away, not rounds spent.
            if (!gun.droppedWithoutShot && !reloading) {
                store(gun, loaded);
            }
        }
    }

    void update(RockApiClient& rock, const FrameInput& input)
    {
        for (auto& gun : s_guns) {
            updateGun(rock, input, gun);
        }
    }

    void presentHand(RockApiClient& rock)
    {
        auto& gun = s_guns[0];
        if (gun.ejecting != Chamber::Empty) {
            throwOut(gun, gun.ejecting);
            gun.ejecting = Chamber::Empty;
        }
        if (!gun.handCycle) {
            return;
        }
        const auto end = [&] {
            if (gun.handPublished) {
                rock.clearHandVisualAuthority(gun.workingHand);
                gun.handPublished = false;
            }
            gun.handWeight = 0.0f;
        };

        parts::SourcePose bolt{};
        const bool boltRead =
            rock.queryWeaponSourcePose(0, gun.generation, gun.action.keys[policy::kBoltBone], bolt) && bolt.valid;
        for (const Hand hand : { Hand::Left, Hand::Right }) {
            const auto index = handIndex(hand);
            const auto* controller = f4vr::getControllerNode(hand == Hand::Left);
            gun.controllerValid[index] = boltRead && controller;
            if (gun.controllerValid[index]) {
                gun.controllerInWeapon[index] =
                    transform_math::worldPointToLocal(weaponRootWorld(bolt), controller->world.translate);
            }
        }

        const bool wanted = gun.gripHeld && gun.gripFromClip;
        const float step = gun.handBlendSeconds > 0.0f && gun.handRestValid ? gun.deltaSeconds / gun.handBlendSeconds : 1.0f;
        gun.handWeight = std::clamp(gun.handWeight + (wanted ? step : -step), 0.0f, 1.0f);
        if (gun.handWeight <= 0.0f) {
            end();
            return;
        }

        if (!boltRead) {
            end();
            return;
        }
        const auto target = transform_math::composeTransforms(weaponRootWorld(bolt), gun.pose.handInWeapon);
        const float alpha = policy::easeHoldWeight(gun.handWeight);
        auto world = target;
        if (gun.handRestValid && alpha < 1.0f) {
            rock::api::hands::HandFrame control{};
            if (rock.queryHandFrame(gun.workingHand, control)) {
                world = transform_math::interpolateTransforms(
                    transform_math::composeTransforms(api_transform::toNi(control.transform), gun.handFromControl),
                    target,
                    alpha);
            }
        }

        const auto& stroke = *gun.action.stroke;
        rock::api::animation::HandVisualAuthorityRequest request{};
        request.hand = gun.workingHand;
        request.priority = kHandVisualPriority;
        request.flags = static_cast<std::uint32_t>(rock::api::animation::HandVisualAuthorityFlag::WorldTransform) |
            static_cast<std::uint32_t>(rock::api::animation::HandVisualAuthorityFlag::FingerLocalTransforms);
        request.fingerLocalTransformMask = stroke.fingerMask;
        api_transform::fromNi(world, request.worldTransform);
        for (std::size_t joint = 0; joint < gun.pose.fingers.size(); ++joint) {
            const auto bit = 1u << joint;
            if ((stroke.fingerMask & bit) == 0) {
                continue;
            }
            api_transform::fromNi(
                (gun.restFingerMask & bit) != 0 ?
                    transform_math::interpolateTransforms(gun.restFingers[joint], gun.pose.fingers[joint], alpha) :
                    gun.pose.fingers[joint],
                request.fingerLocalTransforms[joint]);
        }
        if (rock.setHandVisualAuthority(request)) {
            gun.handPublished = true;
        } else if (!gun.handRefusalLogged) {
            gun.handRefusalLogged = true;
            PAPER_LOG_WARN(Reload, "MANUAL_BOLT ROCK refused the hand pose weapon={:08X}", gun.form);
        }
    }

    void reset(RockApiClient& rock)
    {
        for (auto& gun : s_guns) {
            forget(rock, gun);
        }
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
                    PAPER_LOG_WARN(Reload, "CHAMBER kept rounds not written: the co-save refused the record");
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
                        PAPER_LOG_WARN(Reload, "CHAMBER kept rounds unreadable version={} length={}", version, length);
                        continue;
                    }
                    std::vector<SavedItem> items(length / sizeof(SavedItem));
                    if (serialization->ReadRecordData(items.data(), length) != length) {
                        PAPER_LOG_WARN(Reload, "CHAMBER kept rounds cut short length={}", length);
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
            PAPER_LOG_WARN(Reload, "CHAMBER no co-save interface: kept rounds last only until the game closes");
            return;
        }
        serialization->SetUniqueID(kSerializationId);
        serialization->SetSaveCallback(onSave);
        serialization->SetLoadCallback(onLoad);
        serialization->SetRevertCallback(onRevert);
    }
}
