#include "reload_control/BoltHold.h"

#include "PaperLog.h"
#include "api/ApiTransform.h"
#include "api/RockApiClient.h"
#include "reload_control/BoltHoldPolicy.h"
#include "reload_observation/ReloadObservation.h"
#include "support/TransformMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <span>
#include <vector>

namespace paper::bolt_hold
{
    namespace
    {
        namespace policy = bolt_hold_policy;
        using WeaponStateFlag = rock::api::weapon::WeaponStateFlag;

        constexpr std::size_t kBoneCount = policy::kActionBoneNames.size();
        constexpr std::uint32_t kDriveLeaseFrames = 2;
        // ROCK generates a weapon's parts over several frames after its equip.
        constexpr std::uint32_t kResolveFrames = 240;

        enum class PoseSource : std::uint8_t
        {
            None,
            ClipPeak,
            Dwell,
        };

        struct LearnedPose
        {
            std::uint32_t form{ 0 };
            PoseSource source{ PoseSource::None };
            float travel{ 0.0f };
            std::array<bool, kBoneCount> posed{};
            std::array<RE::NiTransform, kBoneCount> open{};
        };

        struct Bone
        {
            std::uint64_t key{ 0 };
            RE::NiTransform current{};
            RE::NiTransform rest{};
            RE::NiTransform dwell{};
            bool still{ false };
        };

        struct Slot
        {
            std::uint64_t generation{ 0 };
            std::uint32_t form{ 0 };
            std::uint32_t resolveFrames{ 0 };
            bool resolved{ false };
            bool eligible{ false };
            std::array<Bone, kBoneCount> bones{};
            RE::NiTransform lastBolt{};
            std::uint32_t stillFrames{ 0 };
            bool lastBoltValid{ false };
            bool restValid{ false };
            RE::NiTransform dwellBolt{};
            float dwellSeconds{ 0.0f };
            bool dwelling{ false };
            bool dwellSettled{ false };
            bool dwellCommitted{ false };
            float weight{ 0.0f };
            bool holding{ false };
            bool driveRefusalLogged{ false };
        };

        struct Displacement
        {
            float moveGameUnits{ 0.0f };
            float turnDegrees{ 0.0f };
        };

        // Frame thread only. A fixed table: the oldest pose gives way.
        std::array<LearnedPose, 64> s_learned{};
        std::size_t s_learnedNext{ 0 };
        std::array<Slot, rock::api::kMaxWeaponSlots> s_slots{};
        // Kept between frames: a weapon retries while ROCK builds its parts.
        std::vector<rock::api::weaponparts::Source> s_sources;
        std::vector<rock::api::weaponparts::EvidenceDetail> s_evidence;

        [[nodiscard]] bool hasFlag(const std::uint32_t flags, const WeaponStateFlag flag)
        {
            return (flags & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] Displacement displacement(const RE::NiTransform& from, const RE::NiTransform& to)
        {
            double trace = 0.0;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    trace += static_cast<double>(from.rotate.entry[row][column]) * to.rotate.entry[row][column];
                }
            }
            const double cosine = std::clamp((trace - 1.0) * 0.5, -1.0, 1.0);
            return Displacement{
                .moveGameUnits = from.translate.GetDistance(to.translate),
                .turnDegrees = static_cast<float>(std::acos(cosine) * 180.0 / std::numbers::pi),
            };
        }

        [[nodiscard]] LearnedPose* findLearned(const std::uint32_t form)
        {
            for (auto& entry : s_learned) {
                if (entry.form == form) {
                    return &entry;
                }
            }
            return nullptr;
        }

        [[nodiscard]] LearnedPose& learnedEntry(const std::uint32_t form)
        {
            if (auto* entry = findLearned(form)) {
                return *entry;
            }
            auto& entry = s_learned[s_learnedNext];
            s_learnedNext = (s_learnedNext + 1) % s_learned.size();
            entry = {};
            entry.form = form;
            return entry;
        }

        // The bolt where it is now, and each part where it is now if it sits
        // apart from its rest as a part of the action does.
        void storePose(LearnedPose& entry, const Slot& slot, const float travel, const bool requireStill)
        {
            entry.travel = travel;
            entry.posed = {};
            entry.posed[policy::kBoltBone] = true;
            entry.open[policy::kBoltBone] = slot.bones[policy::kBoltBone].current;
            for (std::size_t index = policy::kBoltBone + 1; index < kBoneCount; ++index) {
                const auto& bone = slot.bones[index];
                if (!bone.key || (requireStill && !bone.still)) {
                    continue;
                }
                const auto moved = displacement(bone.rest, bone.current);
                if (policy::partBelongsToPose(moved.moveGameUnits, moved.turnDegrees, travel)) {
                    entry.posed[index] = true;
                    entry.open[index] = bone.current;
                }
            }
        }

        void logDwellPose(const LearnedPose& entry, const Slot& slot)
        {
            std::array<char, 96> parts{};
            std::size_t used = 0;
            for (std::size_t index = policy::kBoltBone + 1; index < kBoneCount; ++index) {
                if (!entry.posed[index]) {
                    continue;
                }
                const auto moved = displacement(slot.bones[index].rest, entry.open[index]);
                const auto written = std::format_to_n(
                    parts.data() + used,
                    parts.size() - used - 1,
                    "E{}:{:.2f}gu/{:.1f}deg ",
                    index,
                    moved.moveGameUnits,
                    moved.turnDegrees);
                used = (std::min)(parts.size() - 1, used + static_cast<std::size_t>(written.size));
            }
            PAPER_LOG_INFO(
                Reload,
                "BOLT_HOLD learned dwell pose weapon={:08X} travel={:.2f}gu parts=[{}]",
                entry.form,
                entry.travel,
                parts.data());
        }

        void learnClipPeak(const Slot& slot, const float travel)
        {
            auto& entry = learnedEntry(slot.form);
            if (entry.source == PoseSource::Dwell || travel <= entry.travel) {
                return;
            }
            storePose(entry, slot, travel, false);
            entry.source = PoseSource::ClipPeak;
        }

        void learnDwell(const Slot& slot, const float travel)
        {
            auto& entry = learnedEntry(slot.form);
            const bool first = entry.source != PoseSource::Dwell;
            storePose(entry, slot, travel, true);
            entry.source = PoseSource::Dwell;
            if (first) {
                logDwellPose(entry, slot);
            }
        }

        void observeDwell(Slot& slot, const float travel, const float deltaSeconds)
        {
            if (travel < policy::kMinimumOpenTravelGameUnits) {
                slot.dwelling = false;
                return;
            }
            const auto& bolt = slot.bones[policy::kBoltBone].current;
            if (!slot.dwelling ||
                bolt.translate.GetDistance(slot.dwellBolt.translate) > policy::kDwellBoltDriftGameUnits) {
                slot.dwelling = true;
                slot.dwellSettled = false;
                slot.dwellCommitted = false;
                slot.dwellSeconds = 0.0f;
                slot.dwellBolt = bolt;
                return;
            }
            slot.dwellSeconds += deltaSeconds;
            if (slot.dwellSeconds < policy::kDwellSettleSeconds) {
                return;
            }
            if (!slot.dwellSettled) {
                slot.dwellSettled = true;
                for (auto& bone : slot.bones) {
                    bone.dwell = bone.current;
                    bone.still = true;
                }
                return;
            }
            for (auto& bone : slot.bones) {
                if (bone.key && bone.still) {
                    const auto drift = displacement(bone.dwell, bone.current);
                    bone.still = drift.moveGameUnits <= policy::kPartStillGameUnits &&
                                 drift.turnDegrees <= policy::kPartStillDegrees;
                }
            }
            if (!slot.dwellCommitted && slot.dwellSeconds >= policy::kDwellCommitSeconds) {
                slot.dwellCommitted = true;
                learnDwell(slot, travel);
            }
        }

        void observe(Slot& slot, const bool reloading, const float deltaSeconds)
        {
            // A hold's own output says nothing about the clips.
            if (slot.weight > 0.0f) {
                slot.stillFrames = 0;
                slot.lastBoltValid = false;
                slot.dwelling = false;
                return;
            }
            const auto& bolt = slot.bones[policy::kBoltBone].current;
            const bool still = slot.lastBoltValid &&
                bolt.translate.GetDistance(slot.lastBolt.translate) <= policy::kRestStillGameUnits;
            slot.lastBolt = bolt;
            slot.lastBoltValid = true;
            slot.stillFrames = still && !reloading ? slot.stillFrames + 1 : 0;
            if (slot.stillFrames >= policy::kRestStillFrames) {
                for (auto& bone : slot.bones) {
                    bone.rest = bone.current;
                }
                slot.restValid = true;
            }
            if (!slot.restValid) {
                return;
            }
            const float travel = bolt.translate.GetDistance(slot.bones[policy::kBoltBone].rest.translate);
            if (travel >= policy::kMinimumOpenTravelGameUnits) {
                learnClipPeak(slot, travel);
            }
            observeDwell(slot, travel, deltaSeconds);
        }

        void endHold(RockApiClient& rock, const std::uint32_t index, Slot& slot)
        {
            if (slot.weight > 0.0f) {
                rock.clearWeaponDrives(index);
            }
            slot.weight = 0.0f;
            slot.holding = false;
        }

        void forgetWeapon(RockApiClient& rock, const std::uint32_t index, Slot& slot)
        {
            endHold(rock, index, slot);
            slot = {};
        }

        // Finds the bones by name and decides the family, once ROCK has the
        // weapon's parts. False keeps trying within kResolveFrames.
        [[nodiscard]] bool resolve(RockApiClient& rock, const std::uint32_t index, Slot& slot)
        {
            std::uint32_t total = 0;
            if (rock.copyWeaponSources(index, slot.generation, nullptr, 0, total) != rock::api::Status::Ok || !total) {
                return false;
            }
            s_sources.resize((std::min)(total, rock::api::weaponparts::kMaxSources));
            if (rock.copyWeaponSources(
                    index,
                    slot.generation,
                    s_sources.data(),
                    static_cast<std::uint32_t>(s_sources.size()),
                    total) != rock::api::Status::Ok) {
                return false;
            }

            rock::api::weapon::Classification classification{};
            if (!rock.queryWeaponClassification(index, classification) || classification.valid == 0) {
                return false;
            }
            s_evidence.resize(rock::api::weaponparts::kMaxEvidenceDetails);
            std::uint32_t evidenceTotal = 0;
            const auto evidenceCount = rock.copyWeaponEvidenceDetails(
                index,
                s_evidence.data(),
                static_cast<std::uint32_t>(s_evidence.size()),
                evidenceTotal);
            if (!evidenceCount) {
                return false;
            }

            // A name on two nodes is ambiguous, and an ambiguous bone is not used.
            std::array<std::uint32_t, kBoneCount> matches{};
            for (const auto& source : s_sources) {
                for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                    if (policy::kActionBoneNames[bone] == source.name) {
                        slot.bones[bone].key = source.sourceKey;
                        ++matches[bone];
                    }
                }
            }
            std::uint32_t parts = 0;
            for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                if (matches[bone] != 1) {
                    slot.bones[bone].key = 0;
                } else if (bone != policy::kBoltBone) {
                    ++parts;
                }
            }

            const auto family = reload_observation::classifyWeaponFamilyFlags(
                slot.form,
                classification,
                std::span(s_evidence).first(evidenceCount));
            slot.eligible = slot.bones[policy::kBoltBone].key != 0 && policy::familyHoldsOpen(family);
            PAPER_LOG_INFO(
                Reload,
                "BOLT_HOLD weapon={:08X}/{:016X} slot={} boltMatches={} parts={} family={:#x} eligible={}",
                slot.form,
                slot.generation,
                index,
                matches[policy::kBoltBone],
                parts,
                family,
                slot.eligible);
            return true;
        }

        [[nodiscard]] bool readBones(RockApiClient& rock, const std::uint32_t index, Slot& slot)
        {
            for (auto& bone : slot.bones) {
                if (!bone.key) {
                    continue;
                }
                rock::api::weaponparts::SourcePose pose{};
                if (!rock.queryWeaponSourcePose(index, slot.generation, bone.key, pose)) {
                    return false;
                }
                bone.current = api_transform::toNi(pose.sourceParentLocal);
            }
            return true;
        }

        void driveHold(RockApiClient& rock, const std::uint32_t index, Slot& slot, const LearnedPose& learned)
        {
            const float blend = policy::easeHoldWeight(slot.weight);
            std::array<rock::api::weaponparts::DriveTarget, kBoneCount> drives{};
            std::uint32_t count = 0;
            for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                if (!learned.posed[bone] || !slot.bones[bone].key) {
                    continue;
                }
                auto& drive = drives[count++];
                drive.flags = static_cast<std::uint32_t>(rock::api::weaponparts::TargetFlag::MatchSourceKey);
                drive.driveSpace = rock::api::weaponparts::DriveSpace::SourceParentLocal;
                drive.weaponGenerationKey = slot.generation;
                drive.sourceKey = slot.bones[bone].key;
                drive.leaseFrames = kDriveLeaseFrames;
                api_transform::fromNi(
                    transform_math::interpolateTransforms(slot.bones[bone].current, learned.open[bone], blend),
                    drive.targetTransform);
            }
            if (!rock.setWeaponDrives(index, std::span(drives).first(count)) && !slot.driveRefusalLogged) {
                slot.driveRefusalLogged = true;
                PAPER_LOG_WARN(
                    Reload,
                    "BOLT_HOLD ROCK refused the drive weapon={:08X} slot={}; the bolt stays with its clip",
                    slot.form,
                    index);
            }
        }

        void updateSlot(RockApiClient& rock, const std::uint32_t index, const FrameInput& input)
        {
            auto& slot = s_slots[index];
            rock::api::weapon::WeaponState state{};
            // Equipped and generated: every source and pose call below validates
            // the live binding. ROCK publishes slot 1's state before that weapon's
            // presentation runs, so its WeaponPresent says nothing here.
            const bool present = rock.queryWeaponState(index, state) &&
                hasFlag(state.flags, WeaponStateFlag::WeaponEquipped) && state.weaponGenerationKey != 0;
            if (!present) {
                forgetWeapon(rock, index, slot);
                return;
            }
            if (state.weaponGenerationKey != slot.generation || state.weaponFormId != slot.form) {
                forgetWeapon(rock, index, slot);
                slot.generation = state.weaponGenerationKey;
                slot.form = state.weaponFormId;
            }
            if (!slot.resolved) {
                slot.resolved = resolve(rock, index, slot);
                if (!slot.resolved && ++slot.resolveFrames >= kResolveFrames) {
                    slot.resolved = true;
                    PAPER_LOG_INFO(
                        Reload,
                        "BOLT_HOLD weapon={:08X}/{:016X} slot={} has no published parts; no hold",
                        slot.form,
                        slot.generation,
                        index);
                }
                if (!slot.resolved) {
                    return;
                }
            }
            if (!slot.eligible) {
                return;
            }
            if (!readBones(rock, index, slot)) {
                endHold(rock, index, slot);
                return;
            }

            const bool reloading = index == 0 ?
                input.playerReloadActive :
                hasFlag(state.flags, WeaponStateFlag::SecondaryReloadActive);
            observe(slot, reloading, input.deltaSeconds);

            const auto* learned = findLearned(slot.form);
            const bool hold = input.enabled && learned && hasFlag(state.flags, WeaponStateFlag::AmmoKnown) &&
                state.loadedRounds == 0 && !reloading && !(index == 0 && input.physicalReloadActive);
            const float weight = policy::advanceHoldWeight(slot.weight, hold, input.deltaSeconds, input.closeSeconds);
            if (hold != slot.holding) {
                slot.holding = hold;
                PAPER_LOG_INFO(
                    Reload,
                    "BOLT_HOLD {} weapon={:08X} slot={} loaded={} reloading={} travel={:.2f}gu pose={}",
                    hold ? "open" : "close",
                    slot.form,
                    index,
                    state.loadedRounds,
                    reloading,
                    learned ? learned->travel : 0.0f,
                    learned && learned->source == PoseSource::Dwell ? "dwell" : "clip-peak");
            }
            if (weight <= 0.0f) {
                if (slot.weight > 0.0f) {
                    rock.clearWeaponDrives(index);
                }
                slot.weight = 0.0f;
                return;
            }
            slot.weight = weight;
            if (learned) {
                driveHold(rock, index, slot, *learned);
            }
        }
    }

    void update(RockApiClient& rock, const FrameInput& input)
    {
        for (std::uint32_t index = 0; index < s_slots.size(); ++index) {
            updateSlot(rock, index, input);
        }
    }

    void reset(RockApiClient& rock)
    {
        for (std::uint32_t index = 0; index < s_slots.size(); ++index) {
            forgetWeapon(rock, index, s_slots[index]);
        }
    }
}
