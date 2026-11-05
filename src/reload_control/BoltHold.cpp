#include "reload_control/BoltHold.h"

#include "PaperLog.h"
#include "animation_evidence/ExactClipPreharvest.h"
#include "api/ApiTransform.h"
#include "api/RockApiClient.h"
#include "reload_control/BoltHoldPolicy.h"
#include "reload_observation/ReloadObservation.h"
#include "support/TransformMath.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace paper::bolt_hold
{
    namespace
    {
        namespace policy = bolt_hold_policy;
        namespace sampler = exact_clip_preharvest;
        using WeaponStateFlag = rock::api::weapon::WeaponStateFlag;
        using Clock = std::chrono::steady_clock;

        constexpr std::size_t kBoneCount = policy::kActionBoneNames.size();
        constexpr std::uint32_t kDriveLeaseFrames = 2;
        // ROCK builds a weapon's part evidence over several frames after its equip, and
        // a revolver with no keyword is known only by that. A gun with none by the end
        // of this wait is judged on its flags and keywords.
        constexpr std::uint32_t kEvidenceWaitFrames = 240;

        // Best last. A dwell is the animator's locked-back pose. A stroke's
        // farthest frame only stands in for one, and a stroke outside a reload is
        // the safer of the two: a reload moves props through it.
        enum class PoseSource : std::uint8_t
        {
            None,
            ReloadStroke,
            OtherStroke,
            Dwell,
        };

        struct OpenPose
        {
            PoseSource source{ PoseSource::None };
            float travel{ 0.0f };
            std::array<bool, kBoneCount> posed{};
            std::array<RE::NiTransform, kBoneCount> open{};
            // The idle clip's pose of each bone, set when the read completes.
            std::array<RE::NiTransform, kBoneCount> closed{};
        };

        // A form with PoseSource::None has been read and has no pose.
        struct LearnedPose
        {
            std::uint32_t form{ 0 };
            OpenPose pose{};
        };

        struct Bone
        {
            std::uint64_t key{ 0 };
            RE::NiTransform current{};
        };

        struct Slot
        {
            std::uint64_t generation{ 0 };
            std::uint32_t form{ 0 };
            std::uint32_t evidenceWaitFrames{ 0 };
            bool resolved{ false };
            bool eligible{ false };
            bool boltAction{ false };
            std::array<Bone, kBoneCount> bones{};
            float weight{ 0.0f };
            bool holding{ false };
            bool driveRefusalLogged{ false };
        };

        struct Displacement
        {
            float moveGameUnits{ 0.0f };
            float turnDegrees{ 0.0f };
        };

        struct ClipDwell
        {
            bool active{ false };
            bool settled{ false };
            bool committed{ false };
            float startSeconds{ 0.0f };
            RE::NiTransform bolt{};
            std::array<RE::NiTransform, kBoneCount> parked{};
            std::array<bool, kBoneCount> still{};
        };

        using ClipPose = std::array<const RE::NiTransform*, kBoneCount>;

        struct PoseCandidate
        {
            OpenPose pose{};
            std::array<char, 64> clip{};
            float seconds{ 0.0f };
        };

        struct PoseRead
        {
            bool active{ false };
            std::uint32_t slot{ 0 };
            std::uint32_t form{ 0 };
            std::uint64_t generation{ 0 };
            // The bones ROCK publishes on this weapon, in the same order as the names.
            std::array<bool, kBoneCount> published{};
            // Each bone its idle clip tracks, where that clip keeps it: the action
            // closed. Never the live bones, which sit wherever they were left until
            // the graph is running this weapon's subgraph, and never the skeleton's
            // bind pose, which every weapon shares.
            bool closedKnown{ false };
            std::array<bool, kBoneCount> closedTracked{};
            std::array<RE::NiTransform, kBoneCount> closed{};
            // The clip being read: it counts only if it also shows the bolt closed.
            policy::ClipKind clipKind{ policy::ClipKind::Other };
            ClipDwell dwell{};
            bool clipShowsClosed{ false };
            PoseCandidate clipBest{};
            PoseCandidate best{};
            std::uint32_t clipsRead{ 0 };
            Clock::time_point started{};
            std::int64_t longestStepMicroseconds{ 0 };
        };

        // Frame thread only. A fixed table: the oldest pose gives way.
        std::array<LearnedPose, 64> s_learned{};
        std::size_t s_learnedNext{ 0 };
        std::array<Slot, rock::api::kMaxWeaponSlots> s_slots{};
        PoseRead s_read{};
        std::uint64_t s_readFailedGeneration{ 0 };
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

        [[nodiscard]] const char* poseName(const PoseSource source)
        {
            switch (source) {
            case PoseSource::Dwell:
                return "dwell";
            case PoseSource::OtherStroke:
                return "stroke";
            case PoseSource::ReloadStroke:
                return "reload-stroke";
            default:
                return "none";
            }
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

        void storeLearned(const std::uint32_t form, const OpenPose& pose)
        {
            auto* entry = findLearned(form);
            if (!entry) {
                entry = &s_learned[s_learnedNext];
                s_learnedNext = (s_learnedNext + 1) % s_learned.size();
            }
            entry->form = form;
            entry->pose = pose;
        }

        [[nodiscard]] bool outranks(const PoseSource source, const float travel, const OpenPose& pose)
        {
            return source > pose.source || (source == pose.source && travel > pose.travel);
        }

        // The bolt where this frame has it, and each part that sits apart from its
        // closed pose as a part of the action does. Parts a dwell saw move stay out.
        void offerPose(
            PoseRead& read,
            const sampler::PoseFrame& frame,
            const ClipPose& pose,
            const float travel,
            const PoseSource source,
            const std::array<bool, kBoneCount>* stillParts)
        {
            auto& candidate = read.clipBest;
            if (!outranks(source, travel, candidate.pose)) {
                return;
            }
            candidate.pose = {};
            candidate.pose.source = source;
            candidate.pose.travel = travel;
            candidate.pose.posed[policy::kBoltBone] = true;
            candidate.pose.open[policy::kBoltBone] = *pose[policy::kBoltBone];
            for (std::size_t index = policy::kBoltBone + 1; index < kBoneCount; ++index) {
                if (!pose[index] || !read.published[index] || !read.closedTracked[index] ||
                    (stillParts && !(*stillParts)[index])) {
                    continue;
                }
                const auto moved = displacement(read.closed[index], *pose[index]);
                if (policy::partBelongsToPose(moved.moveGameUnits, moved.turnDegrees, travel)) {
                    candidate.pose.posed[index] = true;
                    candidate.pose.open[index] = *pose[index];
                }
            }
            candidate.seconds = frame.timeSeconds;
            candidate.clip = {};
            const std::string_view path{ frame.animationPath };
            const auto separator = path.find_last_of("/\\");
            const auto file = separator == std::string_view::npos ? path : path.substr(separator + 1);
            file.substr(0, candidate.clip.size() - 1).copy(candidate.clip.data(), candidate.clip.size() - 1);
        }

        void observeClipDwell(
            PoseRead& read,
            const sampler::PoseFrame& frame,
            const ClipPose& pose,
            const float travel)
        {
            auto& dwell = read.dwell;
            if (travel < policy::kMinimumOpenTravelGameUnits) {
                dwell.active = false;
                return;
            }
            const auto& bolt = *pose[policy::kBoltBone];
            if (!dwell.active ||
                bolt.translate.GetDistance(dwell.bolt.translate) > policy::kDwellBoltDriftGameUnits) {
                dwell = {};
                dwell.active = true;
                dwell.startSeconds = frame.timeSeconds;
                dwell.bolt = bolt;
                return;
            }
            const float parkedSeconds = frame.timeSeconds - dwell.startSeconds;
            if (parkedSeconds < policy::kDwellSettleSeconds) {
                return;
            }
            if (!dwell.settled) {
                dwell.settled = true;
                for (std::size_t index = 0; index < kBoneCount; ++index) {
                    dwell.still[index] = pose[index] != nullptr;
                    if (pose[index]) {
                        dwell.parked[index] = *pose[index];
                    }
                }
                return;
            }
            for (std::size_t index = 0; index < kBoneCount; ++index) {
                if (dwell.still[index]) {
                    const auto drift = displacement(dwell.parked[index], *pose[index]);
                    dwell.still[index] = drift.moveGameUnits <= policy::kPartStillGameUnits &&
                                         drift.turnDegrees <= policy::kPartStillDegrees;
                }
            }
            if (!dwell.committed && parkedSeconds >= policy::kDwellCommitSeconds) {
                dwell.committed = true;
                offerPose(read, frame, pose, travel, PoseSource::Dwell, &dwell.still);
            }
        }

        bool readClipPose(const sampler::PoseFrame& frame, void* userData) noexcept
        {
            auto& read = *static_cast<PoseRead*>(userData);
            if (frame.sampleIndex == 0) {
                read.clipKind = policy::classifyClip(frame.animationPath);
                read.dwell = {};
                read.clipShowsClosed = false;
                read.clipBest = {};
                ++read.clipsRead;
            }
            ClipPose pose{};
            for (std::uint32_t index = 0; index < frame.boneCount; ++index) {
                const auto& bone = frame.bones[index];
                if (bone.nameIndex < kBoneCount) {
                    pose[bone.nameIndex] = &bone.local;
                }
            }
            if (!pose[policy::kBoltBone]) {
                return false;
            }
            // One pose of the first idle clip that tracks the bolt is the whole answer.
            if (read.clipKind == policy::ClipKind::Idle) {
                if (!read.closedKnown) {
                    read.closedKnown = true;
                    for (std::size_t index = 0; index < kBoneCount; ++index) {
                        read.closedTracked[index] = pose[index] != nullptr;
                        if (pose[index]) {
                            read.closed[index] = *pose[index];
                        }
                    }
                }
                return false;
            }

            const float travel = pose[policy::kBoltBone]->translate.GetDistance(
                read.closed[policy::kBoltBone].translate);
            read.clipShowsClosed = read.clipShowsClosed || travel <= policy::kClipClosedGameUnits;
            if (travel >= policy::kMinimumOpenTravelGameUnits) {
                offerPose(
                    read,
                    frame,
                    pose,
                    travel,
                    read.clipKind == policy::ClipKind::Reload ? PoseSource::ReloadStroke : PoseSource::OtherStroke,
                    nullptr);
            }
            observeClipDwell(read, frame, pose, travel);

            if (frame.sampleIndex + 1 == frame.sampleCount && read.clipShowsClosed &&
                outranks(read.clipBest.pose.source, read.clipBest.pose.travel, read.best.pose)) {
                read.best = read.clipBest;
            }
            return true;
        }

        // Every reload variant is read: the farthest dwell wins. With none, the fire
        // clips show the stroke, and only a gun neither of them moves reads the rest.
        // Without a closed pose no clip can be judged, so the read ends with no pose.
        bool readsClip(const std::string_view animationPath, void* userData) noexcept
        {
            const auto& read = *static_cast<const PoseRead*>(userData);
            const auto source = read.best.pose.source;
            switch (policy::classifyClip(animationPath)) {
            case policy::ClipKind::Idle:
                return true;
            case policy::ClipKind::Reload:
                return read.closedKnown;
            case policy::ClipKind::Fire:
                return read.closedKnown && source != PoseSource::Dwell;
            default:
                return read.closedKnown && source == PoseSource::None;
            }
        }

        std::uint32_t clipOrder(const std::string_view animationPath) noexcept
        {
            return static_cast<std::uint32_t>(policy::classifyClip(animationPath));
        }

        void stopRead()
        {
            if (s_read.active) {
                sampler::reset();
                s_read.active = false;
            }
        }

        void logRead(const PoseRead& read, const Slot& slot)
        {
            std::array<char, 96> parts{};
            std::size_t used = 0;
            for (std::size_t index = policy::kBoltBone + 1; index < kBoneCount; ++index) {
                if (!read.best.pose.posed[index]) {
                    continue;
                }
                const auto moved = displacement(read.closed[index], read.best.pose.open[index]);
                const auto written = std::format_to_n(
                    parts.data() + used,
                    parts.size() - used - 1,
                    "E{}:{:.2f}gu/{:.1f}deg ",
                    index,
                    moved.moveGameUnits,
                    moved.turnDegrees);
                used = (std::min)(parts.size() - 1, used + static_cast<std::size_t>(written.size));
            }
            // How far the live bolt sits from the idle clip's: near zero says the clips
            // and the running graph agree about this gun. Only a holding gun tracks it.
            const float liveOffset = read.closedKnown && slot.eligible ?
                slot.bones[policy::kBoltBone].current.translate.GetDistance(read.closed[policy::kBoltBone].translate) :
                0.0f;
            PAPER_LOG_INFO(
                Reload,
                "BOLT_HOLD read clips weapon={:08X} clips={} ms={} longestStepUs={} closed={} liveOffset={:.2f}gu pose={} travel={:.2f}gu clip='{}' at={:.2f}s parts=[{}]",
                read.form,
                read.clipsRead,
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - read.started).count(),
                read.longestStepMicroseconds,
                read.closedKnown,
                liveOffset,
                poseName(read.best.pose.source),
                read.best.pose.travel,
                read.best.clip.data(),
                read.best.seconds,
                parts.data());
        }

        void beginRead(const std::uint32_t index, const Slot& slot)
        {
            s_read = {};
            s_read.active = true;
            s_read.slot = index;
            s_read.form = slot.form;
            s_read.generation = slot.generation;
            s_read.started = Clock::now();
            for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                s_read.published[bone] = slot.bones[bone].key != 0;
            }
        }

        void startReadIfNeeded(const FrameInput& input)
        {
            for (std::uint32_t index = 0; index < s_slots.size(); ++index) {
                const auto& slot = s_slots[index];
                const bool wanted = (input.enabled && slot.eligible) ||
                    (input.manualBoltEnabled && index == 0 && slot.boltAction &&
                        slot.bones[policy::kBoltBone].key != 0);
                if (wanted && !findLearned(slot.form) && slot.generation != s_readFailedGeneration) {
                    beginRead(index, slot);
                    return;
                }
            }
        }

        void failRead()
        {
            s_readFailedGeneration = s_read.generation;
            PAPER_LOG_WARN(
                Reload,
                "BOLT_HOLD could not read the clips of weapon={:08X}: the sampler failed; no hold until it is equipped again",
                s_read.form);
            stopRead();
        }

        void updateRead(const FrameInput& input)
        {
            if ((!input.enabled && !input.manualBoltEnabled) || !input.clipSamplerAvailable) {
                stopRead();
                return;
            }
            if (!s_read.active) {
                startReadIfNeeded(input);
                if (!s_read.active) {
                    return;
                }
            }

            const sampler::PoseReader reader{
                .samplesPerSecond = policy::kClipSamplesPerSecond,
                .clipOrder = &clipOrder,
                .readsClip = &readsClip,
                .pose = &readClipPose,
                .userData = &s_read,
            };
            const auto stepStarted = Clock::now();
            const auto result = sampler::stepPoses(
                s_read.form,
                s_read.generation,
                policy::kActionBoneNames.data(),
                static_cast<std::uint32_t>(kBoneCount),
                reader);
            s_read.longestStepMicroseconds = (std::max)(
                s_read.longestStepMicroseconds,
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - stepStarted).count());

            // Idle retries next frame: the actor's equipped items can trail ROCK's weapon.
            if (result.state == sampler::State::Completed) {
                s_read.best.pose.closed = s_read.closed;
                storeLearned(s_read.form, s_read.best.pose);
                logRead(s_read, s_slots[s_read.slot]);
                stopRead();
            } else if (result.state == sampler::State::Failed) {
                failRead();
            }
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
            if (s_read.active && s_read.slot == index) {
                stopRead();
            }
            slot = {};
        }

        // Finds the bones by name and decides the family. False tries again next
        // frame, for as long as the weapon stays equipped.
        [[nodiscard]] bool resolve(RockApiClient& rock, const std::uint32_t index, Slot& slot)
        {
            std::uint32_t total = 0;
            if (rock.copyWeaponSources(index, slot.generation, nullptr, 0, total) != rock::api::Status::Ok || !total) {
                return false;
            }
            s_sources.resize((std::min)(total, rock::api::weaponparts::kMaxSources));
            const auto status = rock.copyWeaponSources(
                index,
                slot.generation,
                s_sources.data(),
                static_cast<std::uint32_t>(s_sources.size()),
                total);
            if (status != rock::api::Status::Ok) {
                return false;
            }

            // An undecided size class (valid == 0) is no reason to wait: the keywords
            // still arrive, and only the family below is used.
            rock::api::weapon::Classification classification{};
            if (!rock.queryWeaponClassification(index, classification)) {
                return false;
            }
            s_evidence.resize(rock::api::weaponparts::kMaxEvidenceDetails);
            std::uint32_t evidenceTotal = 0;
            const auto evidenceCount = rock.copyWeaponEvidenceDetails(
                index,
                s_evidence.data(),
                static_cast<std::uint32_t>(s_evidence.size()),
                evidenceTotal);
            if (!evidenceCount && ++slot.evidenceWaitFrames < kEvidenceWaitFrames) {
                return false;
            }

            // A name on two nodes is ambiguous, and an ambiguous bone is not used.
            std::array<std::uint32_t, kBoneCount> matches{};
            for (const auto& source : s_sources) {
                for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                    if (std::string_view{ policy::kActionBoneNames[bone] } == source.name) {
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
            slot.boltAction = policy::familyIsBoltAction(family);
            PAPER_LOG_INFO(
                Reload,
                "BOLT_HOLD weapon={:08X}/{:016X} slot={} boltMatches={} parts={} evidence={} family={:#x} eligible={} boltAction={}",
                slot.form,
                slot.generation,
                index,
                matches[policy::kBoltBone],
                parts,
                evidenceCount,
                family,
                slot.eligible,
                slot.boltAction);
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

        void driveHold(RockApiClient& rock, const std::uint32_t index, Slot& slot, const OpenPose& pose)
        {
            const float blend = policy::easeHoldWeight(slot.weight);
            std::array<rock::api::weaponparts::DriveTarget, kBoneCount> drives{};
            std::uint32_t count = 0;
            for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
                if (!pose.posed[bone] || !slot.bones[bone].key) {
                    continue;
                }
                auto& drive = drives[count++];
                drive.flags = static_cast<std::uint32_t>(rock::api::weaponparts::TargetFlag::MatchSourceKey);
                drive.driveSpace = rock::api::weaponparts::DriveSpace::SourceParentLocal;
                drive.weaponGenerationKey = slot.generation;
                drive.sourceKey = slot.bones[bone].key;
                drive.leaseFrames = kDriveLeaseFrames;
                api_transform::fromNi(
                    transform_math::interpolateTransforms(slot.bones[bone].current, pose.open[bone], blend),
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

            const auto* learned = findLearned(slot.form);
            const bool hasPose = learned && learned->pose.source != PoseSource::None;
            const bool hold = input.enabled && hasPose && hasFlag(state.flags, WeaponStateFlag::AmmoKnown) &&
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
                    learned ? learned->pose.travel : 0.0f,
                    poseName(learned ? learned->pose.source : PoseSource::None));
            }
            if (weight <= 0.0f) {
                if (slot.weight > 0.0f) {
                    rock.clearWeaponDrives(index);
                }
                slot.weight = 0.0f;
                return;
            }
            slot.weight = weight;
            if (hasPose) {
                driveHold(rock, index, slot, learned->pose);
            }
        }
    }

    void update(RockApiClient& rock, const FrameInput& input)
    {
        for (std::uint32_t index = 0; index < s_slots.size(); ++index) {
            updateSlot(rock, index, input);
        }
        updateRead(input);
    }

    void reset(RockApiClient& rock)
    {
        for (std::uint32_t index = 0; index < s_slots.size(); ++index) {
            forgetWeapon(rock, index, s_slots[index]);
        }
        stopRead();
        s_readFailedGeneration = 0;
    }

    BoltFamily slotZeroFamily(const std::uint64_t generation)
    {
        const auto& slot = s_slots[0];
        if (slot.generation != generation || !slot.resolved) {
            return BoltFamily::Pending;
        }
        return slot.boltAction ? BoltFamily::BoltAction : BoltFamily::NotBoltAction;
    }

    bool queryManualAction(const std::uint64_t generation, ManualAction& out)
    {
        const auto& slot = s_slots[0];
        if (slot.generation != generation || !slot.resolved || !slot.boltAction ||
            !slot.bones[policy::kBoltBone].key) {
            return false;
        }
        const auto* learned = findLearned(slot.form);
        if (!learned || learned->pose.source == PoseSource::None) {
            return false;
        }
        for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
            out.keys[bone] = slot.bones[bone].key;
        }
        out.posed = learned->pose.posed;
        out.closed = learned->pose.closed;
        out.open = learned->pose.open;
        return true;
    }
}
