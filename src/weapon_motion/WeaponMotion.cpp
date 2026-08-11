#include "weapon_motion/WeaponMotion.h"

#include "PaperConfig.h"
#include "PaperLog.h"
#include "animation_evidence/AnimationEvidence.h"
#include "api/RockApiClient.h"
#include "reload_observation/ReloadObservation.h"
#include "weapon_motion/WeaponMotionCache.h"
#include "weapon_motion/WeaponMotionPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace paper::weapon_motion
{
    namespace
    {
        using namespace api;
        using namespace weapon_motion_policy;

        constexpr std::uint32_t kInvalidId = 0xFFFF'FFFFu;
        constexpr std::uint32_t kInvalidBodyId = 0x7FFF'FFFFu;
        constexpr float kRestProgressThreshold = 0.05f;
        constexpr float kMaximumProgressThreshold = 0.95f;
        constexpr float kReturnToRestTolerance = 0.60f;
        constexpr float kRigidFollowerTranslationTolerance = 0.15f;
        constexpr float kRigidFollowerRotationToleranceRadians = 0.02f;
        constexpr float kRigidFollowerScaleTolerance = 0.01f;
        constexpr float kFollowerMotionOverlapMinimum = 0.35f;

        template <class Enum>
        [[nodiscard]] constexpr std::uint32_t flag(const Enum value)
        {
            return static_cast<std::uint32_t>(value);
        }

        template <std::size_t Capacity>
        [[nodiscard]] std::string_view boundedText(
            const char (&value)[Capacity])
        {
            const auto* end = static_cast<const char*>(
                std::memchr(value, '\0', Capacity));
            return std::string_view{
                value,
                end ? static_cast<std::size_t>(end - value) : Capacity
            };
        }

        template <std::size_t Capacity>
        [[nodiscard]] std::string_view boundedText(
            const std::array<char, Capacity>& value)
        {
            const auto* end = static_cast<const char*>(
                std::memchr(value.data(), '\0', Capacity));
            return std::string_view{
                value.data(),
                end ? static_cast<std::size_t>(end - value.data()) : Capacity
            };
        }

        template <std::size_t Capacity>
        void copyText(char (&destination)[Capacity], const std::string_view source)
        {
            const auto count = (std::min)(Capacity - 1, source.size());
            std::memcpy(destination, source.data(), count);
            destination[count] = '\0';
        }

        template <std::size_t Capacity>
        void copyText(
            std::array<char, Capacity>& destination,
            const std::string_view source)
        {
            const auto count = (std::min)(Capacity - 1, source.size());
            std::memcpy(destination.data(), source.data(), count);
            destination[count] = '\0';
        }

        struct FollowerRecord
        {
            PaperWeaponMotionFollowerV1 value{};
            std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1> keys{};
        };

        struct StageRecord
        {
            PaperWeaponMotionStageV1 value{};
            std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1> keys{};
            std::array<
                FollowerRecord,
                PAPER_MAX_WEAPON_MOTION_FOLLOWERS_V1> followers{};
            std::uint32_t followerCount{ 0 };
        };

        struct RecorderRecord
        {
            PaperWeaponMotionRecorderV1 value{};
            std::array<
                PaperReloadQsTransformV1,
                kMaximumStrokeSamples> samples{};
            PaperReloadQsTransformV1 previous{};
            std::uint32_t recordingCount{ 0 };
            bool previousValid{ false };
        };

        struct ExactTrackCandidate
        {
            bool valid{ false };
            std::uint32_t partId{ kInvalidId };
            std::uint32_t sourceClipId{ kInvalidId };
            std::uint32_t sourceBoundary{ 0 };
            std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1> primary{};
            std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1> returning{};
            std::array<
                PaperReloadQsTransformV1,
                PAPER_MAX_RELOAD_EXACT_SAMPLES_PER_TRACK_V1> sourcePoses{};
            std::array<
                float,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1> primarySourcePositions{};
            std::array<
                float,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1> returnSourcePositions{};
            std::uint32_t sampleCount{ 0 };
            float primaryLength{ 0.0f };
            float returnLength{ 0.0f };
            float peakDelta{ 0.0f };
        };

        struct ExactCursor
        {
            std::array<
                PaperReloadAnimationClipV1,
                PAPER_MAX_RELOAD_ANIMATION_CLIPS_V1> clips{};
            std::array<
                PaperReloadAnimationTrackV1,
                PAPER_MAX_RELOAD_EXACT_TRACKS_PER_CLIP_V1> tracks{};
            std::array<
                PaperReloadAnimationSampleV1,
                PAPER_MAX_RELOAD_EXACT_SAMPLES_PER_TRACK_V1> samples{};
            std::array<
                PaperReloadQsTransformV1,
                PAPER_MAX_RELOAD_EXACT_SAMPLES_PER_TRACK_V1> poses{};
            std::array<
                ExactTrackCandidate,
                PAPER_MAX_RELOAD_EXACT_TRACKS_PER_CLIP_V1> candidates{};
            std::uint64_t catalogSequence{ 0 };
            std::uint64_t catalogRevision{ 0 };
            std::uint32_t clipCount{ 0 };
            std::uint32_t nextClip{ 0 };
            std::uint32_t currentClipIndex{ kInvalidId };
            std::uint32_t trackCount{ 0 };
            std::uint32_t nextTrack{ 0 };
            std::uint32_t candidateCount{ 0 };
            std::uint32_t nextCandidateToFinalize{ 0 };
            std::uint32_t processedExactClipCount{ 0 };
            bool finalizing{ false };
            bool preharvestCompleted{ false };
            bool cacheEligible{ false };
        };

        struct HandRuntime
        {
            PaperWeaponManipulationHandStateV1 value{};
            bool active{ false };
            bool atMaximum{ false };
            bool atRest{ false };
        };

        enum class CacheLookupState : std::uint32_t
        {
            Uninitialized = 0,
            Pending = 1,
            HitSession = 2,
            HitPersistent = 3,
            Miss = 4,
            Bypass = 5,
            Compiled = 6,
            ExactUnavailable = 7,
        };

        struct Runtime
        {
            PaperWeaponMotionCatalogStateV1 catalog{};
            std::array<
                PaperWeaponMotionPartV1,
                PAPER_MAX_WEAPON_MOTION_PARTS_V1> parts{};
            std::array<
                std::array<char, PAPER_RELOAD_NODE_NAME_CAPACITY_V1>,
                PAPER_MAX_WEAPON_MOTION_PARTS_V1> partNodeNames{};
            std::array<
                std::array<char, PAPER_RELOAD_NODE_PATH_CAPACITY_V1>,
                PAPER_MAX_WEAPON_MOTION_PARTS_V1> partNodePaths{};
            std::array<
                PaperReloadNodeCatalogEntryV1,
                PAPER_MAX_RELOAD_CATALOG_NODES_V1> catalogNodes{};
            std::array<
                StageRecord,
                PAPER_MAX_WEAPON_MOTION_STAGES_V1> stages{};
            PaperWeaponMotionLearningStateV1 learning{};
            std::array<
                RecorderRecord,
                PAPER_MAX_WEAPON_MOTION_RECORDERS_V1> recorders{};
            PaperWeaponManipulationFrameStateV1 manipulation{};
            std::array<HandRuntime, 2> hands{};
            PaperWeaponMotionStoreStateV1 store{};
            ExactCursor exact{};
            std::array<
                PaperEventV1,
                PAPER_MAX_WEAPON_MOTION_EVENTS_V1> events{};
            std::uint32_t partCount{ 0 };
            std::uint32_t stageCount{ 0 };
            std::uint32_t recorderCount{ 0 };
            std::uint32_t eventCount{ 0 };
            std::uint64_t eventSequence{ 0 };
            PaperFormIdentityV1 weaponIdentity{};
            PaperWeaponClassificationV1 weaponClassification{};
            std::uint64_t cacheLoadoutKey{ 0 };
            std::uint64_t lastQueuedAuthoredRevision{ 0 };
            CacheLookupState cacheLookupState{ CacheLookupState::Uninitialized };
            RuntimeOptions options{};
        };

        std::unique_ptr<Runtime> s_runtime{};
        std::unique_ptr<weapon_motion_cache::Store> s_cacheStore{};
        std::optional<weapon_motion_cache::Settings> s_cacheSettings{};
        std::uint64_t s_nextCatalogSequence{ 0 };
        std::uint64_t s_nextLearningSnapshotSequence{ 0 };
        std::uint64_t s_nextManipulationSnapshotSequence{ 0 };

        [[nodiscard]] bool hasFlag(
            const std::uint32_t flags,
            const std::uint32_t requested)
        {
            return (flags & requested) != 0;
        }

        [[nodiscard]] std::uint64_t nextSequence(std::uint64_t& sequence)
        {
            if (++sequence == 0) {
                ++sequence;
            }
            return sequence;
        }

        void queueEvent(
            Runtime& state,
            const PaperEventKindV1 kind,
            const std::uint32_t partId = kInvalidId,
            const PaperHandV1 hand = PaperHandV1::Right,
            const std::uint32_t motionFlags = 0,
            const float normalizedProgress = 0.0f)
        {
            if (state.eventCount >= state.events.size()) {
                return;
            }
            auto& event = state.events[state.eventCount++];
            event = {};
            event.kind = kind;
            event.relatedSnapshotSequence =
                state.manipulation.snapshotSequence != 0 ?
                    state.manipulation.snapshotSequence :
                    state.learning.snapshotSequence;
            event.relatedCatalogSequence = state.catalog.catalogSequence;
            event.relatedPartId = partId;
            event.relatedHand = hand;
            event.relatedMotionFlags = motionFlags;
            event.relatedNormalizedProgress = normalizedProgress;
            state.manipulation.eventSequence = nextSequence(state.eventSequence);
        }

        void publishCatalogChanged(Runtime& state, const std::uint32_t partId)
        {
            state.catalog.catalogSequence =
                nextSequence(s_nextCatalogSequence);
            for (std::uint32_t index = 0; index < state.eventCount; ++index) {
                auto& event = state.events[index];
                if (event.kind !=
                    PaperEventKindV1::WeaponMotionCatalogChanged) {
                    continue;
                }
                event.relatedCatalogSequence = state.catalog.catalogSequence;
                if (event.relatedPartId != partId) {
                    event.relatedPartId = kInvalidId;
                }
                return;
            }
            queueEvent(
                state,
                PaperEventKindV1::WeaponMotionCatalogChanged,
                partId);
        }

        [[nodiscard]] PaperWeaponMotionPartV1* findPart(
            Runtime& state,
            const std::uint32_t partId)
        {
            return partId < state.partCount ?
                std::addressof(state.parts[partId]) :
                nullptr;
        }

        [[nodiscard]] const PaperWeaponMotionPartV1* findPart(
            const Runtime& state,
            const std::uint32_t partId)
        {
            return partId < state.partCount ?
                std::addressof(state.parts[partId]) :
                nullptr;
        }

        [[nodiscard]] std::uint32_t findPartByName(
            const Runtime& state,
            const std::string_view name)
        {
            for (std::uint32_t index = 0; index < state.partCount; ++index) {
                if (namesMatch(name, boundedText(state.parts[index].sourceName)) ||
                    namesMatch(
                        name,
                        boundedText(state.partNodeNames[index]))) {
                    return index;
                }
            }
            return kInvalidId;
        }

        [[nodiscard]] bool textEqualInsensitive(
            const std::string_view left,
            const std::string_view right)
        {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index) {
                if (asciiLower(left[index]) != asciiLower(right[index])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] weapon_motion_cache::StablePartKey stablePartKey(
            const Runtime& state,
            const std::uint32_t partId)
        {
            if (partId >= state.partCount) {
                return {};
            }
            const auto& part = state.parts[partId];
            return {
                .sourceName = std::string(boundedText(part.sourceName)),
                .nodePath = std::string(boundedText(state.partNodePaths[partId])),
                .omodPluginName = std::string(
                    boundedText(part.omod.pluginName)),
                .omodLocalFormId = part.omod.localFormId,
            };
        }

        [[nodiscard]] std::uint32_t findPartByStableKey(
            const Runtime& state,
            const weapon_motion_cache::StablePartKey& key)
        {
            auto match = kInvalidId;
            for (std::uint32_t index = 0; index < state.partCount; ++index) {
                const auto candidate = stablePartKey(state, index);
                if (!textEqualInsensitive(candidate.sourceName, key.sourceName) ||
                    !textEqualInsensitive(candidate.nodePath, key.nodePath) ||
                    !textEqualInsensitive(
                        candidate.omodPluginName, key.omodPluginName) ||
                    candidate.omodLocalFormId != key.omodLocalFormId) {
                    continue;
                }
                if (match != kInvalidId) {
                    return kInvalidId;
                }
                match = index;
            }
            return match;
        }

        [[nodiscard]] std::uint64_t buildCurrentLoadoutKey(
            const Runtime& state)
        {
            weapon_motion_cache::LoadoutIdentity identity{
                .weaponPluginName = std::string(
                    boundedText(state.weaponIdentity.pluginName)),
                .weaponLocalFormId = state.weaponIdentity.localFormId,
                .weaponKeywordFlags = state.weaponClassification.keywordFlags,
                .weaponFamilyFlags = state.weaponClassification.familyFlags,
                .weaponPrimaryFamily = static_cast<std::uint32_t>(
                    state.weaponClassification.primaryFamily),
            };
            identity.parts.reserve(state.partCount);
            for (std::uint32_t index = 0; index < state.partCount; ++index) {
                identity.parts.push_back(stablePartKey(state, index));
            }
            return weapon_motion_cache::buildLoadoutKey(std::move(identity));
        }

        [[nodiscard]] std::uint32_t findPartForGrip(
            const Runtime& state,
            const rock::provider::RockProviderWeaponPartGripStateV1& grip)
        {
            if (grip.bodyId != kInvalidBodyId) {
                for (std::uint32_t index = 0; index < state.partCount; ++index) {
                    if (state.parts[index].bodyId == grip.bodyId) {
                        return index;
                    }
                }
            }
            if (grip.omodFormId != 0) {
                for (std::uint32_t index = 0; index < state.partCount; ++index) {
                    if (state.parts[index].omod.runtimeFormId == grip.omodFormId) {
                        return index;
                    }
                }
            }
            return findPartByName(state, boundedText(grip.sourceName));
        }

        void refreshPartStageState(Runtime& state, const std::uint32_t partId)
        {
            auto* part = findPart(state, partId);
            if (!part) {
                return;
            }
            part->stageCount = 0;
            part->flags &= ~(
                flag(PaperWeaponMotionPartFlagV1::ExactAuthoredAvailable) |
                flag(PaperWeaponMotionPartFlagV1::LearnedPrimaryAvailable) |
                flag(PaperWeaponMotionPartFlagV1::LearnedReturnAvailable));
            for (std::uint32_t index = 0; index < state.stageCount; ++index) {
                const auto& stage = state.stages[index].value;
                if (stage.partId != partId) {
                    continue;
                }
                ++part->stageCount;
                if (stage.source == PaperWeaponMotionSourceV1::ExactAuthored) {
                    part->flags |= flag(
                        PaperWeaponMotionPartFlagV1::ExactAuthoredAvailable);
                } else if (stage.kind == PaperWeaponMotionStageKindV1::Primary) {
                    part->flags |= flag(
                        PaperWeaponMotionPartFlagV1::LearnedPrimaryAvailable);
                } else {
                    part->flags |= flag(
                        PaperWeaponMotionPartFlagV1::LearnedReturnAvailable);
                }
            }
        }

        struct StageUpdate
        {
            std::uint32_t index{ kInvalidId };
            PaperWeaponMotionServingDecisionV1 decision{
                PaperWeaponMotionServingDecisionV1::None
            };
            bool accepted{ false };
        };

        [[nodiscard]] StageUpdate upsertStage(
            Runtime& state,
            const std::uint32_t partId,
            const PaperWeaponMotionSourceV1 source,
            const PaperWeaponMotionStageKindV1 kind,
            const std::uint32_t sourceClipId,
            const std::uint32_t sourceBoundary,
            const std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1>& keys,
            const float totalArcLength,
            const float peakDelta)
        {
            if (partId >= state.partCount ||
                !std::isfinite(totalArcLength) ||
                totalArcLength < kMinimumExcursionGameUnits) {
                return {
                    .decision = PaperWeaponMotionServingDecisionV1::
                        RejectedInvalidPath,
                };
            }

            auto existing = kInvalidId;
            for (std::uint32_t index = 0; index < state.stageCount; ++index) {
                const auto& stage = state.stages[index].value;
                if (stage.partId == partId && stage.kind == kind) {
                    existing = index;
                    break;
                }
            }

            if (existing != kInvalidId) {
                const auto& current = state.stages[existing].value;
                if (current.source == PaperWeaponMotionSourceV1::ExactAuthored &&
                    source == PaperWeaponMotionSourceV1::Learned) {
                    return {
                        .index = existing,
                        .decision = kind == PaperWeaponMotionStageKindV1::Primary ?
                            PaperWeaponMotionServingDecisionV1::
                                RejectedSmallerPrimary :
                            PaperWeaponMotionServingDecisionV1::
                                RejectedSmallerReturn,
                    };
                }
                if (current.source == source &&
                    totalArcLength <
                        current.totalArcLengthGameUnits * kReplaceRatio) {
                    return {
                        .index = existing,
                        .decision = kind == PaperWeaponMotionStageKindV1::Primary ?
                            PaperWeaponMotionServingDecisionV1::
                                RejectedSmallerPrimary :
                            PaperWeaponMotionServingDecisionV1::
                                RejectedSmallerReturn,
                    };
                }
            } else if (state.stageCount >= state.stages.size()) {
                state.catalog.flags |=
                    flag(PaperWeaponMotionCatalogFlagV1::StagesTruncated);
                ++state.catalog.omittedStageCount;
                return {
                    .decision = PaperWeaponMotionServingDecisionV1::
                        NoServingSlot,
                };
            }

            const auto stageIndex = existing != kInvalidId ?
                existing : state.stageCount++;
            auto& stage = state.stages[stageIndex];
            stage = {};
            stage.value.stageId = stageIndex;
            stage.value.partId = partId;
            stage.value.source = source;
            stage.value.kind = kind;
            stage.value.flags =
                flag(PaperWeaponMotionStageFlagV1::Valid) |
                flag(PaperWeaponMotionStageFlagV1::TravelExtremeValid) |
                (source == PaperWeaponMotionSourceV1::ExactAuthored ?
                    flag(PaperWeaponMotionStageFlagV1::ExactAuthored) |
                        flag(PaperWeaponMotionStageFlagV1::LiveRebased) :
                    flag(PaperWeaponMotionStageFlagV1::Learned)) |
                (kind == PaperWeaponMotionStageKindV1::Return ?
                    flag(PaperWeaponMotionStageFlagV1::ReturnStage) : 0u);
            stage.value.sourceClipId = sourceClipId;
            stage.value.sourceBoundary = sourceBoundary;
            stage.value.keyCount = PAPER_WEAPON_MOTION_KEY_COUNT_V1;
            stage.value.totalArcLengthGameUnits = totalArcLength;
            stage.value.maximumTravelArcPosition =
                kind == PaperWeaponMotionStageKindV1::Primary ?
                    totalArcLength : 0.0f;
            stage.value.restArcPosition =
                kind == PaperWeaponMotionStageKindV1::Return ?
                    totalArcLength : 0.0f;
            stage.value.peakDeltaGameUnits = peakDelta;
            stage.value.restDeltaGameUnits = poseDistance(
                keys.front(), keys.back());
            stage.value.start = keys.front();
            stage.value.end = keys.back();
            stage.value.restReference =
                kind == PaperWeaponMotionStageKindV1::Return ?
                    keys.back() : keys.front();
            stage.keys = keys;

            if (source == PaperWeaponMotionSourceV1::Learned) {
                ++state.catalog.learningRevision;
                state.learning.learningRevision = state.catalog.learningRevision;
            } else {
                ++state.catalog.authoredRevision;
            }
            refreshPartStageState(state, partId);
            publishCatalogChanged(state, partId);
            return {
                .index = stageIndex,
                .decision = existing == kInvalidId ?
                    (kind == PaperWeaponMotionStageKindV1::Primary ?
                        PaperWeaponMotionServingDecisionV1::StoredPrimary :
                        PaperWeaponMotionServingDecisionV1::StoredReturn) :
                    (kind == PaperWeaponMotionStageKindV1::Primary ?
                        PaperWeaponMotionServingDecisionV1::ReplacedPrimary :
                        PaperWeaponMotionServingDecisionV1::ReplacedReturn),
                .accepted = true,
            };
        }

        void clearCatalogForNewObservation(
            Runtime& state,
            const PaperReloadCatalogStateV1& catalog,
            const PaperReloadFrameStateV1& frame)
        {
            for (const auto& hand : state.hands) {
                if (!hand.active) {
                    continue;
                }
                queueEvent(
                    state,
                    PaperEventKindV1::WeaponManipulationEnded,
                    hand.value.partId,
                    hand.value.hand,
                    flag(PaperWeaponManipulationEndReasonV1::WeaponChanged),
                    hand.value.normalizedProgress);
            }
            state.catalog = {};
            state.learning = {};
            state.manipulation = {};
            state.store = {};
            state.partCount = 0;
            state.stageCount = 0;
            state.recorderCount = 0;
            state.partNodeNames.fill({});
            state.partNodePaths.fill({});
            state.weaponIdentity = catalog.weapon;
            state.weaponClassification = catalog.classification;
            state.cacheLoadoutKey = 0;
            state.lastQueuedAuthoredRevision = 0;
            state.cacheLookupState = CacheLookupState::Uninitialized;
            for (auto& recorder : state.recorders) {
                std::destroy_at(std::addressof(recorder));
                std::construct_at(std::addressof(recorder));
            }
            std::destroy_at(std::addressof(state.exact));
            std::construct_at(std::addressof(state.exact));
            state.hands = {};

            std::array<
                PaperReloadEvidenceV1,
                PAPER_MAX_RELOAD_EVIDENCE_V1> evidence{};
            std::array<
                reload_observation::EvidenceMotionSource,
                PAPER_MAX_RELOAD_EVIDENCE_V1> sources{};
            std::uint32_t evidenceCount = 0;
            std::uint32_t sourceCount = 0;
            std::uint32_t nodeCount = 0;
            if (reload_observation::copyEvidence(
                    catalog.catalogSequence,
                    0,
                    evidence.data(),
                    static_cast<std::uint32_t>(evidence.size()),
                    evidenceCount) != PaperResultV1::Ok ||
                reload_observation::copyEvidenceMotionSources(
                    sources.data(),
                    static_cast<std::uint32_t>(sources.size()),
                    sourceCount) != PaperResultV1::Ok ||
                reload_observation::copyCatalogNodes(
                    catalog.catalogSequence,
                    0,
                    state.catalogNodes.data(),
                    static_cast<std::uint32_t>(state.catalogNodes.size()),
                    nodeCount) != PaperResultV1::Ok) {
                return;
            }

            for (std::uint32_t sourceIndex = 0;
                 sourceIndex < sourceCount &&
                 state.partCount < state.parts.size();
                 ++sourceIndex) {
                const auto& source = sources[sourceIndex];
                const PaperReloadEvidenceV1* metadata = nullptr;
                for (std::uint32_t evidenceIndex = 0;
                     evidenceIndex < evidenceCount;
                     ++evidenceIndex) {
                    if (evidence[evidenceIndex].evidenceId == source.evidenceId) {
                        metadata = std::addressof(evidence[evidenceIndex]);
                        break;
                    }
                }
                if (!metadata) {
                    continue;
                }
                auto& part = state.parts[state.partCount];
                part = {};
                part.partId = state.partCount;
                part.evidenceId = source.evidenceId;
                part.bodyId = source.bodyId;
                part.sourceNodeId = source.sourceNodeId;
                part.interactionNodeId = source.interactionNodeId;
                part.selectedNodeId = source.selectedNodeId;
                part.flags = flag(PaperWeaponMotionPartFlagV1::Valid);
                if (hasFlag(
                        source.flags,
                        flag(reload_observation::
                            EvidenceMotionSourceFlag::BaselineValid))) {
                    part.flags |=
                        flag(PaperWeaponMotionPartFlagV1::BaselineValid);
                }
                if (hasFlag(
                        source.flags,
                        flag(reload_observation::
                            EvidenceMotionSourceFlag::CurrentValid))) {
                    part.flags |=
                        flag(PaperWeaponMotionPartFlagV1::CurrentValid);
                }
                if (hasFlag(
                        source.flags,
                        flag(reload_observation::
                            EvidenceMotionSourceFlag::SourceNodeSelected))) {
                    part.flags |=
                        flag(PaperWeaponMotionPartFlagV1::SourceNodeSelected);
                }
                if (hasFlag(
                        source.flags,
                        flag(reload_observation::
                            EvidenceMotionSourceFlag::InteractionNodeFallback))) {
                    part.flags |= flag(
                        PaperWeaponMotionPartFlagV1::InteractionNodeFallback);
                }
                part.partKind = metadata->partKind;
                part.reloadRole = metadata->reloadRole;
                part.supportRole = metadata->supportRole;
                part.socketRole = metadata->socketRole;
                part.actionRole = metadata->actionRole;
                part.fallbackGripPose = metadata->fallbackGripPose;
                part.classificationSource = metadata->classificationSource;
                copyText(part.sourceName, boundedText(metadata->sourceName));
                if (source.selectedNodeId >= 0) {
                    for (std::uint32_t nodeIndex = 0;
                         nodeIndex < nodeCount;
                         ++nodeIndex) {
                        if (state.catalogNodes[nodeIndex].nodeId !=
                            static_cast<std::uint32_t>(source.selectedNodeId)) {
                            continue;
                        }
                        copyText(
                            state.partNodeNames[state.partCount],
                            boundedText(state.catalogNodes[nodeIndex].name));
                        copyText(
                            state.partNodePaths[state.partCount],
                            boundedText(
                                state.catalogNodes[nodeIndex].rootRelativePath));
                        break;
                    }
                }
                part.omod = metadata->omod;
                part.baselineWeaponLocal = source.baselineWeaponLocal;
                part.currentWeaponLocal = source.currentWeaponLocal;
                ++state.partCount;
            }
            state.catalog.flags = flag(PaperWeaponMotionCatalogFlagV1::Valid);
            if (state.options.liveMotionLearning) {
                state.catalog.flags |=
                    flag(PaperWeaponMotionCatalogFlagV1::LearningActive);
            }
            state.catalog.weaponFormId = catalog.weaponFormId;
            state.catalog.weaponGenerationKey = catalog.weaponGenerationKey;
            state.catalog.catalogSequence = nextSequence(s_nextCatalogSequence);
            state.catalog.observationCatalogSequence = catalog.catalogSequence;
            state.catalog.observationSnapshotSequence = frame.snapshotSequence;
            state.catalog.worldGeneration = frame.worldGeneration;
            state.catalog.skeletonGeneration = frame.skeletonGeneration;
            state.catalog.rockProviderGeneration = frame.rockProviderGeneration;
            state.catalog.paperProviderGeneration = frame.paperProviderGeneration;
            state.catalog.partCount = state.partCount;
            state.catalog.omittedPartCount = sourceCount - state.partCount;
            if (state.catalog.omittedPartCount != 0) {
                state.catalog.flags |=
                    flag(PaperWeaponMotionCatalogFlagV1::PartsTruncated);
            }
            state.recorderCount = state.options.liveMotionLearning ?
                (std::min)(
                    state.partCount,
                    static_cast<std::uint32_t>(state.recorders.size())) :
                0;
            for (std::uint32_t index = 0; index < state.recorderCount; ++index) {
                state.recorders[index].value.recorderId = index;
                state.recorders[index].value.partId = index;
                state.recorders[index].value.flags =
                    flag(PaperWeaponMotionRecorderFlagV1::Valid);
            }
            state.learning.flags = state.options.liveMotionLearning ?
                flag(PaperWeaponMotionLearningFlagV1::Active) |
                    flag(PaperWeaponMotionLearningFlagV1::
                        NativeGraphObservations) :
                0;
            state.learning.weaponFormId = catalog.weaponFormId;
            state.learning.weaponGenerationKey = catalog.weaponGenerationKey;
            state.learning.recorderCount = state.recorderCount;
            state.store.flags = flag(PaperWeaponMotionStoreFlagV1::Active) |
                flag(PaperWeaponMotionStoreFlagV1::InMemoryCatalog) |
                flag(PaperWeaponMotionStoreFlagV1::RawObservationEvidenceAvailable);
            if (s_cacheStore && s_cacheStore->readEnabled()) {
                state.store.flags |= flag(
                    PaperWeaponMotionStoreFlagV1::CompiledStageCacheEnabled);
            }
            if (s_cacheStore && s_cacheStore->writeEnabled()) {
                state.store.flags |= flag(
                    PaperWeaponMotionStoreFlagV1::
                        CompiledStageCacheWriteEnabled);
            }
            state.store.weaponFormId = catalog.weaponFormId;
            state.store.weaponGenerationKey = catalog.weaponGenerationKey;
            queueEvent(state, PaperEventKindV1::WeaponMotionCatalogChanged);
        }

        void beginCacheLookup(Runtime& state)
        {
            if (state.cacheLookupState != CacheLookupState::Uninitialized) {
                return;
            }
            if (!state.options.cacheRead || !s_cacheStore ||
                !s_cacheStore->readEnabled()) {
                state.cacheLookupState = CacheLookupState::Bypass;
                return;
            }
            if (state.cacheLoadoutKey == 0) {
                state.cacheLoadoutKey = buildCurrentLoadoutKey(state);
            }
            if (state.cacheLoadoutKey == 0) {
                state.cacheLookupState = CacheLookupState::Bypass;
                PAPER_LOG_DEBUG(
                    MotionCache,
                    "Bypassing compiled cache: weapon/loadout identity is not stable");
                return;
            }
            if (s_cacheStore->requestLoad(state.cacheLoadoutKey)) {
                state.cacheLookupState = CacheLookupState::Pending;
                state.store.flags |= flag(
                    PaperWeaponMotionStoreFlagV1::CacheLookupPending);
            }
        }

        [[nodiscard]] bool validateCachedRecord(
            const Runtime& state,
            const weapon_motion_cache::CompiledRecord& record)
        {
            if (record.loadoutKey != state.cacheLoadoutKey ||
                record.stages.empty() ||
                record.stages.size() > state.stages.size()) {
                return false;
            }
            std::array<bool, PAPER_MAX_WEAPON_MOTION_PARTS_V1 * 2> occupied{};
            for (const auto& cachedStage : record.stages) {
                const auto partId = findPartByStableKey(state, cachedStage.part);
                if (partId == kInvalidId ||
                    !hasFlag(
                        state.parts[partId].flags,
                        flag(PaperWeaponMotionPartFlagV1::BaselineValid))) {
                    return false;
                }
                const auto baseline = fromTransform(
                    state.parts[partId].baselineWeaponLocal);
                if (!finite(baseline)) {
                    return false;
                }
                std::array<
                    PaperReloadQsTransformV1,
                    PAPER_WEAPON_MOTION_KEY_COUNT_V1> rebasedKeys{};
                for (std::size_t key = 0; key < rebasedKeys.size(); ++key) {
                    rebasedKeys[key] = compose(
                        baseline, cachedStage.keys[key]);
                    if (!finite(rebasedKeys[key])) {
                        return false;
                    }
                }
                if (pathLength(rebasedKeys) < kMinimumExcursionGameUnits) {
                    return false;
                }
                const auto kindIndex =
                    cachedStage.kind == PaperWeaponMotionStageKindV1::Primary ?
                        0u : 1u;
                const auto slot = partId * 2u + kindIndex;
                if (slot >= occupied.size() || occupied[slot]) {
                    return false;
                }
                occupied[slot] = true;
                for (const auto& follower : cachedStage.followers) {
                    const auto followerPartId = findPartByStableKey(
                        state, follower.part);
                    if (followerPartId == kInvalidId ||
                        !hasFlag(
                            state.parts[followerPartId].flags,
                            flag(PaperWeaponMotionPartFlagV1::BaselineValid))) {
                        return false;
                    }
                    const auto followerBaseline = fromTransform(
                        state.parts[followerPartId].baselineWeaponLocal);
                    if (!finite(followerBaseline)) {
                        return false;
                    }
                    for (const auto& key : follower.keys) {
                        if (!finite(compose(followerBaseline, key))) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        [[nodiscard]] bool hydrateCachedRecord(
            Runtime& state,
            const weapon_motion_cache::CompiledRecord& record)
        {
            if (!validateCachedRecord(state, record)) {
                return false;
            }
            for (const auto& cachedStage : record.stages) {
                const auto partId = findPartByStableKey(state, cachedStage.part);
                const auto baseline = fromTransform(
                    state.parts[partId].baselineWeaponLocal);
                std::array<
                    PaperReloadQsTransformV1,
                    PAPER_WEAPON_MOTION_KEY_COUNT_V1> keys{};
                for (std::size_t key = 0; key < keys.size(); ++key) {
                    keys[key] = compose(baseline, cachedStage.keys[key]);
                    if (!finite(keys[key])) {
                        return false;
                    }
                }
                const auto length = pathLength(keys);
                const auto update = upsertStage(
                    state,
                    partId,
                    PaperWeaponMotionSourceV1::ExactAuthored,
                    cachedStage.kind,
                    kInvalidId,
                    0,
                    keys,
                    length,
                    poseDistance(keys.front(), keys.back()));
                if (!update.accepted || update.index >= state.stageCount) {
                    return false;
                }
                auto& stage = state.stages[update.index];
                stage.value.flags |= flag(
                    PaperWeaponMotionStageFlagV1::HydratedFromCache);
                for (const auto& cachedFollower : cachedStage.followers) {
                    if (stage.followerCount >= stage.followers.size()) {
                        return false;
                    }
                    const auto followerPartId = findPartByStableKey(
                        state, cachedFollower.part);
                    const auto followerBaseline = fromTransform(
                        state.parts[followerPartId].baselineWeaponLocal);
                    auto& follower = stage.followers[stage.followerCount];
                    follower = {};
                    follower.value.stageId = update.index;
                    follower.value.followerIndex = stage.followerCount;
                    follower.value.partId = followerPartId;
                    follower.value.evidenceId =
                        state.parts[followerPartId].evidenceId;
                    follower.value.bodyId = state.parts[followerPartId].bodyId;
                    follower.value.flags =
                        flag(PaperWeaponMotionFollowerFlagV1::Valid) |
                        flag(PaperWeaponMotionFollowerFlagV1::CoTimed) |
                        flag(PaperWeaponMotionFollowerFlagV1::EvidenceMapped) |
                        flag(PaperWeaponMotionFollowerFlagV1::LiveRebased) |
                        flag(PaperWeaponMotionFollowerFlagV1::
                            HydratedFromCache) |
                        (cachedFollower.flags &
                            flag(PaperWeaponMotionFollowerFlagV1::Rigid));
                    follower.value.keyCount =
                        PAPER_WEAPON_MOTION_KEY_COUNT_V1;
                    copyText(
                        follower.value.sourceName,
                        boundedText(state.parts[followerPartId].sourceName));
                    for (std::size_t key = 0; key < follower.keys.size(); ++key) {
                        follower.keys[key] = compose(
                            followerBaseline, cachedFollower.keys[key]);
                        if (!finite(follower.keys[key])) {
                            return false;
                        }
                    }
                    ++stage.followerCount;
                }
                stage.value.followerCount = stage.followerCount;
            }
            return true;
        }

        void pollCacheLookup(Runtime& state)
        {
            if (state.cacheLookupState != CacheLookupState::Pending ||
                !s_cacheStore) {
                return;
            }
            weapon_motion_cache::LoadResult result{};
            if (!s_cacheStore->tryTakeLoadResult(result)) {
                return;
            }
            if (result.loadoutKey != state.cacheLoadoutKey) {
                return;
            }
            state.store.flags &= ~flag(
                PaperWeaponMotionStoreFlagV1::CacheLookupPending);
            const auto sessionHit =
                result.status == weapon_motion_cache::LoadStatus::HitSession;
            const auto persistentHit =
                result.status == weapon_motion_cache::LoadStatus::HitPersistent;
            if ((sessionHit || persistentHit) && result.record &&
                hydrateCachedRecord(state, *result.record)) {
                state.cacheLookupState = sessionHit ?
                    CacheLookupState::HitSession :
                    CacheLookupState::HitPersistent;
                state.store.flags |= flag(sessionHit ?
                    PaperWeaponMotionStoreFlagV1::SessionCacheHit :
                    PaperWeaponMotionStoreFlagV1::PersistentCacheHit);
                PAPER_LOG_INFO(
                    MotionCache,
                    "Hydrated {} authored stages for loadout {:016X} from {} cache",
                    result.record->stages.size(),
                    state.cacheLoadoutKey,
                    sessionHit ? "session" : "persistent");
                return;
            }
            state.cacheLookupState = CacheLookupState::Miss;
            PAPER_LOG_DEBUG(
                MotionCache,
                "Compiled cache miss for loadout {:016X}; exact preharvest enabled",
                state.cacheLoadoutKey);
        }

        [[nodiscard]] std::string sourceAnimationPath(
            const Runtime& state,
            const std::uint32_t sourceClipId)
        {
            for (std::uint32_t index = 0; index < state.exact.clipCount; ++index) {
                const auto& clip = state.exact.clips[index];
                if (clip.clipId == sourceClipId) {
                    return std::string(boundedText(clip.animationPath));
                }
            }
            return {};
        }

        [[nodiscard]] std::unique_ptr<
            weapon_motion_cache::CompiledRecord> buildCacheRecord(
                const Runtime& state)
        {
            auto record = std::make_unique<
                weapon_motion_cache::CompiledRecord>();
            record->loadoutKey = state.cacheLoadoutKey;
            for (std::uint32_t index = 0; index < state.exact.clipCount; ++index) {
                const auto& clip = state.exact.clips[index];
                if (clip.acquisition !=
                        PaperReloadAnimationAcquisitionV1::
                            ExactWeaponPreharvest) {
                    continue;
                }
                const auto path = std::string(boundedText(clip.animationPath));
                if (path.empty()) {
                    continue;
                }
                const auto duplicate = std::find_if(
                    record->animationPaths.begin(),
                    record->animationPaths.end(),
                    [&path](const std::string& existing) {
                        return textEqualInsensitive(existing, path);
                    });
                if (duplicate == record->animationPaths.end()) {
                    record->animationPaths.push_back(path);
                }
            }
            record->stages.reserve(state.stageCount);
            for (std::uint32_t index = 0; index < state.stageCount; ++index) {
                const auto& stage = state.stages[index];
                if (stage.value.source !=
                    PaperWeaponMotionSourceV1::ExactAuthored) {
                    continue;
                }
                const auto* part = findPart(state, stage.value.partId);
                if (!part || !hasFlag(
                        part->flags,
                        flag(PaperWeaponMotionPartFlagV1::BaselineValid))) {
                    return {};
                }
                weapon_motion_cache::CachedStage cached{
                    .part = stablePartKey(state, stage.value.partId),
                    .kind = stage.value.kind,
                    .sourceAnimationPath = sourceAnimationPath(
                        state, stage.value.sourceClipId),
                    .sourceBoundary = stage.value.sourceBoundary,
                    .totalArcLengthGameUnits =
                        stage.value.totalArcLengthGameUnits,
                    .peakDeltaGameUnits = stage.value.peakDeltaGameUnits,
                };
                const auto baseline = fromTransform(part->baselineWeaponLocal);
                const auto inverseBaseline = inverse(baseline);
                for (std::size_t key = 0; key < cached.keys.size(); ++key) {
                    cached.keys[key] = compose(inverseBaseline, stage.keys[key]);
                }
                cached.followers.reserve(stage.followerCount);
                for (std::uint32_t followerIndex = 0;
                     followerIndex < stage.followerCount;
                     ++followerIndex) {
                    const auto& follower = stage.followers[followerIndex];
                    const auto* followerPart = findPart(
                        state, follower.value.partId);
                    if (!followerPart || !hasFlag(
                            followerPart->flags,
                            flag(PaperWeaponMotionPartFlagV1::BaselineValid))) {
                        return {};
                    }
                    weapon_motion_cache::CachedFollower cachedFollower{
                        .part = stablePartKey(state, follower.value.partId),
                        .flags = follower.value.flags &
                            flag(PaperWeaponMotionFollowerFlagV1::Rigid),
                    };
                    const auto inverseFollowerBaseline = inverse(
                        fromTransform(followerPart->baselineWeaponLocal));
                    for (std::size_t key = 0;
                         key < cachedFollower.keys.size();
                         ++key) {
                        cachedFollower.keys[key] = compose(
                            inverseFollowerBaseline, follower.keys[key]);
                    }
                    cached.followers.push_back(std::move(cachedFollower));
                }
                record->stages.push_back(std::move(cached));
            }
            if (record->stages.empty()) {
                return {};
            }
            return record;
        }

        void queueCompletedCacheRecord(Runtime& state)
        {
            const bool compilationFinished =
                state.exact.preharvestCompleted &&
                state.exact.currentClipIndex == kInvalidId &&
                !state.exact.finalizing &&
                state.exact.nextClip >= state.exact.clipCount;
            if (!compilationFinished) {
                return;
            }
            if (state.catalog.authoredRevision == 0) {
                state.cacheLookupState = CacheLookupState::ExactUnavailable;
                state.store.flags &= ~flag(
                    PaperWeaponMotionStoreFlagV1::
                        RawAnimationEvidenceAvailable);
                state.store.rawAnimationCatalogSequence = 0;
                state.store.rawAnimationCatalogRevision = 0;
                return;
            }
            if (state.cacheLookupState == CacheLookupState::Bypass ||
                !state.exact.cacheEligible) {
                state.cacheLookupState = CacheLookupState::Compiled;
                state.store.flags &= ~flag(
                    PaperWeaponMotionStoreFlagV1::
                        RawAnimationEvidenceAvailable);
                state.store.rawAnimationCatalogSequence = 0;
                state.store.rawAnimationCatalogRevision = 0;
                return;
            }
            if (!state.options.cacheWrite ||
                state.cacheLookupState != CacheLookupState::Miss ||
                !s_cacheStore || state.cacheLoadoutKey == 0 ||
                state.catalog.authoredRevision ==
                    state.lastQueuedAuthoredRevision) {
                return;
            }
            auto record = buildCacheRecord(state);
            if (record && s_cacheStore->requestSave(std::move(record))) {
                state.lastQueuedAuthoredRevision =
                    state.catalog.authoredRevision;
                state.cacheLookupState = CacheLookupState::Compiled;
                state.store.flags &= ~flag(
                    PaperWeaponMotionStoreFlagV1::
                        RawAnimationEvidenceAvailable);
                state.store.rawAnimationCatalogSequence = 0;
                state.store.rawAnimationCatalogRevision = 0;
                PAPER_LOG_DEBUG(
                    MotionCache,
                    "Queued compiled authored revision {} for loadout {:016X}",
                    state.catalog.authoredRevision,
                    state.cacheLoadoutKey);
            }
        }

        [[nodiscard]] bool synchronizeParts(
            Runtime& state,
            const rock::provider::RockProviderAnimationPhaseContextV1& context)
        {
            PaperReloadCatalogStateV1 catalog{};
            PaperReloadFrameStateV1 frame{};
            if (reload_observation::getCatalogState(catalog) != PaperResultV1::Ok ||
                reload_observation::getFrameState(frame) != PaperResultV1::Ok ||
                catalog.catalogSequence == 0 || frame.snapshotSequence == 0 ||
                catalog.weaponGenerationKey != frame.weaponGenerationKey) {
                return false;
            }
            if (state.catalog.observationCatalogSequence !=
                    catalog.catalogSequence ||
                state.catalog.weaponGenerationKey !=
                    catalog.weaponGenerationKey) {
                clearCatalogForNewObservation(state, catalog, frame);
            }
            if (state.partCount == 0) {
                return false;
            }
            beginCacheLookup(state);

            std::array<
                reload_observation::EvidenceMotionSource,
                PAPER_MAX_RELOAD_EVIDENCE_V1> sources{};
            std::uint32_t sourceCount = 0;
            if (reload_observation::copyEvidenceMotionSources(
                    sources.data(),
                    static_cast<std::uint32_t>(sources.size()),
                    sourceCount) != PaperResultV1::Ok) {
                return false;
            }
            for (std::uint32_t index = 0; index < state.partCount; ++index) {
                auto& part = state.parts[index];
                part.flags &= ~flag(PaperWeaponMotionPartFlagV1::CurrentValid);
                for (std::uint32_t sourceIndex = 0;
                     sourceIndex < sourceCount;
                     ++sourceIndex) {
                    const auto& source = sources[sourceIndex];
                    if (source.evidenceId != part.evidenceId) {
                        continue;
                    }
                    part.currentWeaponLocal = source.currentWeaponLocal;
                    if (hasFlag(
                            source.flags,
                            flag(reload_observation::
                                EvidenceMotionSourceFlag::CurrentValid))) {
                        part.flags |=
                            flag(PaperWeaponMotionPartFlagV1::CurrentValid);
                    }
                    break;
                }
            }
            state.catalog.frameIndex = context.frameIndex;
            state.catalog.observationSnapshotSequence = frame.snapshotSequence;
            state.catalog.worldGeneration = context.worldGeneration;
            state.catalog.skeletonGeneration = context.skeletonGeneration;
            state.catalog.rockProviderGeneration = context.providerGeneration;
            state.catalog.partCount = state.partCount;
            state.catalog.stageCount = state.stageCount;
            state.catalog.activeRecorderCount = 0;
            return true;
        }

        void completeCandidate(
            Runtime& state,
            RecorderRecord& recorder,
            const PaperWeaponMotionStrokeTerminationV1 termination)
        {
            auto& value = recorder.value;
            value.lastTermination = termination;
            ++state.learning.completedStrokeCount;
            if (termination != PaperWeaponMotionStrokeTerminationV1::Settled &&
                termination != PaperWeaponMotionStrokeTerminationV1::SampleCapacity) {
                ++state.learning.interruptedStrokeCount;
                value.lastDecision =
                    PaperWeaponMotionServingDecisionV1::RejectedInvalidPath;
            } else if (recorder.recordingCount < 2) {
                ++state.learning.rejectedStrokeCount;
                value.lastDecision =
                    PaperWeaponMotionServingDecisionV1::RejectedBelowNoise;
            } else {
                const auto peak = peakIndex(
                    recorder.samples, recorder.recordingCount);
                std::array<
                    PaperReloadQsTransformV1,
                    PAPER_WEAPON_MOTION_KEY_COUNT_V1> primary{};
                const auto primaryLength = resamplePath(
                    recorder.samples, 0, peak, primary);
                const auto update = upsertStage(
                    state,
                    value.partId,
                    PaperWeaponMotionSourceV1::Learned,
                    PaperWeaponMotionStageKindV1::Primary,
                    kInvalidId,
                    peak,
                    primary,
                    primaryLength,
                    value.peakExcursionGameUnits);
                value.lastDecision = update.decision;
                auto accepted = update.accepted;

                const auto returnsToRest = peak + 1 < recorder.recordingCount &&
                    poseDistance(
                        recorder.samples[recorder.recordingCount - 1],
                        recorder.samples[0]) <= kReturnToRestTolerance;
                if (returnsToRest) {
                    std::array<
                        PaperReloadQsTransformV1,
                        PAPER_WEAPON_MOTION_KEY_COUNT_V1> returning{};
                    const auto returnLength = resamplePath(
                        recorder.samples,
                        peak,
                        recorder.recordingCount - 1,
                        returning);
                    const auto returnUpdate = upsertStage(
                        state,
                        value.partId,
                        PaperWeaponMotionSourceV1::Learned,
                        PaperWeaponMotionStageKindV1::Return,
                        kInvalidId,
                        peak,
                        returning,
                        returnLength,
                        value.peakExcursionGameUnits);
                    accepted = accepted || returnUpdate.accepted;
                    if (!update.accepted) {
                        value.lastDecision = returnUpdate.decision;
                    }
                }
                if (accepted) {
                    ++state.learning.acceptedStrokeCount;
                    value.flags |=
                        flag(PaperWeaponMotionRecorderFlagV1::CandidateValid);
                } else {
                    ++state.learning.rejectedStrokeCount;
                    value.flags &= ~flag(
                        PaperWeaponMotionRecorderFlagV1::CandidateValid);
                }
            }
            queueEvent(
                state,
                PaperEventKindV1::WeaponMotionCandidateCompleted,
                value.partId,
                PaperHandV1::Right,
                flag(value.lastDecision),
                0.0f);
            value.phase = PaperWeaponMotionRecorderPhaseV1::WaitingForRest;
            value.sampleCount = recorder.recordingCount;
            value.stableFrameCount = 0;
            recorder.recordingCount = 0;
        }

        void updateLearning(Runtime& state, const std::uint64_t frameIndex)
        {
            state.learning.snapshotSequence =
                nextSequence(s_nextLearningSnapshotSequence);
            state.learning.frameIndex = frameIndex;
            state.learning.activeRecorderCount = 0;
            for (std::uint32_t index = 0;
                 index < state.recorderCount;
                 ++index) {
                auto& recorder = state.recorders[index];
                auto& value = recorder.value;
                const auto& part = state.parts[value.partId];
                value.lastObservedFrameIndex = frameIndex;
                value.flags &= ~flag(
                    PaperWeaponMotionRecorderFlagV1::ObservationValid);
                if (!hasFlag(
                        part.flags,
                        flag(PaperWeaponMotionPartFlagV1::CurrentValid))) {
                    if (value.phase ==
                        PaperWeaponMotionRecorderPhaseV1::Recording) {
                        completeCandidate(
                            state,
                            recorder,
                            PaperWeaponMotionStrokeTerminationV1::ObservationLost);
                    }
                    recorder.previousValid = false;
                    continue;
                }
                const auto current = fromTransform(part.currentWeaponLocal);
                if (!finite(current)) {
                    continue;
                }
                value.flags |= flag(
                    PaperWeaponMotionRecorderFlagV1::ObservationValid);
                value.currentPose = current;
                ++state.learning.observationCount;
                if (!recorder.previousValid) {
                    recorder.previous = current;
                    recorder.previousValid = true;
                    continue;
                }
                const auto frameStable = stable(recorder.previous, current);
                switch (value.phase) {
                case PaperWeaponMotionRecorderPhaseV1::WaitingForRest:
                    value.stableFrameCount = frameStable ?
                        value.stableFrameCount + 1 : 0;
                    if (value.stableFrameCount >= kArmStableFrames) {
                        value.phase = PaperWeaponMotionRecorderPhaseV1::Armed;
                        value.restPose = current;
                        value.stableFrameCount = 0;
                    }
                    break;
                case PaperWeaponMotionRecorderPhaseV1::Armed: {
                    const auto excursion = poseDistance(value.restPose, current);
                    if (excursion >= kMinimumExcursionGameUnits) {
                        value.phase =
                            PaperWeaponMotionRecorderPhaseV1::Recording;
                        value.startFrameIndex = frameIndex;
                        value.peakExcursionGameUnits = excursion;
                        value.recordedArcLengthGameUnits = excursion;
                        value.stableFrameCount = 0;
                        recorder.samples[0] = value.restPose;
                        recorder.samples[1] = current;
                        recorder.recordingCount = 2;
                    }
                    break;
                }
                case PaperWeaponMotionRecorderPhaseV1::Recording: {
                    ++state.learning.activeRecorderCount;
                    value.peakExcursionGameUnits = (std::max)(
                        value.peakExcursionGameUnits,
                        poseDistance(value.restPose, current));
                    value.stableFrameCount = frameStable ?
                        value.stableFrameCount + 1 : 0;
                    if (!frameStable &&
                        recorder.recordingCount < recorder.samples.size()) {
                        value.recordedArcLengthGameUnits += poseDistance(
                            recorder.samples[recorder.recordingCount - 1],
                            current);
                        recorder.samples[recorder.recordingCount++] = current;
                    }
                    value.sampleCount = recorder.recordingCount;
                    if (recorder.recordingCount >= recorder.samples.size()) {
                        state.learning.flags |= flag(
                            PaperWeaponMotionLearningFlagV1::
                                SampleCapacityReached);
                        completeCandidate(
                            state,
                            recorder,
                            PaperWeaponMotionStrokeTerminationV1::
                                SampleCapacity);
                    } else if (value.stableFrameCount >= kSettleFrames) {
                        if (recorder.recordingCount == 0 ||
                            !stable(
                                recorder.samples[recorder.recordingCount - 1],
                                current)) {
                            recorder.samples[recorder.recordingCount++] = current;
                        }
                        completeCandidate(
                            state,
                            recorder,
                            PaperWeaponMotionStrokeTerminationV1::Settled);
                    }
                    break;
                }
                default:
                    break;
                }
                recorder.previous = current;
            }
            state.learning.activeRecorderCount =
                state.catalog.activeRecorderCount =
                    state.learning.activeRecorderCount;
        }

        [[nodiscard]] bool rigidFollowers(
            const std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1>& leaderKeys,
            const std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1>& followerKeys)
        {
            for (std::size_t key = 1; key < leaderKeys.size(); ++key) {
                if (!relativeTransformStable(
                        leaderKeys[0],
                        followerKeys[0],
                        leaderKeys[key],
                        followerKeys[key],
                        kRigidFollowerTranslationTolerance,
                        kRigidFollowerRotationToleranceRadians,
                        kRigidFollowerScaleTolerance)) {
                    return false;
                }
            }
            return true;
        }

        struct RankedFollower
        {
            std::uint32_t candidateIndex{ kInvalidId };
            std::array<
                PaperReloadQsTransformV1,
                PAPER_WEAPON_MOTION_KEY_COUNT_V1> keys{};
            float overlap{ 0.0f };
            bool rigid{ false };
        };

        void attachFollowers(
            Runtime& state,
            const std::uint32_t stageIndex,
            const ExactTrackCandidate& leader,
            const PaperWeaponMotionStageKindV1 kind)
        {
            if (stageIndex >= state.stageCount) {
                return;
            }
            auto& stage = state.stages[stageIndex];
            const auto& leaderKeys =
                kind == PaperWeaponMotionStageKindV1::Primary ?
                    leader.primary : leader.returning;
            const auto& sourcePositions =
                kind == PaperWeaponMotionStageKindV1::Primary ?
                    leader.primarySourcePositions :
                    leader.returnSourcePositions;
            std::array<
                RankedFollower,
                PAPER_MAX_RELOAD_EXACT_TRACKS_PER_CLIP_V1> ranked{};
            std::uint32_t rankedCount = 0;
            for (std::uint32_t index = 0;
                 index < state.exact.candidateCount;
                 ++index) {
                const auto& candidate = state.exact.candidates[index];
                if (!candidate.valid || candidate.partId == leader.partId ||
                    (kind == PaperWeaponMotionStageKindV1::Return &&
                        candidate.returnLength < kMinimumExcursionGameUnits)) {
                    continue;
                }
                auto& entry = ranked[rankedCount];
                if (!sampleAtSourcePositions(
                        candidate.sourcePoses,
                        candidate.sampleCount,
                        sourcePositions,
                        entry.keys)) {
                    continue;
                }
                const auto length = pathLength(entry.keys);
                if (length < kMinimumExcursionGameUnits) {
                    continue;
                }
                entry.rigid = rigidFollowers(leaderKeys, entry.keys);
                entry.overlap = temporalMotionOverlap(leaderKeys, entry.keys);
                if (!entry.rigid &&
                    entry.overlap < kFollowerMotionOverlapMinimum) {
                    continue;
                }
                entry.candidateIndex = index;
                ++rankedCount;
            }
            std::sort(
                ranked.begin(),
                ranked.begin() + rankedCount,
                [&state](const RankedFollower& left,
                         const RankedFollower& right) {
                    if (left.rigid != right.rigid) {
                        return left.rigid;
                    }
                    if (left.overlap != right.overlap) {
                        return left.overlap > right.overlap;
                    }
                    return state.exact.candidates[left.candidateIndex].partId <
                        state.exact.candidates[right.candidateIndex].partId;
                });
            const auto acceptedCount = (std::min)(
                rankedCount,
                static_cast<std::uint32_t>(stage.followers.size()));
            if (rankedCount > acceptedCount) {
                stage.value.flags |= flag(
                    PaperWeaponMotionStageFlagV1::FollowersTruncated);
                state.catalog.flags |= flag(
                    PaperWeaponMotionCatalogFlagV1::FollowersTruncated);
                state.catalog.omittedFollowerCount += rankedCount - acceptedCount;
            }
            for (std::uint32_t rank = 0; rank < acceptedCount; ++rank) {
                const auto& rankedFollower = ranked[rank];
                const auto& candidate = state.exact.candidates[
                    rankedFollower.candidateIndex];
                auto& follower = stage.followers[stage.followerCount];
                follower = {};
                follower.value.stageId = stageIndex;
                follower.value.followerIndex = stage.followerCount;
                follower.value.partId = candidate.partId;
                const auto* part = findPart(state, candidate.partId);
                if (part) {
                    follower.value.evidenceId = part->evidenceId;
                    follower.value.bodyId = part->bodyId;
                    copyText(
                        follower.value.sourceName,
                        boundedText(part->sourceName));
                }
                follower.value.flags =
                    flag(PaperWeaponMotionFollowerFlagV1::Valid) |
                    flag(PaperWeaponMotionFollowerFlagV1::CoTimed) |
                    flag(PaperWeaponMotionFollowerFlagV1::EvidenceMapped) |
                    flag(PaperWeaponMotionFollowerFlagV1::LiveRebased);
                if (rankedFollower.rigid) {
                    follower.value.flags |=
                        flag(PaperWeaponMotionFollowerFlagV1::Rigid);
                }
                follower.value.keyCount = PAPER_WEAPON_MOTION_KEY_COUNT_V1;
                follower.keys = rankedFollower.keys;
                ++stage.followerCount;
            }
            stage.value.followerCount = stage.followerCount;
            state.catalog.followerCount += stage.followerCount;
        }

        void finalizeExactClip(Runtime& state)
        {
            if (state.exact.nextCandidateToFinalize <
                state.exact.candidateCount) {
                const auto& candidate = state.exact.candidates[
                    state.exact.nextCandidateToFinalize++];
                if (!candidate.valid) {
                    return;
                }
                const auto primary = upsertStage(
                    state,
                    candidate.partId,
                    PaperWeaponMotionSourceV1::ExactAuthored,
                    PaperWeaponMotionStageKindV1::Primary,
                    candidate.sourceClipId,
                    candidate.sourceBoundary,
                    candidate.primary,
                    candidate.primaryLength,
                    candidate.peakDelta);
                if (primary.accepted) {
                    attachFollowers(
                        state,
                        primary.index,
                        candidate,
                        PaperWeaponMotionStageKindV1::Primary);
                }
                if (candidate.returnLength >= kMinimumExcursionGameUnits) {
                    const auto returning = upsertStage(
                        state,
                        candidate.partId,
                        PaperWeaponMotionSourceV1::ExactAuthored,
                        PaperWeaponMotionStageKindV1::Return,
                        candidate.sourceClipId,
                        candidate.sourceBoundary,
                        candidate.returning,
                        candidate.returnLength,
                        candidate.peakDelta);
                    if (returning.accepted) {
                        attachFollowers(
                            state,
                            returning.index,
                            candidate,
                        PaperWeaponMotionStageKindV1::Return);
                    }
                }
                return;
            }
            ++state.exact.processedExactClipCount;
            state.exact.currentClipIndex = kInvalidId;
            state.exact.trackCount = 0;
            state.exact.nextTrack = 0;
            state.exact.candidateCount = 0;
            state.exact.nextCandidateToFinalize = 0;
            state.exact.finalizing = false;
        }

        void synchronizeExactCatalog(Runtime& state)
        {
            PaperReloadAnimationCatalogStateV1 catalog{};
            if (animation_evidence::getCatalogState(catalog) !=
                    PaperResultV1::Ok ||
                catalog.catalogSequence == 0 ||
                catalog.weaponGenerationKey !=
                    state.catalog.weaponGenerationKey) {
                return;
            }
            state.catalog.animationCatalogSequence = catalog.catalogSequence;
            state.catalog.animationCatalogRevision = catalog.catalogRevision;
            state.store.rawAnimationCatalogSequence = catalog.catalogSequence;
            state.store.rawAnimationCatalogRevision = catalog.catalogRevision;
            state.store.flags |= flag(
                PaperWeaponMotionStoreFlagV1::RawAnimationEvidenceAvailable);
            const bool preharvestCompleted = hasFlag(
                catalog.statusFlags,
                flag(PaperReloadAnimationCatalogFlagV1::
                    ExactPreharvestCompleted));
            const bool cacheEligible = preharvestCompleted &&
                catalog.omittedClipCount == 0 &&
                catalog.exactClipsRejected == 0 &&
                catalog.exactTargetTruncationCount == 0 &&
                !hasFlag(
                    catalog.statusFlags,
                    flag(PaperReloadAnimationCatalogFlagV1::
                        ExactPreharvestFailed)) &&
                !hasFlag(
                    catalog.statusFlags,
                    flag(PaperReloadAnimationCatalogFlagV1::
                        ClipCapacityTruncated)) &&
                !hasFlag(
                    catalog.statusFlags,
                    flag(PaperReloadAnimationCatalogFlagV1::
                        SampleStorageTruncated));
            if (catalog.exactClipCount != 0) {
                state.catalog.flags |= flag(
                    PaperWeaponMotionCatalogFlagV1::ExactEvidenceAvailable);
            } else {
                state.catalog.flags &= ~flag(
                    PaperWeaponMotionCatalogFlagV1::ExactEvidenceAvailable);
            }

            if (state.exact.catalogSequence != catalog.catalogSequence) {
                if (state.exact.catalogSequence != 0) {
                    std::destroy_at(std::addressof(state.exact));
                    std::construct_at(std::addressof(state.exact));
                }
                state.exact.catalogSequence = catalog.catalogSequence;
            }
            if (catalog.clipCount < state.exact.clipCount) {
                std::destroy_at(std::addressof(state.exact));
                std::construct_at(std::addressof(state.exact));
                state.exact.catalogSequence = catalog.catalogSequence;
            }
            state.exact.preharvestCompleted = preharvestCompleted;
            state.exact.cacheEligible = cacheEligible;
            if (catalog.catalogRevision != state.exact.catalogRevision ||
                catalog.clipCount != state.exact.clipCount) {
                std::uint32_t copied = 0;
                const auto remaining = static_cast<std::uint32_t>(
                    state.exact.clips.size()) - state.exact.clipCount;
                if (remaining != 0 &&
                    animation_evidence::copyClips(
                        catalog.catalogSequence,
                        state.exact.clipCount,
                        state.exact.clips.data() + state.exact.clipCount,
                        remaining,
                        copied) == PaperResultV1::Ok) {
                    state.exact.clipCount += copied;
                }
            }
            for (std::uint32_t index = 0;
                 index < state.exact.clipCount && state.exact.cacheEligible;
                 ++index) {
                const auto& clip = state.exact.clips[index];
                if (clip.acquisition !=
                    PaperReloadAnimationAcquisitionV1::
                        ExactWeaponPreharvest) {
                    continue;
                }
                state.exact.cacheEligible =
                    !boundedText(clip.animationPath).empty() &&
                    !hasFlag(
                        clip.flags,
                        flag(PaperReloadAnimationClipFlagV1::
                            AnimationPathTruncated)) &&
                    !hasFlag(
                        clip.flags,
                        flag(PaperReloadAnimationClipFlagV1::TracksTruncated)) &&
                    !hasFlag(
                        clip.flags,
                        flag(PaperReloadAnimationClipFlagV1::SamplesTruncated));
            }
            state.exact.catalogRevision = catalog.catalogRevision;
        }

        [[nodiscard]] bool beginNextExactClip(Runtime& state)
        {
            while (state.exact.nextClip < state.exact.clipCount) {
                const auto clipIndex = state.exact.nextClip++;
                const auto& clip = state.exact.clips[clipIndex];
                if (clip.acquisition !=
                        PaperReloadAnimationAcquisitionV1::
                            ExactWeaponPreharvest ||
                    clip.trackSpace !=
                        PaperReloadAnimationTrackSpaceV1::WeaponRootLocal ||
                    clip.trackCount == 0) {
                    continue;
                }
                std::uint32_t copied = 0;
                if (animation_evidence::copyTracks(
                        state.exact.catalogSequence,
                        clip.clipId,
                        0,
                        state.exact.tracks.data(),
                        static_cast<std::uint32_t>(state.exact.tracks.size()),
                        copied) != PaperResultV1::Ok || copied == 0) {
                    continue;
                }
                state.exact.currentClipIndex = clipIndex;
                state.exact.trackCount = copied;
                state.exact.nextTrack = 0;
                state.exact.candidateCount = 0;
                state.exact.nextCandidateToFinalize = 0;
                state.exact.finalizing = false;
                return true;
            }
            return false;
        }

        void processOneExactTrack(Runtime& state)
        {
            synchronizeExactCatalog(state);
            if (state.exact.finalizing) {
                finalizeExactClip(state);
                return;
            }
            if (state.exact.currentClipIndex == kInvalidId &&
                !beginNextExactClip(state)) {
                return;
            }
            if (state.exact.nextTrack >= state.exact.trackCount) {
                state.exact.finalizing = true;
                return;
            }
            const auto& clip =
                state.exact.clips[state.exact.currentClipIndex];
            const auto& track =
                state.exact.tracks[state.exact.nextTrack++];
            if (!hasFlag(
                    track.flags,
                    flag(PaperReloadAnimationTrackFlagV1::SamplesAvailable)) ||
                track.sampleCount < 2 ||
                state.exact.candidateCount >= state.exact.candidates.size()) {
                return;
            }
            const auto partId = findPartByName(
                state, boundedText(track.boneName));
            const auto* part = findPart(state, partId);
            if (!part || !hasFlag(
                    part->flags,
                    flag(PaperWeaponMotionPartFlagV1::BaselineValid))) {
                return;
            }
            std::uint32_t sampleCount = 0;
            if (animation_evidence::copySamples(
                    state.exact.catalogSequence,
                    clip.clipId,
                    track.trackId,
                    0,
                    state.exact.samples.data(),
                    static_cast<std::uint32_t>(state.exact.samples.size()),
                    sampleCount) != PaperResultV1::Ok || sampleCount < 2) {
                return;
            }
            const auto liveRest = fromTransform(part->baselineWeaponLocal);
            const auto authoredRest = state.exact.samples[0].transform;
            if (!finite(liveRest) || !finite(authoredRest)) {
                return;
            }
            for (std::uint32_t index = 0; index < sampleCount; ++index) {
                state.exact.poses[index] = rebase(
                    authoredRest,
                    liveRest,
                    state.exact.samples[index].transform);
            }
            const auto peak = peakIndex(state.exact.poses, sampleCount);
            if (peak == 0) {
                return;
            }
            auto& candidate =
                state.exact.candidates[state.exact.candidateCount];
            candidate = {};
            candidate.partId = partId;
            candidate.sourceClipId = clip.clipId;
            candidate.sourceBoundary = peak;
            candidate.sampleCount = sampleCount;
            std::copy_n(
                state.exact.poses.data(),
                sampleCount,
                candidate.sourcePoses.data());
            candidate.peakDelta = poseDistance(
                state.exact.poses[0], state.exact.poses[peak]);
            candidate.primaryLength = resamplePathWithSourcePositions(
                state.exact.poses,
                0,
                peak,
                candidate.primary,
                candidate.primarySourcePositions);
            if (peak + 1 < sampleCount &&
                poseDistance(
                    state.exact.poses[sampleCount - 1],
                    state.exact.poses[0]) <= kReturnToRestTolerance) {
                candidate.returnLength = resamplePathWithSourcePositions(
                    state.exact.poses,
                    peak,
                    sampleCount - 1,
                    candidate.returning,
                    candidate.returnSourcePositions);
            }
            candidate.valid =
                candidate.primaryLength >= kMinimumExcursionGameUnits;
            if (candidate.valid) {
                ++state.exact.candidateCount;
            }
            if (state.exact.nextTrack >= state.exact.trackCount) {
                state.exact.finalizing = true;
            }
        }

        void refreshCatalogFlags(Runtime& state)
        {
            auto learned = false;
            auto authored = false;
            state.catalog.followerCount = 0;
            for (std::uint32_t index = 0; index < state.stageCount; ++index) {
                const auto& stage = state.stages[index];
                learned = learned ||
                    stage.value.source == PaperWeaponMotionSourceV1::Learned;
                authored = authored ||
                    stage.value.source ==
                        PaperWeaponMotionSourceV1::ExactAuthored;
                state.catalog.followerCount += stage.followerCount;
            }
            state.catalog.flags &= ~(
                flag(PaperWeaponMotionCatalogFlagV1::LearnedPathsAvailable) |
                flag(PaperWeaponMotionCatalogFlagV1::AuthoredPathsAvailable) |
                flag(PaperWeaponMotionCatalogFlagV1::BuildingAuthoredPaths));
            if (learned) {
                state.catalog.flags |= flag(
                    PaperWeaponMotionCatalogFlagV1::LearnedPathsAvailable);
            }
            if (authored) {
                state.catalog.flags |= flag(
                    PaperWeaponMotionCatalogFlagV1::AuthoredPathsAvailable);
            }
            if (state.exact.currentClipIndex != kInvalidId ||
                state.exact.nextClip < state.exact.clipCount) {
                state.catalog.flags |= flag(
                    PaperWeaponMotionCatalogFlagV1::BuildingAuthoredPaths);
            }
            state.catalog.stageCount = state.stageCount;
            state.catalog.processedExactClipCount =
                state.exact.processedExactClipCount;
            state.catalog.pendingExactClipCount =
                state.exact.clipCount - state.exact.nextClip +
                (state.exact.currentClipIndex != kInvalidId ? 1u : 0u);
            state.store.catalogSequence = state.catalog.catalogSequence;
            state.store.frameIndex = state.catalog.frameIndex;
            state.store.learningRevision = state.catalog.learningRevision;
            state.store.authoredRevision = state.catalog.authoredRevision;
            state.store.rawObservationSnapshotSequence =
                state.catalog.observationSnapshotSequence;
            state.store.inMemoryPartCount = state.partCount;
            state.store.inMemoryStageCount = state.stageCount;
            if (s_cacheStore) {
                const auto statistics = s_cacheStore->statistics();
                state.store.persistentRecordCount =
                    statistics.persistentRecordCount;
                state.store.pendingWriteCount = statistics.pendingWriteCount;
                state.store.droppedCaptureCount =
                    statistics.droppedRequestCount;
                if (statistics.persistentStorageAvailable) {
                    state.store.flags |= flag(
                        PaperWeaponMotionStoreFlagV1::
                            PersistentStorageAvailable);
                } else {
                    state.store.flags &= ~flag(
                        PaperWeaponMotionStoreFlagV1::
                            PersistentStorageAvailable);
                }
            }
        }

        [[nodiscard]] const StageRecord* selectProjectedStage(
            const Runtime& state,
            const std::uint32_t partId,
            const PaperReloadQsTransformV1& current,
            Projection& outProjection)
        {
            const StageRecord* selected = nullptr;
            auto bestResidual = (std::numeric_limits<float>::max)();
            for (std::uint32_t index = 0; index < state.stageCount; ++index) {
                const auto& stage = state.stages[index];
                if (stage.value.partId != partId) {
                    continue;
                }
                const auto projection = projectOntoPath(current, stage.keys);
                if (!projection.valid || projection.residual >= bestResidual) {
                    continue;
                }
                selected = std::addressof(stage);
                outProjection = projection;
                bestResidual = projection.residual;
            }
            return selected;
        }

        void emitHandTransitionEvents(
            Runtime& state,
            const HandRuntime& previous,
            const HandRuntime& current)
        {
            const auto hand = current.value.hand;
            if (!previous.active && current.active) {
                queueEvent(
                    state,
                    PaperEventKindV1::WeaponManipulationStarted,
                    current.value.partId,
                    hand,
                    current.value.flags,
                    current.value.normalizedProgress);
            }
            if (previous.active && !current.active) {
                queueEvent(
                    state,
                    PaperEventKindV1::WeaponManipulationEnded,
                    previous.value.partId,
                    hand,
                    flag(current.value.lastEndReason),
                    previous.value.normalizedProgress);
                return;
            }
            if (!current.active) {
                return;
            }
            if (previous.active &&
                previous.value.stageId != current.value.stageId) {
                queueEvent(
                    state,
                    PaperEventKindV1::WeaponManipulationStageChanged,
                    current.value.partId,
                    hand,
                    current.value.flags,
                    current.value.normalizedProgress);
            }
            if (!previous.atMaximum && current.atMaximum) {
                queueEvent(
                    state,
                    PaperEventKindV1::WeaponManipulationReachedMaximum,
                    current.value.partId,
                    hand,
                    current.value.flags,
                    current.value.normalizedProgress);
            }
            if (previous.active && !previous.atRest && current.atRest) {
                queueEvent(
                    state,
                    PaperEventKindV1::WeaponManipulationReachedRest,
                    current.value.partId,
                    hand,
                    current.value.flags,
                    current.value.normalizedProgress);
            }
        }

        void updateManipulationHand(
            Runtime& state,
            RockApiClient& rockApi,
            const PaperHandV1 hand,
            const std::uint64_t frameIndex)
        {
            const auto handIndex = hand == PaperHandV1::Left ? 1u : 0u;
            const auto previous = state.hands[handIndex];
            auto& current = state.hands[handIndex];
            current = {};
            current.value.hand = hand;
            current.value.snapshotSequence = state.manipulation.snapshotSequence;
            current.value.frameIndex = frameIndex;
            current.value.flags = flag(PaperWeaponManipulationHandFlagV1::Valid);

            rock::provider::RockProviderWeaponPartGripStateV1 grip{};
            const auto rockHand = hand == PaperHandV1::Left ?
                rock::provider::RockProviderHand::Left :
                rock::provider::RockProviderHand::Right;
            if (!rockApi.queryWeaponPartGripState(rockHand, grip)) {
                if (previous.active) {
                    current.value.lastEndReason =
                        PaperWeaponManipulationEndReasonV1::GripEnded;
                }
                emitHandTransitionEvents(state, previous, current);
                return;
            }
            state.manipulation.flags |= flag(
                PaperWeaponManipulationFrameFlagV1::RockGripStateAvailable);
            if (grip.active == 0 ||
                grip.gripKind ==
                    rock::provider::RockProviderWeaponPartGripKindV1::None) {
                if (previous.active) {
                    current.value.lastEndReason =
                        PaperWeaponManipulationEndReasonV1::GripEnded;
                }
                emitHandTransitionEvents(state, previous, current);
                return;
            }

            current.value.flags |=
                flag(PaperWeaponManipulationHandFlagV1::GripActive);
            current.value.rockGripKind = flag(grip.gripKind);
            current.value.gripSequence = grip.gripSequence;
            if (grip.attachOnly != 0) {
                current.value.flags |=
                    flag(PaperWeaponManipulationHandFlagV1::AttachOnly);
            }
            if (grip.weaponGenerationKey !=
                state.catalog.weaponGenerationKey) {
                current.value.lastEndReason =
                    PaperWeaponManipulationEndReasonV1::WeaponChanged;
                current.active = false;
                emitHandTransitionEvents(state, previous, current);
                return;
            }
            ++state.manipulation.activeHandCount;
            state.manipulation.flags |=
                flag(PaperWeaponManipulationFrameFlagV1::AnyGripActive);
            const auto partId = findPartForGrip(state, grip);
            const auto* part = findPart(state, partId);
            if (!part || !hasFlag(
                    part->flags,
                    flag(PaperWeaponMotionPartFlagV1::CurrentValid))) {
                current.value.lastEndReason =
                    PaperWeaponManipulationEndReasonV1::PathUnavailable;
                emitHandTransitionEvents(state, previous, current);
                return;
            }
            current.value.flags |=
                flag(PaperWeaponManipulationHandFlagV1::PartMapped);
            current.value.partId = partId;
            current.value.evidenceId = part->evidenceId;
            current.value.bodyId = part->bodyId;
            current.value.currentPartWeaponLocal =
                fromTransform(part->currentWeaponLocal);
            Projection projection{};
            const auto* stage = selectProjectedStage(
                state,
                partId,
                current.value.currentPartWeaponLocal,
                projection);
            if (!stage) {
                current.value.lastEndReason =
                    PaperWeaponManipulationEndReasonV1::PathUnavailable;
                emitHandTransitionEvents(state, previous, current);
                return;
            }
            current.active = true;
            current.value.flags |=
                flag(PaperWeaponManipulationHandFlagV1::PathAvailable) |
                flag(PaperWeaponManipulationHandFlagV1::ProgressValid);
            current.value.source = stage->value.source;
            current.value.stageKind = stage->value.kind;
            current.value.stageId = stage->value.stageId;
            current.value.arcPositionGameUnits = projection.arcPosition;
            current.value.normalizedProgress = projection.normalizedProgress;
            current.value.projectionResidualGameUnits = projection.residual;
            current.value.maximumTravelArcPosition =
                stage->value.maximumTravelArcPosition;
            current.value.restArcPosition = stage->value.restArcPosition;
            current.value.projectedPartWeaponLocal = projection.pose;
            const auto isReturn =
                stage->value.kind == PaperWeaponMotionStageKindV1::Return;
            if (isReturn) {
                current.value.flags |=
                    flag(PaperWeaponManipulationHandFlagV1::ReturnStage);
            }
            current.atMaximum = isReturn ?
                projection.normalizedProgress <= kRestProgressThreshold :
                projection.normalizedProgress >= kMaximumProgressThreshold;
            current.atRest = isReturn ?
                projection.normalizedProgress >= kMaximumProgressThreshold :
                projection.normalizedProgress <= kRestProgressThreshold;
            if (current.atMaximum) {
                current.value.flags |= flag(
                    PaperWeaponManipulationHandFlagV1::AtMaximumTravel);
            }
            if (current.atRest) {
                current.value.flags |=
                    flag(PaperWeaponManipulationHandFlagV1::AtRest);
            }
            ++state.manipulation.mappedHandCount;
            state.manipulation.flags |= flag(
                PaperWeaponManipulationFrameFlagV1::AnyMappedManipulation);
            emitHandTransitionEvents(state, previous, current);
        }

        void updateManipulation(
            Runtime& state,
            RockApiClient& rockApi,
            const rock::provider::RockProviderAnimationPhaseContextV1& context,
            const std::uint32_t paperProviderGeneration)
        {
            state.manipulation = {};
            state.manipulation.flags =
                flag(PaperWeaponManipulationFrameFlagV1::Valid);
            state.manipulation.weaponFormId = state.catalog.weaponFormId;
            state.manipulation.weaponGenerationKey =
                state.catalog.weaponGenerationKey;
            state.manipulation.snapshotSequence =
                nextSequence(s_nextManipulationSnapshotSequence);
            state.manipulation.catalogSequence = state.catalog.catalogSequence;
            state.manipulation.frameIndex = context.frameIndex;
            state.manipulation.eventSequence = state.eventSequence;
            state.manipulation.worldGeneration = context.worldGeneration;
            state.manipulation.skeletonGeneration = context.skeletonGeneration;
            state.manipulation.rockProviderGeneration = context.providerGeneration;
            state.manipulation.paperProviderGeneration = paperProviderGeneration;
            updateManipulationHand(
                state, rockApi, PaperHandV1::Right, context.frameIndex);
            updateManipulationHand(
                state, rockApi, PaperHandV1::Left, context.frameIndex);
            state.manipulation.eventSequence = state.eventSequence;
        }

        template <class Value, std::size_t Capacity>
        [[nodiscard]] PaperResultV1 copyRange(
            const std::array<Value, Capacity>& source,
            const std::uint32_t count,
            const std::uint32_t first,
            Value* destination,
            const std::uint32_t maximum,
            std::uint32_t& copied)
        {
            copied = 0;
            if (first > count) {
                return PaperResultV1::OutOfRange;
            }
            if (first == count || maximum == 0) {
                return PaperResultV1::Ok;
            }
            if (!destination) {
                return PaperResultV1::InvalidArgument;
            }
            copied = (std::min)(maximum, count - first);
            std::copy_n(source.data() + first, copied, destination);
            return PaperResultV1::Ok;
        }

        [[nodiscard]] PaperResultV1 validateCatalogSequence(
            const Runtime& state,
            const std::uint64_t catalogSequence)
        {
            return catalogSequence == state.catalog.catalogSequence ?
                PaperResultV1::Ok : PaperResultV1::StaleSnapshot;
        }
    }

    void activate(const RuntimeOptions& options)
    {
        if (s_cacheStore && (options.cacheRead || options.cacheWrite)) {
            s_cacheStore->start();
        }
        if (!s_runtime) {
            s_runtime = std::make_unique<Runtime>();
        }
    }

    void configureCache(const PaperConfig& config)
    {
        const auto iniPath = std::filesystem::path(config.activePath);
        weapon_motion_cache::Settings settings{
            .readEnabled = config.weaponMotionCacheAccess !=
                api::PaperWeaponMotionCacheAccessV1::Off,
            .writeEnabled = config.weaponMotionCacheAccess ==
                api::PaperWeaponMotionCacheAccessV1::ReadWrite,
            .root = iniPath.parent_path() / "MotionCache" / "v1",
            .dataRoot = std::filesystem::current_path() / "Data",
            .maximumSessionBytes =
                static_cast<std::uint64_t>(
                    config.weaponMotionSessionCacheMiB) *
                1024ull * 1024ull,
            .maximumDiskBytes =
                static_cast<std::uint64_t>(config.weaponMotionDiskCacheMiB) *
                1024ull * 1024ull,
            .maximumFileBytes =
                static_cast<std::uint64_t>(
                    config.weaponMotionMaximumFileMiB) *
                1024ull * 1024ull,
            .maximumEntries = config.weaponMotionMaximumEntries,
        };
        if (s_cacheSettings && *s_cacheSettings == settings && s_cacheStore) {
            return;
        }
        if (!s_cacheStore) {
            s_cacheStore = std::make_unique<weapon_motion_cache::Store>();
        }
        s_cacheStore->configure(settings);
        s_cacheSettings = std::move(settings);
        PAPER_LOG_INFO(
            MotionCache,
            "Configured compiled motion cache at '{}' (access={} session={} MiB, disk={} MiB, file={} MiB, entries={})",
            s_cacheSettings->root.string(),
            static_cast<std::uint32_t>(config.weaponMotionCacheAccess),
            config.weaponMotionSessionCacheMiB,
            config.weaponMotionDiskCacheMiB,
            config.weaponMotionMaximumFileMiB,
            config.weaponMotionMaximumEntries);
    }

    void setCacheAccess(const api::PaperWeaponMotionCacheAccessV1 access)
    {
        if (!s_cacheStore) {
            return;
        }
        s_cacheStore->setAccess(
            access != api::PaperWeaponMotionCacheAccessV1::Off,
            access == api::PaperWeaponMotionCacheAccessV1::ReadWrite);
    }

    bool requiresExactAnimationEvidence(const bool cacheReadRequested)
    {
        if (!cacheReadRequested || !s_cacheStore ||
            !s_cacheStore->readEnabled()) {
            return true;
        }
        if (!s_runtime) {
            return false;
        }
        return s_runtime->cacheLookupState == CacheLookupState::Miss ||
            s_runtime->cacheLookupState == CacheLookupState::Bypass;
    }

    void reset(const ResetReason reason)
    {
        s_runtime.reset();
        if (reason == ResetReason::ProviderShutdown) {
            if (s_cacheStore) {
                s_cacheStore->shutdown();
            }
            s_cacheStore.reset();
            s_cacheSettings.reset();
        }
    }

    void completeFrame(
        const rock::provider::RockProviderAnimationPhaseContextV1& context,
        RockApiClient& rockApi,
        const std::uint32_t paperProviderGeneration,
        const RuntimeOptions& options)
    {
        activate(options);
        auto& state = *s_runtime;
        state.options = options;
        if (!synchronizeParts(state, context)) {
            return;
        }
        pollCacheLookup(state);
        state.catalog.paperProviderGeneration = paperProviderGeneration;
        if (options.liveMotionLearning) {
            updateLearning(state, context.frameIndex);
        }
        if (state.cacheLookupState == CacheLookupState::Miss ||
            state.cacheLookupState == CacheLookupState::Bypass) {
            processOneExactTrack(state);
        }
        queueCompletedCacheRecord(state);
        refreshCatalogFlags(state);
        if (options.manipulationTelemetry) {
            updateManipulation(state, rockApi, context, paperProviderGeneration);
        }
    }

    PaperResultV1 getLimits(PaperWeaponMotionLimitsV1& outLimits)
    {
        outLimits = {};
        outLimits.featureBits =
            flag(PaperProviderFeatureBitV1::WeaponMotionCatalog) |
            flag(PaperProviderFeatureBitV1::WeaponMotionDiagnostics) |
            flag(PaperProviderFeatureBitV1::WeaponManipulationTelemetry);
        outLimits.maxParts = PAPER_MAX_WEAPON_MOTION_PARTS_V1;
        outLimits.maxStages = PAPER_MAX_WEAPON_MOTION_STAGES_V1;
        outLimits.keysPerStage = PAPER_WEAPON_MOTION_KEY_COUNT_V1;
        outLimits.maxFollowersPerStage =
            PAPER_MAX_WEAPON_MOTION_FOLLOWERS_V1;
        outLimits.maxRecorders = PAPER_MAX_WEAPON_MOTION_RECORDERS_V1;
        outLimits.nameCapacity = PAPER_WEAPON_MOTION_NAME_CAPACITY_V1;
        outLimits.maxPendingEvents = PAPER_MAX_WEAPON_MOTION_EVENTS_V1;
        return PaperResultV1::Ok;
    }

    PaperResultV1 getCatalogState(PaperWeaponMotionCatalogStateV1& outState)
    {
        if (!s_runtime || s_runtime->catalog.catalogSequence == 0) {
            return PaperResultV1::NotReady;
        }
        outState = s_runtime->catalog;
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyParts(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstPart,
        PaperWeaponMotionPartV1* outParts,
        const std::uint32_t maxParts,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_runtime) {
            return PaperResultV1::NotReady;
        }
        const auto validation = validateCatalogSequence(
            *s_runtime, catalogSequence);
        return validation == PaperResultV1::Ok ?
            copyRange(
                s_runtime->parts,
                s_runtime->partCount,
                firstPart,
                outParts,
                maxParts,
                outCopied) : validation;
    }

    PaperResultV1 copyStages(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstStage,
        PaperWeaponMotionStageV1* outStages,
        const std::uint32_t maxStages,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_runtime) {
            return PaperResultV1::NotReady;
        }
        const auto validation = validateCatalogSequence(
            *s_runtime, catalogSequence);
        if (validation != PaperResultV1::Ok) {
            return validation;
        }
        if (firstStage > s_runtime->stageCount) {
            return PaperResultV1::OutOfRange;
        }
        if (maxStages == 0 || firstStage == s_runtime->stageCount) {
            return PaperResultV1::Ok;
        }
        if (!outStages) {
            return PaperResultV1::InvalidArgument;
        }
        outCopied = (std::min)(
            maxStages, s_runtime->stageCount - firstStage);
        for (std::uint32_t index = 0; index < outCopied; ++index) {
            outStages[index] = s_runtime->stages[firstStage + index].value;
        }
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyStageKeys(
        const std::uint64_t catalogSequence,
        const std::uint32_t stageId,
        const std::uint32_t firstKey,
        PaperReloadQsTransformV1* outKeys,
        const std::uint32_t maxKeys,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_runtime) {
            return PaperResultV1::NotReady;
        }
        const auto validation = validateCatalogSequence(
            *s_runtime, catalogSequence);
        if (validation != PaperResultV1::Ok) {
            return validation;
        }
        if (stageId >= s_runtime->stageCount) {
            return PaperResultV1::NotFound;
        }
        return copyRange(
            s_runtime->stages[stageId].keys,
            PAPER_WEAPON_MOTION_KEY_COUNT_V1,
            firstKey,
            outKeys,
            maxKeys,
            outCopied);
    }

    PaperResultV1 copyFollowers(
        const std::uint64_t catalogSequence,
        const std::uint32_t stageId,
        const std::uint32_t firstFollower,
        PaperWeaponMotionFollowerV1* outFollowers,
        const std::uint32_t maxFollowers,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_runtime) {
            return PaperResultV1::NotReady;
        }
        const auto validation = validateCatalogSequence(
            *s_runtime, catalogSequence);
        if (validation != PaperResultV1::Ok) {
            return validation;
        }
        if (stageId >= s_runtime->stageCount) {
            return PaperResultV1::NotFound;
        }
        const auto& stage = s_runtime->stages[stageId];
        if (firstFollower > stage.followerCount) {
            return PaperResultV1::OutOfRange;
        }
        if (maxFollowers == 0 || firstFollower == stage.followerCount) {
            return PaperResultV1::Ok;
        }
        if (!outFollowers) {
            return PaperResultV1::InvalidArgument;
        }
        outCopied = (std::min)(
            maxFollowers, stage.followerCount - firstFollower);
        for (std::uint32_t index = 0; index < outCopied; ++index) {
            outFollowers[index] =
                stage.followers[firstFollower + index].value;
        }
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyFollowerKeys(
        const std::uint64_t catalogSequence,
        const std::uint32_t stageId,
        const std::uint32_t followerIndex,
        const std::uint32_t firstKey,
        PaperReloadQsTransformV1* outKeys,
        const std::uint32_t maxKeys,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_runtime) {
            return PaperResultV1::NotReady;
        }
        const auto validation = validateCatalogSequence(
            *s_runtime, catalogSequence);
        if (validation != PaperResultV1::Ok) {
            return validation;
        }
        if (stageId >= s_runtime->stageCount ||
            followerIndex >= s_runtime->stages[stageId].followerCount) {
            return PaperResultV1::NotFound;
        }
        return copyRange(
            s_runtime->stages[stageId].followers[followerIndex].keys,
            PAPER_WEAPON_MOTION_KEY_COUNT_V1,
            firstKey,
            outKeys,
            maxKeys,
            outCopied);
    }

    PaperResultV1 getLearningState(PaperWeaponMotionLearningStateV1& outState)
    {
        if (!s_runtime || s_runtime->learning.snapshotSequence == 0) {
            return PaperResultV1::NotReady;
        }
        outState = s_runtime->learning;
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyRecorders(
        const std::uint64_t snapshotSequence,
        const std::uint32_t firstRecorder,
        PaperWeaponMotionRecorderV1* outRecorders,
        const std::uint32_t maxRecorders,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_runtime || s_runtime->learning.snapshotSequence == 0) {
            return PaperResultV1::NotReady;
        }
        if (snapshotSequence != s_runtime->learning.snapshotSequence) {
            return PaperResultV1::StaleSnapshot;
        }
        if (firstRecorder > s_runtime->recorderCount) {
            return PaperResultV1::OutOfRange;
        }
        if (maxRecorders == 0 ||
            firstRecorder == s_runtime->recorderCount) {
            return PaperResultV1::Ok;
        }
        if (!outRecorders) {
            return PaperResultV1::InvalidArgument;
        }
        outCopied = (std::min)(
            maxRecorders, s_runtime->recorderCount - firstRecorder);
        for (std::uint32_t index = 0; index < outCopied; ++index) {
            outRecorders[index] =
                s_runtime->recorders[firstRecorder + index].value;
        }
        return PaperResultV1::Ok;
    }

    PaperResultV1 getManipulationFrameState(
        PaperWeaponManipulationFrameStateV1& outState)
    {
        if (!s_runtime || s_runtime->manipulation.snapshotSequence == 0) {
            return PaperResultV1::NotReady;
        }
        outState = s_runtime->manipulation;
        return PaperResultV1::Ok;
    }

    PaperResultV1 getManipulationHandState(
        const PaperHandV1 hand,
        PaperWeaponManipulationHandStateV1& outState)
    {
        if (!s_runtime || s_runtime->manipulation.snapshotSequence == 0) {
            return PaperResultV1::NotReady;
        }
        if (hand != PaperHandV1::Right && hand != PaperHandV1::Left) {
            return PaperResultV1::InvalidArgument;
        }
        outState = s_runtime->hands[hand == PaperHandV1::Left ? 1u : 0u].value;
        return PaperResultV1::Ok;
    }

    PaperResultV1 getStoreState(PaperWeaponMotionStoreStateV1& outState)
    {
        if (!s_runtime || s_runtime->store.catalogSequence == 0) {
            return PaperResultV1::NotReady;
        }
        outState = s_runtime->store;
        return PaperResultV1::Ok;
    }

    std::uint32_t drainEvents(
        PaperEventV1* outEvents,
        const std::uint32_t maxEvents)
    {
        if (!s_runtime || !outEvents || maxEvents == 0) {
            return 0;
        }
        const auto copied = (std::min)(maxEvents, s_runtime->eventCount);
        std::copy_n(s_runtime->events.data(), copied, outEvents);
        const auto remaining = s_runtime->eventCount - copied;
        if (remaining != 0) {
            std::move(
                s_runtime->events.begin() + copied,
                s_runtime->events.begin() + s_runtime->eventCount,
                s_runtime->events.begin());
        }
        s_runtime->eventCount = remaining;
        return copied;
    }
}
