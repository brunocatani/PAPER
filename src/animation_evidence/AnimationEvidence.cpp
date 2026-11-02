#include "animation_evidence/AnimationEvidence.h"

#include "animation_evidence/AnimationPreharvestPolicy.h"
#include "animation_evidence/AnimationEvidencePolicy.h"
#include "animation_evidence/ClipTelemetry.h"
#include "animation_evidence/ExactClipPreharvest.h"
#include "PaperLog.h"
#include "reload_observation/ReloadObservation.h"
#include "support/AcquiredAnimationGraphManager.h"

#include "RE/Bethesda/PlayerCharacter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace paper::animation_evidence
{
    namespace
    {
        using namespace api;

        constexpr std::uint32_t kMaxSceneNames = 256;
        constexpr std::uint32_t kSceneWalkDepth = 32;
        constexpr std::uint32_t kPassiveWalkMaxAttempts = 900;
        constexpr std::uint32_t kPassiveRewalkIntervalFrames = 180;
        constexpr std::size_t kPassiveDrainCapacity = 8;

        template <class Enum>
        [[nodiscard]] constexpr std::uint32_t flag(const Enum value)
        {
            return static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] std::uint64_t nextSequence(std::uint64_t& value)
        {
            if (++value == 0) {
                ++value;
            }
            return value;
        }

        template <std::size_t N>
        [[nodiscard]] bool copyText(
            char (&destination)[N],
            const std::string_view source)
        {
            static_assert(N > 0);
            std::memset(destination, 0, N);
            const auto count = (std::min)(source.size(), N - 1);
            if (count > 0) {
                std::memcpy(destination, source.data(), count);
            }
            return source.size() >= N;
        }

        template <std::size_t N>
        [[nodiscard]] bool copyText(
            std::array<char, N>& destination,
            const std::string_view source)
        {
            destination.fill('\0');
            const auto count = (std::min)(source.size(), N - 1);
            if (count > 0) {
                std::memcpy(destination.data(), source.data(), count);
            }
            return source.size() >= N;
        }

        [[nodiscard]] std::string_view boundedText(
            const char* text,
            const std::size_t capacity)
        {
            if (!text || capacity == 0) {
                return {};
            }
            std::size_t length = 0;
            while (length < capacity && text[length] != '\0') {
                ++length;
            }
            return { text, length };
        }

        [[nodiscard]] ReloadQsTransform toApiTransform(
            const PoseSample& pose,
            const Vec3& scale)
        {
            ReloadQsTransform result{};
            result.translate[0] = pose.translate.x;
            result.translate[1] = pose.translate.y;
            result.translate[2] = pose.translate.z;
            result.rotate[0] = pose.rotate.x;
            result.rotate[1] = pose.rotate.y;
            result.rotate[2] = pose.rotate.z;
            result.rotate[3] = pose.rotate.w;
            result.scale[0] = scale.x;
            result.scale[1] = scale.y;
            result.scale[2] = scale.z;
            return result;
        }

        [[nodiscard]] std::string_view fileName(const std::string_view path)
        {
            const auto separator = path.find_last_of("/\\");
            return separator == std::string_view::npos ?
                path :
                path.substr(separator + 1);
        }

        struct TrackRecord
        {
            ReloadAnimationTrack value{};
            std::vector<ReloadQsTransform> samples;
        };

        struct ClipRecord
        {
            ReloadAnimationClip value{};
            std::vector<TrackRecord> tracks;
            std::vector<ReloadAnimationAnnotation> annotations;
            std::vector<ReloadAnimationTrigger> triggers;
        };

        struct NameSet
        {
            std::array<
                std::array<char, kReloadAnimationNameCapacity>,
                kMaxSceneNames>
                storage{};
            std::array<const char*, kMaxSceneNames> pointers{};
            std::uint32_t count{ 0 };
            bool truncated{ false };
        };

        struct Runtime
        {
            ReloadAnimationCatalogState catalog{};
            ReloadAnimationLiveState live{};
            std::vector<ClipRecord> clips;
            std::vector<ReloadAnimationSkeletonBone> skeleton;
            std::array<clip_telemetry::CapturePacket, kPassiveDrainCapacity>
                passivePackets{};
            std::uint64_t nextCatalogSequence{ 0 };
            std::uint64_t nextCatalogRevision{ 0 };
            std::uint32_t liveClipCount{ 0 };
            std::uint32_t exactClipCount{ 0 };
            std::uint32_t passiveWalkAttempts{ 0 };
            std::uint32_t passiveRewalkCooldown{ 0 };
            bool valid{ false };
            bool passiveWalkCompleted{ false };
            bool passiveWalkGaveUp{ false };
            bool passiveRewalkActive{ false };
            bool exactPreharvestDemandActive{ false };
            FrameDiagnostics frameDiagnostics{};
        };

        [[nodiscard]] Runtime& runtime()
        {
            static Runtime* instance = new Runtime();
            return *instance;
        }

        using DiagnosticsClock = std::chrono::steady_clock;

        [[nodiscard]] std::uint64_t elapsedMicroseconds(
            const DiagnosticsClock::time_point started)
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    DiagnosticsClock::now() - started)
                    .count());
        }

        void finishFrameDiagnostics(
            Runtime& state,
            const DiagnosticsClock::time_point frameStarted)
        {
            state.frameDiagnostics.totalMicroseconds =
                elapsedMicroseconds(frameStarted);
            state.frameDiagnostics.exact =
                exact_clip_preharvest::snapshotDiagnostics();
        }

        void markMutation(Runtime& state)
        {
            state.catalog.catalogRevision =
                nextSequence(state.nextCatalogRevision);
        }

        void clearCatalog(Runtime& state)
        {
            state.catalog = {};
            state.live = {};
            state.clips.clear();
            state.skeleton.clear();
            state.liveClipCount = 0;
            state.exactClipCount = 0;
            state.passiveWalkAttempts = 0;
            state.passiveRewalkCooldown = 0;
            state.valid = false;
            state.passiveWalkCompleted = false;
            state.passiveWalkGaveUp = false;
            state.passiveRewalkActive = false;
            state.exactPreharvestDemandActive = false;
            clip_telemetry::setCaptureEnabled(false);
            clip_telemetry::clearTargets();
            clip_telemetry::resetWalk();
            exact_clip_preharvest::reset();
        }

        void beginCatalog(
            Runtime& state,
            const std::uint32_t weaponFormId,
            const std::uint64_t weaponGenerationKey,
            const std::uint32_t paperProviderGeneration,
            const std::uint64_t reloadCatalogSequence,
            const rock::api::animation::AnimationPhaseContext& context)
        {
            clearCatalog(state);
            state.catalog.statusFlags =
                flag(ReloadAnimationCatalogFlag::Valid) |
                flag(ReloadAnimationCatalogFlag::PassiveCaptureActive);
            state.catalog.weaponFormId = weaponFormId;
            state.catalog.weaponGenerationKey = weaponGenerationKey;
            state.catalog.paperProviderGeneration = paperProviderGeneration;
            state.catalog.catalogSequence =
                nextSequence(state.nextCatalogSequence);
            state.catalog.reloadCatalogSequence = reloadCatalogSequence;
            state.catalog.worldGeneration = context.worldGeneration;
            state.catalog.skeletonGeneration = context.skeletonGeneration;
            state.catalog.rockProviderGeneration = context.providerGeneration;
            state.catalog.sampleStorageBudgetBytes =
                kReloadAnimationSampleBudgetBytes;
            state.catalog.updatedFrameIndex = context.frameIndex;
            state.valid = true;
            markMutation(state);
            clip_telemetry::setCaptureEnabled(true);
            PAPER_LOG_INFO(
                Animation,
                "Raw reload animation catalog started weapon={:08X}/{:016X} sequence={} sampleBudget={}MiB",
                weaponFormId,
                weaponGenerationKey,
                state.catalog.catalogSequence,
                state.catalog.sampleStorageBudgetBytes / (1024 * 1024));
        }

        [[nodiscard]] bool canAppendClip(
            Runtime& state,
            const ClipAcquisition acquisition)
        {
            const bool live = acquisition !=
                ClipAcquisition::ExactWeaponPreharvest;
            const bool sourceCapacityAvailable = live ?
                state.liveClipCount <
                    kMaxReloadLiveAnimationClips :
                state.exactClipCount <
                    kMaxReloadExactAnimationClips;
            if (sourceCapacityAvailable &&
                state.clips.size() < kMaxReloadAnimationClips) {
                return true;
            }
            ++state.catalog.omittedClipCount;
            state.catalog.statusFlags |= flag(
                ReloadAnimationCatalogFlag::ClipCapacityTruncated);
            markMutation(state);
            return false;
        }

        template <class SourceTrack>
        void appendSamples(
            Runtime& state,
            ClipRecord& clip,
            TrackRecord& track,
            const SourceTrack& source,
            const std::uint32_t requestedCount)
        {
            const auto remainingBytes =
                state.catalog.storedSampleBytes <
                        state.catalog.sampleStorageBudgetBytes ?
                    state.catalog.sampleStorageBudgetBytes -
                        state.catalog.storedSampleBytes :
                    0;
            const auto copyCount =
                animation_evidence_policy::boundedSampleCopyCount(
                    requestedCount,
                    remainingBytes,
                    sizeof(ReloadQsTransform));
            track.samples.reserve(copyCount);
            for (std::uint32_t sample = 0; sample < copyCount; ++sample) {
                track.samples.push_back(toApiTransform(
                    source.samples[sample],
                    source.scales[sample]));
            }
            track.value.sampleCount = copyCount;
            if (copyCount > 0) {
                track.value.flags |= flag(
                    ReloadAnimationTrackFlag::SamplesAvailable);
            }
            state.catalog.storedSampleBytes +=
                static_cast<std::uint64_t>(copyCount) *
                sizeof(ReloadQsTransform);
            if (copyCount < requestedCount) {
                clip.value.flags |= flag(
                    ReloadAnimationClipFlag::SamplesTruncated);
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        SampleStorageTruncated);
            }
        }

        void storeSkeleton(
            Runtime& state,
            const exact_clip_preharvest::ClipView& source)
        {
            if (!state.skeleton.empty() || !source.skeletonBones ||
                source.skeletonBoneCount == 0) {
                return;
            }
            const auto count = (std::min)(
                source.skeletonBoneCount,
                kMaxReloadSkeletonBones);
            std::vector<ReloadAnimationSkeletonBone> skeleton;
            skeleton.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto& sourceBone = source.skeletonBones[index];
                ReloadAnimationSkeletonBone bone{};
                bone.boneIndex = sourceBone.boneIndex;
                bone.parentBoneIndex = sourceBone.parentBoneIndex;
                bone.transformTrackIndex = sourceBone.transformTrackIndex;
                if ((sourceBone.flags & 1u) != 0) {
                    bone.flags |= flag(
                        ReloadAnimationTrackFlag::BoneNameValid);
                    if (copyText(
                            bone.boneName,
                            boundedText(
                                sourceBone.name.data(),
                                sourceBone.name.size()))) {
                        bone.flags |= flag(
                            ReloadAnimationTrackFlag::
                                BoneNameTruncated);
                    }
                }
                if (sourceBone.parentBoneIndex >= 0) {
                    bone.flags |= flag(
                        ReloadAnimationTrackFlag::
                            ParentBoneIndexValid);
                }
                if (sourceBone.transformTrackIndex >= 0) {
                    bone.flags |= flag(
                        ReloadAnimationTrackFlag::
                            TransformTrackIndexValid);
                }
                if ((sourceBone.flags & 2u) != 0) {
                    bone.flags |= flag(
                        ReloadAnimationTrackFlag::
                            ReferenceTransformValid);
                    bone.referenceLocal = toApiTransform(
                        sourceBone.referenceLocal,
                        sourceBone.referenceScale);
                }
                skeleton.push_back(bone);
            }
            state.skeleton = std::move(skeleton);
            state.catalog.skeletonBoneCount =
                static_cast<std::uint32_t>(state.skeleton.size());
            state.catalog.statusFlags |= flag(
                ReloadAnimationCatalogFlag::SkeletonAvailable);
        }

        void consumeExactClip(
            const exact_clip_preharvest::ClipView& source,
            void* userData) noexcept
        {
            auto* state = static_cast<Runtime*>(userData);
            if (!state || !state->valid ||
                source.weaponFormId != state->catalog.weaponFormId ||
                source.weaponGenerationKey !=
                    state->catalog.weaponGenerationKey ||
                !canAppendClip(
                    *state,
                    ClipAcquisition::ExactWeaponPreharvest)) {
                return;
            }
            const auto storedBytesBefore =
                state->catalog.storedSampleBytes;
            try {
                ClipRecord record{};
                record.value.clipId =
                    static_cast<std::uint32_t>(state->clips.size());
                record.value.acquisition =
                    ReloadAnimationAcquisition::
                        ExactWeaponPreharvest;
                record.value.trackSpace =
                    ReloadAnimationTrackSpace::WeaponRootLocal;
                const std::string_view path = source.animationPath ?
                    source.animationPath :
                    "";
                if (copyText(record.value.animationPath, path)) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::
                            AnimationPathTruncated);
                }
                if (copyText(record.value.animationName, fileName(path))) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::
                            AnimationNameTruncated);
                }
                record.value.durationSeconds = source.durationSeconds;
                record.value.animationType = source.animationType;
                record.value.rawTransformTrackCount =
                    source.rawTransformTrackCount > 0 ?
                        static_cast<std::uint32_t>(
                            source.rawTransformTrackCount) :
                        0;
                record.value.rawFloatTrackCount =
                    source.rawFloatTrackCount;
                record.value.sampleCount = source.sampleCount;
                record.value.trackCount = source.trackCount;
                if (source.targetTracksTruncated) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::TracksTruncated);
                }
                if (source.trackCount > 0) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::HasTargetTracks);
                }

                const auto trackCount = (std::min)(
                    source.trackCount,
                    kMaxReloadExactTracksPerClip);
                record.tracks.reserve(trackCount);
                for (std::uint32_t index = 0; index < trackCount; ++index) {
                    const auto& sourceTrack = source.tracks[index];
                    TrackRecord track{};
                    track.value.clipId = record.value.clipId;
                    track.value.trackId = index;
                    track.value.boneIndex = sourceTrack.boneIndex;
                    track.value.parentBoneIndex =
                        sourceTrack.parentBoneIndex;
                    track.value.transformTrackIndex =
                        sourceTrack.transformTrackIndex;
                    track.value.chainDepth = sourceTrack.chainDepth;
                    track.value.flags |= flag(
                        ReloadAnimationTrackFlag::BoneNameValid) |
                        flag(ReloadAnimationTrackFlag::
                            ReferenceTransformValid);
                    if (sourceTrack.boneIndex >= 0) {
                        track.value.flags |= flag(
                            ReloadAnimationTrackFlag::
                                BoneIndexValid);
                    }
                    if (sourceTrack.parentBoneIndex >= 0) {
                        track.value.flags |= flag(
                            ReloadAnimationTrackFlag::
                                ParentBoneIndexValid);
                    }
                    if (sourceTrack.transformTrackIndex >= 0) {
                        track.value.flags |= flag(
                            ReloadAnimationTrackFlag::
                                TransformTrackIndexValid);
                    }
                    if (copyText(
                            track.value.boneName,
                            boundedText(
                                sourceTrack.boneName.data(),
                                sourceTrack.boneName.size()))) {
                        track.value.flags |= flag(
                            ReloadAnimationTrackFlag::
                                BoneNameTruncated);
                    }
                    track.value.referenceLocal = toApiTransform(
                        sourceTrack.referenceLocal,
                        sourceTrack.referenceScale);
                    appendSamples(
                        *state,
                        record,
                        track,
                        sourceTrack,
                        (std::min)(
                            sourceTrack.sampleCount,
                            kMaxReloadExactSamplesPerTrack));
                    record.tracks.push_back(std::move(track));
                }
                record.value.trackCount =
                    static_cast<std::uint32_t>(record.tracks.size());
                storeSkeleton(*state, source);
                state->clips.push_back(std::move(record));
                ++state->exactClipCount;
                state->catalog.clipCount =
                    static_cast<std::uint32_t>(state->clips.size());
                state->catalog.exactClipCount = state->exactClipCount;
                markMutation(*state);
            } catch (...) {
                state->catalog.storedSampleBytes = storedBytesBefore;
                ++state->catalog.omittedClipCount;
                state->catalog.statusFlags |=
                    flag(ReloadAnimationCatalogFlag::
                        ClipCapacityTruncated) |
                    flag(ReloadAnimationCatalogFlag::
                        SampleStorageTruncated);
                markMutation(*state);
                PAPER_LOG_ERROR(
                    Animation,
                    "Raw exact clip evidence allocation failed; clip omitted");
            }
        }

        void appendPassiveClip(
            Runtime& state,
            const clip_telemetry::CapturePacket& source)
        {
            if (!state.valid ||
                source.weaponFormId != state.catalog.weaponFormId ||
                source.weaponGenerationKey !=
                    state.catalog.weaponGenerationKey ||
                !canAppendClip(state, source.acquisition)) {
                return;
            }
            const auto storedBytesBefore =
                state.catalog.storedSampleBytes;
            try {
                ClipRecord record{};
                record.value.clipId =
                    static_cast<std::uint32_t>(state.clips.size());
                record.value.acquisition =
                    static_cast<ReloadAnimationAcquisition>(
                        source.acquisition);
                record.value.trackSpace =
                    ReloadAnimationTrackSpace::RigBoneLocal;
                record.value.activityId = source.activityId;
                if (source.activityId != 0) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::HasActivityId);
                }
                if (copyText(
                        record.value.animationName,
                        boundedText(
                            source.animationName.data(),
                            source.animationName.size()))) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::
                            AnimationNameTruncated);
                }
                record.value.durationSeconds = source.durationSeconds;
                record.value.rawTransformTrackCount =
                    source.rawTransformTrackCount;
                record.value.sampleCount = kLiveClipSampleCount;
                record.value.rawAnnotationTrackCount =
                    source.rawAnnotationTrackCount;
                record.value.rawTriggerCount = source.rawTriggerCount;
                record.value.graphEventNameCount =
                    source.graphEventNameCount;
                if (source.weaponTracksTruncated) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::TracksTruncated);
                }
                if (source.annotationsTruncated) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::
                            AnnotationsTruncated);
                }
                if (source.triggersTruncated) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::TriggersTruncated);
                }

                const auto trackCount = (std::min)(
                    source.capturedWeaponTrackCount,
                    kMaxReloadLiveTracksPerClip);
                record.tracks.reserve(trackCount);
                for (std::uint32_t index = 0; index < trackCount; ++index) {
                    const auto& sourceTrack = source.weaponTracks[index];
                    TrackRecord track{};
                    track.value.clipId = record.value.clipId;
                    track.value.trackId = index;
                    track.value.flags |= flag(
                        ReloadAnimationTrackFlag::BoneNameValid);
                    if (copyText(
                            track.value.boneName,
                            boundedText(
                                sourceTrack.boneName.data(),
                                sourceTrack.boneName.size()))) {
                        track.value.flags |= flag(
                            ReloadAnimationTrackFlag::
                                BoneNameTruncated);
                    }
                    appendSamples(
                        state,
                        record,
                        track,
                        sourceTrack,
                        (std::min)(
                            sourceTrack.sampleCount,
                            kMaxReloadLiveSamplesPerTrack));
                    record.tracks.push_back(std::move(track));
                }
                record.value.trackCount =
                    static_cast<std::uint32_t>(record.tracks.size());
                if (!record.tracks.empty()) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::HasTargetTracks);
                }

                record.annotations.reserve(source.annotationCount);
                for (std::uint32_t index = 0;
                     index < source.annotationCount;
                     ++index) {
                    const auto& marker = source.annotations[index];
                    ReloadAnimationAnnotation annotation{};
                    annotation.clipId = record.value.clipId;
                    annotation.annotationId = index;
                    annotation.timeSeconds = marker.timeSeconds;
                    (void)copyText(
                        annotation.trackName,
                        boundedText(
                            marker.trackName.data(),
                            marker.trackName.size()));
                    (void)copyText(
                        annotation.text,
                        boundedText(marker.text.data(), marker.text.size()));
                    record.annotations.push_back(annotation);
                }
                record.triggers.reserve(source.triggerCount);
                for (std::uint32_t index = 0;
                     index < source.triggerCount;
                     ++index) {
                    const auto& marker = source.triggers[index];
                    ReloadAnimationTrigger trigger{};
                    trigger.clipId = record.value.clipId;
                    trigger.triggerId = index;
                    trigger.localTimeSeconds = marker.localTimeSeconds;
                    trigger.eventId = marker.eventId;
                    (void)copyText(
                        trigger.eventName,
                        boundedText(
                            marker.eventName.data(),
                            marker.eventName.size()));
                    record.triggers.push_back(trigger);
                }
                record.value.annotationCount =
                    static_cast<std::uint32_t>(record.annotations.size());
                record.value.triggerCount =
                    static_cast<std::uint32_t>(record.triggers.size());
                if (!record.annotations.empty()) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::HasAnnotations);
                }
                if (!record.triggers.empty()) {
                    record.value.flags |= flag(
                        ReloadAnimationClipFlag::HasTriggers);
                }

                state.clips.push_back(std::move(record));
                ++state.liveClipCount;
                state.catalog.clipCount =
                    static_cast<std::uint32_t>(state.clips.size());
                state.catalog.liveClipCount = state.liveClipCount;
                markMutation(state);
            } catch (...) {
                state.catalog.storedSampleBytes = storedBytesBefore;
                ++state.catalog.omittedClipCount;
                state.catalog.statusFlags |=
                    flag(ReloadAnimationCatalogFlag::
                        ClipCapacityTruncated) |
                    flag(ReloadAnimationCatalogFlag::
                        SampleStorageTruncated);
                markMutation(state);
            }
        }

        void drainPassive(Runtime& state)
        {
            const auto count = clip_telemetry::drainCaptures(
                state.passivePackets.data(),
                static_cast<std::uint32_t>(
                    state.passivePackets.size()));
            for (std::uint32_t index = 0; index < count; ++index) {
                appendPassiveClip(state, state.passivePackets[index]);
            }
            const auto dropped = clip_telemetry::drainDropInfo();
            if (dropped.count > 0 &&
                dropped.weaponFormId == state.catalog.weaponFormId &&
                dropped.weaponGenerationKey ==
                    state.catalog.weaponGenerationKey) {
                state.catalog.passiveCaptureDropCount += dropped.count;
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        PassiveCaptureDropped);
                markMutation(state);
            }
        }

        [[nodiscard]] bool hasGripFlag(
            const std::uint32_t flags,
            const rock::api::weapon::GripFlag value)
        {
            return (flags & static_cast<std::uint32_t>(value)) != 0;
        }

        [[nodiscard]] RE::NiNode* currentWeaponRoot(
            const paper::RockWeaponGripState* gripState)
        {
            if (!gripState ||
                !hasGripFlag(
                    gripState->flags,
                    rock::api::weapon::GripFlag::Valid) ||
                gripState->weaponFormId == 0 ||
                gripState->weaponGenerationKey == 0 ||
                gripState->weaponNode == 0) {
                return nullptr;
            }
            auto* object = reinterpret_cast<RE::NiAVObject*>(
                gripState->weaponNode);
            return object ? object->IsNode() : nullptr;
        }

        void appendName(NameSet& names, const char* rawName)
        {
            if (!rawName || rawName[0] == '\0') {
                return;
            }
            const auto normalized =
                animation_preharvest_policy::withoutSceneInstanceSuffix(
                    rawName);
            if (normalized.empty()) {
                return;
            }
            for (std::uint32_t index = 0; index < names.count; ++index) {
                if (animation_preharvest_policy::boneNameMatchesSceneNode(
                        normalized,
                        names.storage[index].data())) {
                    return;
                }
            }
            if (names.count >= names.storage.size()) {
                names.truncated = true;
                return;
            }
            auto& destination = names.storage[names.count];
            if (copyText(destination, normalized)) {
                names.truncated = true;
            }
            names.pointers[names.count] = destination.data();
            ++names.count;
        }

        void collectNames(
            NameSet& names,
            RE::NiAVObject* object,
            const std::uint32_t depth)
        {
            if (!object || depth > kSceneWalkDepth) {
                return;
            }
            appendName(names, object->name.c_str());
            auto* node = object->IsNode();
            if (!node) {
                return;
            }
            auto& children = node->GetRuntimeData().children;
            for (std::uint16_t index = 0; index < children.size(); ++index) {
                collectNames(names, children[index].get(), depth + 1);
            }
        }

        [[nodiscard]] NameSet collectWeaponNames(RE::NiNode* root)
        {
            NameSet result{};
            if (!root) {
                return result;
            }
            auto& children = root->GetRuntimeData().children;
            for (std::uint16_t index = 0; index < children.size(); ++index) {
                collectNames(result, children[index].get(), 0);
            }
            return result;
        }

        void appendCandidate(
            std::array<const void*, 5>& candidates,
            std::uint32_t& count,
            const void* manager)
        {
            if (!manager || count >= candidates.size()) {
                return;
            }
            for (std::uint32_t index = 0; index < count; ++index) {
                if (candidates[index] == manager) {
                    return;
                }
            }
            candidates[count++] = manager;
        }

        [[nodiscard]] bool passiveUpdateDue(Runtime& state)
        {
            if (state.passiveWalkGaveUp) {
                return false;
            }
            if (state.passiveWalkCompleted &&
                !state.passiveRewalkActive) {
                if (++state.passiveRewalkCooldown <
                    kPassiveRewalkIntervalFrames) {
                    return false;
                }
                state.passiveRewalkCooldown = 0;
                state.passiveRewalkActive = true;
                clip_telemetry::restartWalkPass();
            }
            return true;
        }

        void updatePassive(
            Runtime& state,
            RE::NiNode* weaponRoot,
            const NameSet& names)
        {
            if (!weaponRoot || names.count == 0 ||
                state.passiveWalkGaveUp) {
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }
            std::array<const void*, 5> candidates{};
            std::uint32_t candidateCount = 0;
            const auto firstWeaponSlot = static_cast<std::uint32_t>(
                std::to_underlying(RE::BIPED_OBJECT::kWeaponHand));
            const auto totalSlots = static_cast<std::uint32_t>(
                std::to_underlying(RE::BIPED_OBJECT::kTotal));
            for (const bool firstPerson : { true, false }) {
                auto* biped = player->GetBiped(firstPerson).get();
                if (!biped) {
                    continue;
                }
                for (std::uint32_t slot = firstWeaponSlot;
                     slot < totalSlots;
                     ++slot) {
                    auto& object = biped->object[slot];
                    const auto* form = object.parent.object;
                    if (!form || form->GetFormID() !=
                            state.catalog.weaponFormId ||
                        !object.objectGraphManager) {
                        continue;
                    }
                    appendCandidate(
                        candidates,
                        candidateCount,
                        clip_telemetry::managerFromWeaponHolder(
                            object.objectGraphManager.get()));
                }
            }
            native_animation_graph::AcquiredGraphManager actorManager{ player };
            appendCandidate(
                candidates,
                candidateCount,
                actorManager.get());
            bool catalogChanged =
                state.catalog.passiveCandidateManagerCount !=
                    candidateCount;
            state.catalog.passiveCandidateManagerCount = candidateCount;

            const void* chosen = nullptr;
            for (std::uint32_t index = 0;
                 index < candidateCount;
                 ++index) {
                if (clip_telemetry::probeBindings(candidates[index])) {
                    chosen = candidates[index];
                    break;
                }
            }
            catalogChanged = catalogChanged ||
                state.catalog.passiveBindingManagerAvailable !=
                    (chosen ? 1u : 0u);
            state.catalog.passiveBindingManagerAvailable =
                chosen ? 1u : 0u;
            if (!chosen) {
                const auto priorAttempts = state.passiveWalkAttempts;
                if (++state.passiveWalkAttempts >
                    kPassiveWalkMaxAttempts) {
                    state.passiveWalkGaveUp = true;
                    state.catalog.statusFlags |= flag(
                        ReloadAnimationCatalogFlag::
                            PassiveWalkUnavailable);
                }
                state.catalog.passiveWalkAttempts =
                    state.passiveWalkAttempts;
                if (catalogChanged ||
                    priorAttempts != state.passiveWalkAttempts) {
                    markMutation(state);
                }
                return;
            }

            const bool hooksInstalled =
                clip_telemetry::ensureHooksInstalled();
            if (hooksInstalled &&
                (state.catalog.statusFlags & flag(
                    ReloadAnimationCatalogFlag::
                        PassiveHooksInstalled)) == 0) {
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        PassiveHooksInstalled);
                catalogChanged = true;
            } else if (!hooksInstalled &&
                (state.catalog.statusFlags & flag(
                    ReloadAnimationCatalogFlag::
                        PassiveHookInstallFailed)) == 0) {
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        PassiveHookInstallFailed);
                catalogChanged = true;
            }
            const auto result = clip_telemetry::stepCapture(
                chosen,
                state.catalog.weaponFormId,
                state.catalog.weaponGenerationKey,
                names.pointers.data(),
                names.count);
            clip_telemetry::setTargets(
                candidates.data(),
                candidateCount,
                names.pointers.data(),
                names.count,
                state.catalog.weaponFormId,
                state.catalog.weaponGenerationKey);
            ++state.passiveWalkAttempts;
            state.catalog.passiveWalkAttempts =
                state.passiveWalkAttempts;
            catalogChanged = true;
            if (result == clip_telemetry::StepResult::Completed) {
                state.passiveWalkCompleted = true;
                state.passiveRewalkActive = false;
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        PassiveWalkCompleted);
            }
            if (catalogChanged) {
                markMutation(state);
            }
        }

        void refreshPassiveStats(Runtime& state)
        {
            const auto stats = clip_telemetry::snapshotStats();
            const bool changed =
                state.catalog.passiveBindingsSeen != stats.bindingsSeen ||
                state.catalog.passiveBindingsCaptured !=
                    stats.bindingsCaptured ||
                state.catalog.passiveBindingsWithoutTargets !=
                    stats.bindingsWithoutTargets ||
                state.catalog.passiveSkippedNonSpline !=
                    stats.skippedNonSpline ||
                state.catalog.passiveWalksCompleted !=
                    stats.walksCompleted ||
                state.catalog.passiveRejectedAnimationPointer !=
                    stats.rejectedAnimationPointer ||
                state.catalog.passiveRejectedClipParameters !=
                    stats.rejectedClipParameters ||
                state.catalog.passiveRejectedTrackMap !=
                    stats.rejectedTrackMap ||
                state.catalog.passiveRejectedBoneCount !=
                    stats.rejectedBoneCount ||
                state.catalog.passiveRejectedSampler !=
                    stats.rejectedSampler ||
                state.catalog.passiveRejectedSplineData !=
                    stats.rejectedSplineData ||
                state.catalog.passiveHookCalls != stats.hookCalls ||
                state.catalog.passiveHookMatches != stats.hookMatches;
            state.catalog.passiveBindingsSeen = stats.bindingsSeen;
            state.catalog.passiveBindingsCaptured =
                stats.bindingsCaptured;
            state.catalog.passiveBindingsWithoutTargets =
                stats.bindingsWithoutTargets;
            state.catalog.passiveSkippedNonSpline =
                stats.skippedNonSpline;
            state.catalog.passiveWalksCompleted = stats.walksCompleted;
            state.catalog.passiveRejectedAnimationPointer =
                stats.rejectedAnimationPointer;
            state.catalog.passiveRejectedClipParameters =
                stats.rejectedClipParameters;
            state.catalog.passiveRejectedTrackMap =
                stats.rejectedTrackMap;
            state.catalog.passiveRejectedBoneCount =
                stats.rejectedBoneCount;
            state.catalog.passiveRejectedSampler = stats.rejectedSampler;
            state.catalog.passiveRejectedSplineData =
                stats.rejectedSplineData;
            state.catalog.passiveHookCalls = stats.hookCalls;
            state.catalog.passiveHookMatches = stats.hookMatches;

            std::array<char, kReloadResolvePointCapacity>
                resolvePoint{};
            (void)copyText(
                resolvePoint,
                clip_telemetry::lastResolvePoint());
            const bool resolvePointChanged = std::memcmp(
                resolvePoint.data(),
                state.catalog.passiveLastResolvePoint,
                resolvePoint.size()) != 0;
            std::memcpy(
                state.catalog.passiveLastResolvePoint,
                resolvePoint.data(),
                resolvePoint.size());
            if (changed || resolvePointChanged) {
                markMutation(state);
            }
        }

        [[nodiscard]] ReloadAnimationPreharvestState toApiState(
            const exact_clip_preharvest::State state)
        {
            return static_cast<ReloadAnimationPreharvestState>(state);
        }

        void updateExact(
            Runtime& state,
            const NameSet& names)
        {
            const auto result = exact_clip_preharvest::step(
                state.catalog.weaponFormId,
                state.catalog.weaponGenerationKey,
                names.pointers.data(),
                names.count,
                &consumeExactClip,
                &state);
            const auto previousState =
                state.catalog.exactPreharvestState;
            state.catalog.exactPreharvestState =
                toApiState(result.state);
            if (result.state == exact_clip_preharvest::State::Completed) {
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        ExactPreharvestCompleted);
            } else if (
                result.state == exact_clip_preharvest::State::Failed) {
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        ExactPreharvestFailed);
            }
            const auto stats = exact_clip_preharvest::snapshotStats();
            if (stats.usedLoadedGraphPathFallback) {
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        ExactLoadedGraphPathFallback);
            }
            const bool changed = previousState !=
                    state.catalog.exactPreharvestState ||
                state.catalog.exactAnimationFileCount !=
                    stats.animationFileCount ||
                state.catalog.exactClipsSampled != stats.clipsSampled ||
                state.catalog.exactClipsRejected != stats.clipsRejected ||
                state.catalog.exactTargetTruncationCount !=
                    stats.targetBonesTruncated;
            state.catalog.exactAnimationFileCount =
                stats.animationFileCount;
            state.catalog.exactClipsSampled = stats.clipsSampled;
            state.catalog.exactClipsRejected = stats.clipsRejected;
            state.catalog.exactTargetTruncationCount =
                stats.targetBonesTruncated;
            if (changed) {
                markMutation(state);
            }
        }

        void updateLiveState(
            Runtime& state,
            const std::uint64_t frameIndex)
        {
            state.live = {};
            if (!state.valid) {
                return;
            }
            state.live.catalogSequence =
                state.catalog.catalogSequence;
            state.live.frameIndex = frameIndex;
            const auto source = clip_telemetry::activityState();
            if (!source.active ||
                source.weaponFormId != state.catalog.weaponFormId ||
                source.weaponGenerationKey !=
                    state.catalog.weaponGenerationKey) {
                return;
            }
            state.live.flags = flag(
                ReloadAnimationLiveFlag::Active);
            state.live.concurrentActivityCount =
                source.concurrentActivityCount;
            state.live.activityId = source.activityId;
            state.live.weaponFormId = source.weaponFormId;
            state.live.weaponGenerationKey =
                source.weaponGenerationKey;
            if (!boundedText(
                    source.animationName.data(),
                    source.animationName.size()).empty()) {
                state.live.flags |= flag(
                    ReloadAnimationLiveFlag::
                        AnimationNameValid);
                (void)copyText(
                    state.live.animationName,
                    boundedText(
                        source.animationName.data(),
                        source.animationName.size()));
            }
            state.live.durationSeconds = source.durationSeconds;
            state.live.cropStartSeconds = source.cropStartSeconds;
            state.live.croppedDurationSeconds =
                source.croppedDurationSeconds;
            state.live.localTimeSeconds = source.localTimeSeconds;
            state.live.fraction = source.fraction;
        }
    }

    using namespace api;

    void reset()
    {
        auto& state = runtime();
        clearCatalog(state);
        state.frameDiagnostics = {};
    }

    void advanceFrame(
        const rock::api::animation::AnimationPhaseContext& context,
        const paper::RockWeaponGripState* gripState,
        const std::uint32_t paperProviderGeneration,
        const bool exactPreharvestDemand)
    {
        auto& state = runtime();
        state.frameDiagnostics = {};
        const auto frameStarted = DiagnosticsClock::now();
        auto stageStarted = frameStarted;
        auto* root = currentWeaponRoot(gripState);
        if (!root || !gripState) {
            if (state.valid) {
                clearCatalog(state);
            }
            state.frameDiagnostics.catalogSetupMicroseconds =
                elapsedMicroseconds(stageStarted);
            finishFrameDiagnostics(state, frameStarted);
            return;
        }

        ReloadCatalogState observationCatalog{};
        const bool observationReady =
            reload_observation::getCatalogState(observationCatalog) ==
                Status::Ok &&
            observationCatalog.weaponFormId == gripState->weaponFormId &&
            observationCatalog.weaponGenerationKey ==
                gripState->weaponGenerationKey &&
            observationCatalog.paperProviderGeneration ==
                paperProviderGeneration;
        if (!observationReady) {
            if (state.valid &&
                (state.catalog.weaponFormId != gripState->weaponFormId ||
                    state.catalog.weaponGenerationKey !=
                        gripState->weaponGenerationKey)) {
                clearCatalog(state);
            }
            state.frameDiagnostics.catalogSetupMicroseconds =
                elapsedMicroseconds(stageStarted);
            finishFrameDiagnostics(state, frameStarted);
            return;
        }

        if (state.valid && state.exactPreharvestDemandActive &&
            !exactPreharvestDemand) {
            // Exact acquisition owns an off-screen graph and clip resource while
            // in flight. Restart the passive catalog when that demand disappears
            // so ownership is released immediately and a later exact request
            // cannot append a second copy of partially harvested clips.
            clearCatalog(state);
        }

        if (!state.valid ||
            state.catalog.weaponFormId != gripState->weaponFormId ||
            state.catalog.weaponGenerationKey !=
                gripState->weaponGenerationKey ||
            state.catalog.paperProviderGeneration !=
                paperProviderGeneration ||
            state.catalog.reloadCatalogSequence !=
                observationCatalog.catalogSequence ||
            state.catalog.worldGeneration != context.worldGeneration ||
            state.catalog.skeletonGeneration !=
                context.skeletonGeneration ||
            state.catalog.rockProviderGeneration !=
                context.providerGeneration) {
            beginCatalog(
                state,
                gripState->weaponFormId,
                gripState->weaponGenerationKey,
                paperProviderGeneration,
                observationCatalog.catalogSequence,
                context);
        }
        state.exactPreharvestDemandActive = exactPreharvestDemand;

        state.frameDiagnostics.catalogSetupMicroseconds =
            elapsedMicroseconds(stageStarted);
        stageStarted = DiagnosticsClock::now();
        drainPassive(state);
        state.frameDiagnostics.passiveDrainMicroseconds +=
            elapsedMicroseconds(stageStarted);
        const bool passiveDue = passiveUpdateDue(state);
        if (passiveDue || exactPreharvestDemand) {
            stageStarted = DiagnosticsClock::now();
            const auto names = collectWeaponNames(root);
            state.frameDiagnostics.nameCollectionMicroseconds =
                elapsedMicroseconds(stageStarted);
            if (names.truncated) {
                state.catalog.statusFlags |= flag(
                    ReloadAnimationCatalogFlag::
                        SceneNameCapacityTruncated);
            }
            if (names.count > 0) {
                if (passiveDue) {
                    stageStarted = DiagnosticsClock::now();
                    updatePassive(state, root, names);
                    state.frameDiagnostics.passiveUpdateMicroseconds =
                        elapsedMicroseconds(stageStarted);
                }
                if (exactPreharvestDemand) {
                    stageStarted = DiagnosticsClock::now();
                    updateExact(state, names);
                    state.frameDiagnostics.exactUpdateMicroseconds =
                        elapsedMicroseconds(stageStarted);
                }
            }
        }
        stageStarted = DiagnosticsClock::now();
        drainPassive(state);
        state.frameDiagnostics.passiveDrainMicroseconds +=
            elapsedMicroseconds(stageStarted);
        stageStarted = DiagnosticsClock::now();
        refreshPassiveStats(state);
        state.frameDiagnostics.passiveStatsMicroseconds =
            elapsedMicroseconds(stageStarted);
        state.catalog.updatedFrameIndex = context.frameIndex;
        finishFrameDiagnostics(state, frameStarted);
    }

    void completeFrame(
        const rock::api::animation::AnimationPhaseContext& context)
    {
        auto& state = runtime();
        if (!state.valid) {
            return;
        }
        drainPassive(state);
        refreshPassiveStats(state);
        updateLiveState(state, context.frameIndex);
        state.catalog.updatedFrameIndex = context.frameIndex;
    }

    FrameDiagnostics snapshotFrameDiagnostics()
    {
        return runtime().frameDiagnostics;
    }

    Status getLimits(ReloadAnimationLimits& outLimits)
    {
        outLimits = {};
        outLimits.maxClips = kMaxReloadAnimationClips;
        outLimits.maxLiveClips =
            kMaxReloadLiveAnimationClips;
        outLimits.maxExactClips =
            kMaxReloadExactAnimationClips;
        outLimits.maxLiveTracksPerClip =
            kMaxReloadLiveTracksPerClip;
        outLimits.maxExactTracksPerClip =
            kMaxReloadExactTracksPerClip;
        outLimits.maxLiveSamplesPerTrack =
            kMaxReloadLiveSamplesPerTrack;
        outLimits.maxExactSamplesPerTrack =
            kMaxReloadExactSamplesPerTrack;
        outLimits.maxSkeletonBones =
            kMaxReloadSkeletonBones;
        outLimits.maxAnnotationsPerClip =
            kMaxReloadClipAnnotations;
        outLimits.maxTriggersPerClip =
            kMaxReloadClipTriggers;
        outLimits.animationNameCapacity =
            kReloadAnimationNameCapacity;
        outLimits.animationPathCapacity =
            kReloadAnimationPathCapacity;
        outLimits.markerTextCapacity =
            kReloadMarkerTextCapacity;
        outLimits.resolvePointCapacity =
            kReloadResolvePointCapacity;
        outLimits.sampleStorageBudgetBytes =
            kReloadAnimationSampleBudgetBytes;
        return Status::Ok;
    }

    Status getCatalogState(
        ReloadAnimationCatalogState& outState)
    {
        const auto& state = runtime();
        if (!state.valid) {
            outState = {};
            return Status::NotReady;
        }
        outState = state.catalog;
        return Status::Ok;
    }

    Status getLiveState(
        ReloadAnimationLiveState& outState)
    {
        const auto& state = runtime();
        if (!state.valid) {
            outState = {};
            return Status::NotReady;
        }
        outState = state.live;
        return Status::Ok;
    }

    namespace
    {
        [[nodiscard]] Status validateSequence(
            const Runtime& state,
            const std::uint64_t catalogSequence)
        {
            if (!state.valid) {
                return Status::NotReady;
            }
            return catalogSequence == state.catalog.catalogSequence ?
                Status::Ok :
                Status::StaleSnapshot;
        }

        template <class Value, class Source>
        [[nodiscard]] Status copyValues(
            const std::vector<Source>& source,
            const std::uint32_t first,
            Value* output,
            const std::uint32_t maximum,
            std::uint32_t& copied)
        {
            copied = 0;
            if (first > source.size()) {
                return Status::OutOfRange;
            }
            if (maximum == 0) {
                return Status::Ok;
            }
            if (!output) {
                return Status::InvalidArgument;
            }
            copied = (std::min)(
                maximum,
                static_cast<std::uint32_t>(source.size() - first));
            for (std::uint32_t index = 0; index < copied; ++index) {
                if constexpr (std::is_same_v<Value, Source>) {
                    output[index] = source[first + index];
                } else {
                    output[index] = source[first + index].value;
                }
            }
            return Status::Ok;
        }
    }

    Status copyClips(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstClip,
        ReloadAnimationClip* outClips,
        const std::uint32_t maxClips,
        std::uint32_t& outCopied)
    {
        const auto& state = runtime();
        const auto valid = validateSequence(state, catalogSequence);
        if (valid != Status::Ok) {
            outCopied = 0;
            return valid;
        }
        return copyValues(
            state.clips,
            firstClip,
            outClips,
            maxClips,
            outCopied);
    }

    Status copyTracks(
        const std::uint64_t catalogSequence,
        const std::uint32_t clipId,
        const std::uint32_t firstTrack,
        ReloadAnimationTrack* outTracks,
        const std::uint32_t maxTracks,
        std::uint32_t& outCopied)
    {
        const auto& state = runtime();
        const auto valid = validateSequence(state, catalogSequence);
        if (valid != Status::Ok) {
            outCopied = 0;
            return valid;
        }
        if (clipId >= state.clips.size()) {
            outCopied = 0;
            return Status::NotFound;
        }
        return copyValues(
            state.clips[clipId].tracks,
            firstTrack,
            outTracks,
            maxTracks,
            outCopied);
    }

    Status copySamples(
        const std::uint64_t catalogSequence,
        const std::uint32_t clipId,
        const std::uint32_t trackId,
        const std::uint32_t firstSample,
        ReloadAnimationSample* outSamples,
        const std::uint32_t maxSamples,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        const auto& state = runtime();
        const auto valid = validateSequence(state, catalogSequence);
        if (valid != Status::Ok) {
            return valid;
        }
        if (clipId >= state.clips.size() ||
            trackId >= state.clips[clipId].tracks.size()) {
            return Status::NotFound;
        }
        const auto& clip = state.clips[clipId];
        const auto& samples = clip.tracks[trackId].samples;
        if (firstSample > samples.size()) {
            return Status::OutOfRange;
        }
        if (maxSamples == 0) {
            return Status::Ok;
        }
        if (!outSamples) {
            return Status::InvalidArgument;
        }
        outCopied = (std::min)(
            maxSamples,
            static_cast<std::uint32_t>(samples.size() - firstSample));
        for (std::uint32_t index = 0; index < outCopied; ++index) {
            const auto sampleIndex = firstSample + index;
            auto& output = outSamples[index];
            output = {};
            output.clipId = clipId;
            output.trackId = trackId;
            output.sampleIndex = sampleIndex;
            output.transform = samples[sampleIndex];
            output.timeSeconds = animation_evidence_policy::sampleTime(
                clip.value.durationSeconds,
                sampleIndex,
                clip.value.sampleCount);
        }
        return Status::Ok;
    }

    Status copyAnnotations(
        const std::uint64_t catalogSequence,
        const std::uint32_t clipId,
        const std::uint32_t firstAnnotation,
        ReloadAnimationAnnotation* outAnnotations,
        const std::uint32_t maxAnnotations,
        std::uint32_t& outCopied)
    {
        const auto& state = runtime();
        const auto valid = validateSequence(state, catalogSequence);
        if (valid != Status::Ok) {
            outCopied = 0;
            return valid;
        }
        if (clipId >= state.clips.size()) {
            outCopied = 0;
            return Status::NotFound;
        }
        return copyValues(
            state.clips[clipId].annotations,
            firstAnnotation,
            outAnnotations,
            maxAnnotations,
            outCopied);
    }

    Status copyTriggers(
        const std::uint64_t catalogSequence,
        const std::uint32_t clipId,
        const std::uint32_t firstTrigger,
        ReloadAnimationTrigger* outTriggers,
        const std::uint32_t maxTriggers,
        std::uint32_t& outCopied)
    {
        const auto& state = runtime();
        const auto valid = validateSequence(state, catalogSequence);
        if (valid != Status::Ok) {
            outCopied = 0;
            return valid;
        }
        if (clipId >= state.clips.size()) {
            outCopied = 0;
            return Status::NotFound;
        }
        return copyValues(
            state.clips[clipId].triggers,
            firstTrigger,
            outTriggers,
            maxTriggers,
            outCopied);
    }

    Status copySkeleton(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstBone,
        ReloadAnimationSkeletonBone* outBones,
        const std::uint32_t maxBones,
        std::uint32_t& outCopied)
    {
        const auto& state = runtime();
        const auto valid = validateSequence(state, catalogSequence);
        if (valid != Status::Ok) {
            outCopied = 0;
            return valid;
        }
        return copyValues(
            state.skeleton,
            firstBone,
            outBones,
            maxBones,
            outCopied);
    }
}
