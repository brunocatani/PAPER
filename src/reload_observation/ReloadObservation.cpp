#include "PCH.h"

#include "reload_observation/ReloadObservation.h"

#include "api/ApiTransform.h"
#include "api/RockApiClient.h"
#include "PaperLog.h"
#include "reload_observation/ReloadObservationPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paper::reload_observation
{
    namespace
    {
        using namespace api;

        constexpr std::uint32_t kMaxSceneVisits = 8192;
        constexpr std::uint32_t kMaxSceneDepth = 64;
        constexpr std::uint32_t kMaxGeometryCopyAttempts = 3;

        template <class Enum>
        [[nodiscard]] constexpr std::uint32_t flag(const Enum value)
        {
            return static_cast<std::uint32_t>(value);
        }

        template <std::size_t Capacity>
        [[nodiscard]] bool copyFixedString(
            char (&destination)[Capacity],
            const std::string_view source)
        {
            static_assert(Capacity > 0);
            const auto copied = std::min(source.size(), Capacity - 1);
            if (copied > 0) {
                std::memcpy(destination, source.data(), copied);
            }
            destination[copied] = '\0';
            return copied != source.size();
        }

        [[nodiscard]] std::string_view fixedStringView(
            const char* value,
            const std::size_t capacity)
        {
            if (!value) {
                return {};
            }
            for (std::size_t index = 0; index < capacity; ++index) {
                if (value[index] == '\0') {
                    return { value, index };
                }
            }
            return { value, capacity };
        }

        [[nodiscard]] bool finiteTransform(const RE::NiTransform& transform)
        {
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    if (!std::isfinite(
                            transform.rotate.entry[row][column])) {
                        return false;
                    }
                }
            }
            return std::isfinite(transform.translate.x) &&
                   std::isfinite(transform.translate.y) &&
                   std::isfinite(transform.translate.z) &&
                   std::isfinite(transform.scale) &&
                   std::abs(transform.scale) > 0.0001f;
        }

        [[nodiscard]] RE::NiMatrix3 transposeRotation(
            const RE::NiMatrix3& matrix)
        {
            RE::NiMatrix3 result{};
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    result.entry[row][column] =
                        matrix.entry[column][row];
                }
            }
            return result;
        }

        [[nodiscard]] RE::NiMatrix3 multiplyStoredRotations(
            const RE::NiMatrix3& lhs,
            const RE::NiMatrix3& rhs)
        {
            RE::NiMatrix3 result{};
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    double value = 0.0;
                    for (int component = 0; component < 3; ++component) {
                        value +=
                            static_cast<double>(
                                lhs.entry[row][component]) *
                            static_cast<double>(
                                rhs.entry[component][column]);
                    }
                    result.entry[row][column] =
                        static_cast<float>(value);
                }
            }
            return result;
        }

        [[nodiscard]] RE::NiPoint3 worldPointToLocal(
            const RE::NiTransform& transform,
            const RE::NiPoint3& point)
        {
            const double inverseScale =
                1.0 / static_cast<double>(transform.scale);
            const double offset[3]{
                static_cast<double>(point.x) - transform.translate.x,
                static_cast<double>(point.y) - transform.translate.y,
                static_cast<double>(point.z) - transform.translate.z,
            };
            RE::NiPoint3 result{};
            result.x = static_cast<float>(
                (transform.rotate.entry[0][0] * offset[0] +
                    transform.rotate.entry[0][1] * offset[1] +
                    transform.rotate.entry[0][2] * offset[2]) *
                inverseScale);
            result.y = static_cast<float>(
                (transform.rotate.entry[1][0] * offset[0] +
                    transform.rotate.entry[1][1] * offset[1] +
                    transform.rotate.entry[1][2] * offset[2]) *
                inverseScale);
            result.z = static_cast<float>(
                (transform.rotate.entry[2][0] * offset[0] +
                    transform.rotate.entry[2][1] * offset[1] +
                    transform.rotate.entry[2][2] * offset[2]) *
                inverseScale);
            return result;
        }

        [[nodiscard]] RE::NiTransform relativeTransform(
            const RE::NiTransform& referenceWorld,
            const RE::NiTransform& childWorld)
        {
            RE::NiTransform result{};
            result.rotate = multiplyStoredRotations(
                childWorld.rotate,
                transposeRotation(referenceWorld.rotate));
            result.translate = worldPointToLocal(
                referenceWorld,
                childWorld.translate);
            result.scale = childWorld.scale / referenceWorld.scale;
            return result;
        }

        [[nodiscard]] std::uint64_t nextSequence(
            std::uint64_t& sequence)
        {
            if (++sequence == 0) {
                ++sequence;
            }
            return sequence;
        }

        [[nodiscard]] bool hasGripFlag(
            const std::uint32_t flags,
            const rock::provider::
                RockProviderEquippedWeaponGripStateFlagV1 value)
        {
            return (flags & flag(value)) != 0;
        }

        [[nodiscard]] bool validWeaponGrip(
            const rock::provider::
                RockProviderEquippedWeaponGripStateV1& gripState)
        {
            return hasGripFlag(
                       gripState.flags,
                       rock::provider::
                           RockProviderEquippedWeaponGripStateFlagV1::Valid) &&
                   gripState.weaponFormId != 0 &&
                   gripState.weaponGenerationKey != 0 &&
                   gripState.weaponNode != 0;
        }

        [[nodiscard]] RE::NiNode* weaponRoot(
            const rock::provider::
                RockProviderEquippedWeaponGripStateV1& gripState)
        {
            if (!validWeaponGrip(gripState)) {
                return nullptr;
            }
            auto* object = reinterpret_cast<RE::NiAVObject*>(
                gripState.weaponNode);
            return object ? object->IsNode() : nullptr;
        }

        [[nodiscard]] PaperFormIdentityV1 describeForm(
            const std::uint32_t runtimeFormId)
        {
            PaperFormIdentityV1 identity{};
            identity.runtimeFormId = runtimeFormId;
            if (runtimeFormId == 0) {
                return identity;
            }

            auto* form = RE::TESForm::GetFormByID(runtimeFormId);
            if (!form) {
                return identity;
            }
            identity.flags |= flag(PaperFormIdentityFlagV1::Resolved);
            identity.formType =
                static_cast<std::uint32_t>(form->GetFormType());

            if (auto* file = form->GetFile(0)) {
                const auto filename = file->GetFilename();
                if (!filename.empty()) {
                    identity.localFormId = form->GetLocalFormID();
                    identity.flags |= flag(
                        PaperFormIdentityFlagV1::PluginIdentityValid);
                    if (copyFixedString(
                            identity.pluginName,
                            filename)) {
                        identity.flags |= flag(
                            PaperFormIdentityFlagV1::
                                PluginNameTruncated);
                    }
                }
            }

            if (const char* editorId = form->GetFormEditorID();
                editorId && editorId[0] != '\0') {
                identity.flags |= flag(
                    PaperFormIdentityFlagV1::EditorIdValid);
                if (copyFixedString(identity.editorId, editorId)) {
                    identity.flags |= flag(
                        PaperFormIdentityFlagV1::EditorIdTruncated);
                }
            }

            const auto fullName = RE::TESFullName::GetFullName(*form);
            if (!fullName.empty()) {
                identity.flags |= flag(
                    PaperFormIdentityFlagV1::DisplayNameValid);
                if (copyFixedString(identity.displayName, fullName)) {
                    identity.flags |= flag(
                        PaperFormIdentityFlagV1::
                            DisplayNameTruncated);
                }
            }
            return identity;
        }

        struct NodeRecord
        {
            PaperReloadNodeCatalogEntryV1 value{};
            std::string identityName;
        };

        struct EvidenceRecord
        {
            PaperReloadEvidenceV1 value{};
            std::vector<PaperPoint3V1> points;
            std::uint32_t scheduledPointCount{ 0 };
            std::uint32_t shortCopyAttempts{ 0 };
        };

        struct CatalogBuffer
        {
            PaperReloadCatalogStateV1 state{};
            std::vector<NodeRecord> nodes;
            std::vector<EvidenceRecord> evidence;
            std::array<
                std::uint32_t,
                PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1>
                targetNodeIds{};
            std::array<
                std::int16_t,
                PAPER_MAX_RELOAD_CATALOG_NODES_V1>
                targetIndexByNode{};
            std::uint32_t targetCount{ 0 };
            std::uint32_t omittedTargetCount{ 0 };
            bool geometryRequested{ false };
            bool valid{ false };
        };

        struct PhaseCapture
        {
            std::array<
                PaperReloadNodeObservationV1,
                PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1>
                observations{};
            std::uint32_t count{ 0 };
            bool captured{ false };
        };

        struct WorkingFrame
        {
            PaperReloadFrameStateV1 state{};
            PhaseCapture nativeGraphOutput{};
            PhaseCapture postRock{};
            bool active{ false };
        };

        struct PublishedFrame
        {
            PaperReloadFrameStateV1 state{};
            std::array<
                PaperReloadNodeObservationV1,
                PAPER_MAX_RELOAD_NODE_OBSERVATIONS_V1>
                observations{};
            bool valid{ false };
        };

        CatalogBuffer s_catalog{};
        CatalogBuffer s_buildingCatalog{};
        WorkingFrame s_workingFrame{};
        PublishedFrame s_publishedFrame{};
        bool s_buildingCatalogActive{ false };
        bool s_rebuildRequested{ false };
        bool s_geometryDemandActive{ false };
        std::uint32_t s_geometryEvidenceIndex{ 0 };
        std::uint64_t s_lastGeometryAdvanceFrame{
            (std::numeric_limits<std::uint64_t>::max)()
        };
        std::uint64_t s_catalogSequence{ 0 };
        std::uint64_t s_snapshotSequence{ 0 };

        void clearPublishedFrame()
        {
            s_workingFrame = {};
            s_publishedFrame = {};
        }

        void clearCatalogState()
        {
            s_catalog = {};
            s_buildingCatalog = {};
            s_buildingCatalogActive = false;
            s_rebuildRequested = false;
            s_geometryDemandActive = false;
            s_geometryEvidenceIndex = 0;
            s_lastGeometryAdvanceFrame =
                (std::numeric_limits<std::uint64_t>::max)();
            clearPublishedFrame();
        }

        [[nodiscard]] std::int32_t nodeIdFor(
            const std::vector<RE::NiAVObject*>& nodePointers,
            const RE::NiAVObject* object)
        {
            if (!object) {
                return -1;
            }
            for (std::uint32_t index = 0;
                 index < nodePointers.size();
                 ++index) {
                if (nodePointers[index] == object) {
                    return static_cast<std::int32_t>(index);
                }
            }
            return -1;
        }

        void finalizeCatalog()
        {
            auto& building = s_buildingCatalog;
            bool geometryComplete = true;
            std::uint32_t copiedPointCount = 0;
            std::vector<
                reload_observation_policy::EvidenceNodeCandidate>
                evidenceCandidates;
            evidenceCandidates.reserve(building.evidence.size());
            for (auto& evidence : building.evidence) {
                copiedPointCount +=
                    static_cast<std::uint32_t>(evidence.points.size());
                evidence.value.copiedPointCount =
                    static_cast<std::uint32_t>(evidence.points.size());
                if (!evidence.points.empty()) {
                    evidence.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::GeometryPresent);
                }
                if (evidence.value.copiedPointCount ==
                    evidence.value.providerPointCount) {
                    evidence.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::GeometryComplete);
                } else if (building.geometryRequested) {
                    geometryComplete = false;
                    evidence.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::GeometryTruncated);
                } else {
                    geometryComplete = false;
                }
                evidenceCandidates.push_back({
                    .sourceNodeId = evidence.value.sourceNodeId,
                    .interactionNodeId =
                        evidence.value.interactionNodeId,
                });
            }

            std::vector<std::uint8_t> namedNodes(
                building.nodes.size(),
                0);
            for (std::size_t index = 0;
                 index < building.nodes.size();
                 ++index) {
                namedNodes[index] =
                    building.nodes[index].identityName.empty() ? 0 : 1;
            }
            const auto selection =
                reload_observation_policy::selectObservationTargets(
                    static_cast<std::uint32_t>(building.nodes.size()),
                    evidenceCandidates,
                    namedNodes);
            building.targetNodeIds = selection.nodeIds;
            building.targetCount = selection.count;
            building.omittedTargetCount = selection.omittedCount;
            building.targetIndexByNode.fill(-1);
            for (std::uint32_t index = 0;
                 index < building.targetCount;
                 ++index) {
                building.targetIndexByNode[
                    building.targetNodeIds[index]] =
                    static_cast<std::int16_t>(index);
            }

            building.state.nodeCount =
                static_cast<std::uint32_t>(building.nodes.size());
            building.state.evidenceCount =
                static_cast<std::uint32_t>(building.evidence.size());
            building.state.copiedGeometryPointCount = copiedPointCount;
            building.state.statusFlags |= flag(
                PaperReloadCatalogStatusFlagV1::Valid);
            building.state.statusFlags |= geometryComplete ?
                flag(PaperReloadCatalogStatusFlagV1::GeometryComplete) :
                flag(PaperReloadCatalogStatusFlagV1::GeometryIncomplete);
            building.state.catalogSequence =
                nextSequence(s_catalogSequence);
            building.valid = true;

            s_catalog = std::move(building);
            s_buildingCatalog = {};
            s_buildingCatalogActive = false;
            s_geometryEvidenceIndex = 0;
            s_rebuildRequested = false;
            clearPublishedFrame();

            PAPER_LOG_INFO(
                Api,
                "Reload observation catalog published weapon={:08X}/{:016X} sequence={} nodes={}/{} evidence={}/{} targets={} omittedTargets={} geometryPoints={} status=0x{:08X}",
                s_catalog.state.weaponFormId,
                s_catalog.state.weaponGenerationKey,
                s_catalog.state.catalogSequence,
                s_catalog.state.nodeCount,
                s_catalog.state.discoveredNodeCount,
                s_catalog.state.evidenceCount,
                s_catalog.state.reportedEvidenceCount,
                s_catalog.targetCount,
                s_catalog.omittedTargetCount,
                s_catalog.state.copiedGeometryPointCount,
                s_catalog.state.statusFlags);
        }

        [[nodiscard]] bool beginCatalog(
            const rock::provider::
                RockProviderAnimationPhaseContextV1& context,
            const rock::provider::
                RockProviderEquippedWeaponGripStateV1& gripState,
            const std::uint32_t paperProviderGeneration,
            const bool collectEvidenceGeometry)
        {
            auto* root = weaponRoot(gripState);
            if (!root || !finiteTransform(root->world)) {
                return false;
            }

            s_catalog = {};
            s_buildingCatalog = {};
            s_rebuildRequested = false;
            clearPublishedFrame();

            auto& building = s_buildingCatalog;
            building.geometryRequested = collectEvidenceGeometry;
            building.nodes.reserve(256);
            building.evidence.reserve(PAPER_MAX_RELOAD_EVIDENCE_V1);
            building.state.weaponFormId = gripState.weaponFormId;
            building.state.weaponGenerationKey =
                gripState.weaponGenerationKey;
            building.state.capturedFrameIndex = context.frameIndex;
            building.state.worldGeneration = context.worldGeneration;
            building.state.skeletonGeneration =
                context.skeletonGeneration;
            building.state.rockProviderGeneration =
                context.providerGeneration;
            building.state.paperProviderGeneration =
                paperProviderGeneration;
            if (!rockApiClient().reloadObservationEvidenceReady()) {
                building.state.statusFlags |= flag(
                    PaperReloadCatalogStatusFlagV1::
                        EvidenceUnavailable);
            }
            building.state.weapon = describeForm(
                gripState.weaponFormId);
            api_transform::fromNi(
                root->world,
                building.state.weaponRootWorld);

            rock::provider::RockProviderWeaponClassificationV1
                sourceClassification{};
            if (rockApiClient().queryEquippedWeaponClassification(
                    sourceClassification) &&
                sourceClassification.formId == gripState.weaponFormId &&
                sourceClassification.weaponGenerationKey ==
                    gripState.weaponGenerationKey) {
                auto& classification = building.state.classification;
                classification.flags |= flag(
                    PaperWeaponClassificationFlagV1::Available);
                classification.formId = sourceClassification.formId;
                classification.weaponGenerationKey =
                    sourceClassification.weaponGenerationKey;
                classification.keywordFlags =
                    sourceClassification.keywordFlags;
                classification.sizeClass = static_cast<std::uint32_t>(
                    sourceClassification.sizeClass);
                classification.source = static_cast<std::uint32_t>(
                    sourceClassification.source);
                classification.confidence =
                    sourceClassification.confidence;
                classification.provenanceFlags =
                    sourceClassification.provenanceFlags;
                building.state.statusFlags |= flag(
                    PaperReloadCatalogStatusFlagV1::
                        ClassificationAvailable);
                if (sourceClassification.valid != 0) {
                    classification.flags |= flag(
                        PaperWeaponClassificationFlagV1::Valid);
                    building.state.statusFlags |= flag(
                        PaperReloadCatalogStatusFlagV1::
                            ClassificationValid);
                }
            }

            const auto weaponWorld = root->world;
            std::vector<RE::NiAVObject*> nodePointers;
            nodePointers.reserve(256);
            std::uint32_t visitedNodeCount = 0;
            const auto walk = [&](auto&& self,
                                  RE::NiAVObject* object,
                                  const std::int32_t parentNodeId,
                                  const std::uint32_t childIndex,
                                  const std::uint32_t depth,
                                  const std::string& parentPath) -> void {
                if (!object || visitedNodeCount >= kMaxSceneVisits) {
                    if (object) {
                        building.state.statusFlags |= flag(
                            PaperReloadCatalogStatusFlagV1::
                                NodeCatalogTruncated);
                    }
                    return;
                }
                ++visitedNodeCount;
                ++building.state.discoveredNodeCount;

                const char* rawName = object->name.c_str();
                const std::string name = rawName ? rawName : "";
                std::string path = parentPath;
                if (!path.empty()) {
                    path.push_back('/');
                }
                path += name.empty() ? "<unnamed>" : name;
                path.push_back('#');
                path += std::to_string(childIndex);

                std::int32_t thisNodeId = -1;
                if (depth <= kMaxSceneDepth &&
                    building.nodes.size() <
                        PAPER_MAX_RELOAD_CATALOG_NODES_V1) {
                    thisNodeId = static_cast<std::int32_t>(
                        building.nodes.size());
                    NodeRecord record{};
                    record.identityName = name;
                    record.value.nodeId =
                        static_cast<std::uint32_t>(thisNodeId);
                    record.value.parentNodeId = parentNodeId;
                    record.value.childIndex = childIndex;
                    if (copyFixedString(record.value.name, name)) {
                        record.value.flags |= flag(
                            PaperReloadNodeFlagV1::NameTruncated);
                    }
                    if (copyFixedString(
                            record.value.rootRelativePath,
                            path)) {
                        record.value.flags |= flag(
                            PaperReloadNodeFlagV1::PathTruncated);
                    }

                    if (object != root && object->parent) {
                        if (auto* parent = object->parent->IsNode()) {
                            auto& siblings =
                                parent->GetRuntimeData().children;
                            for (std::uint16_t index = 0;
                                 index < siblings.size() &&
                                 index < childIndex;
                                 ++index) {
                                const char* siblingName =
                                    siblings[index] ?
                                        siblings[index]->name.c_str() :
                                        nullptr;
                                if ((siblingName ?
                                         std::string_view(siblingName) :
                                         std::string_view{}) == name) {
                                    ++record.value.
                                        sameNameSiblingOrdinal;
                                }
                            }
                        }
                    }

                    if (auto* asNode = object->IsNode()) {
                        record.value.flags |= flag(
                            PaperReloadNodeFlagV1::IsNode);
                        record.value.childCount =
                            asNode->GetRuntimeData().children.size();
                    }
                    if (finiteTransform(object->local)) {
                        record.value.flags |= flag(
                            PaperReloadNodeFlagV1::
                                LocalTransformValid);
                        api_transform::fromNi(
                            object->local,
                            record.value.baselineLocal);
                    }
                    const auto weaponLocal = relativeTransform(
                        weaponWorld,
                        object->world);
                    if (finiteTransform(weaponLocal)) {
                        record.value.flags |= flag(
                            PaperReloadNodeFlagV1::
                                WeaponLocalTransformValid);
                        api_transform::fromNi(
                            weaponLocal,
                            record.value.baselineWeaponLocal);
                    }
                    building.nodes.push_back(std::move(record));
                    nodePointers.push_back(object);
                } else {
                    ++building.state.omittedNodeCount;
                    building.state.statusFlags |= flag(
                        PaperReloadCatalogStatusFlagV1::
                            NodeCatalogTruncated);
                }

                if (depth >= kMaxSceneDepth) {
                    if (auto* asNode = object->IsNode();
                        asNode &&
                        !asNode->GetRuntimeData().children.empty()) {
                        building.state.omittedNodeCount +=
                            asNode->GetRuntimeData().children.size();
                        building.state.statusFlags |= flag(
                            PaperReloadCatalogStatusFlagV1::
                                NodeCatalogTruncated);
                    }
                    return;
                }
                if (auto* asNode = object->IsNode()) {
                    auto& children = asNode->GetRuntimeData().children;
                    for (std::uint16_t index = 0;
                         index < children.size();
                         ++index) {
                        self(
                            self,
                            children[index].get(),
                            thisNodeId,
                            index,
                            depth + 1,
                            path);
                    }
                }
            };
            walk(walk, root, -1, 0, 0, {});
            if (building.nodes.empty()) {
                s_buildingCatalog = {};
                return false;
            }

            building.state.reportedEvidenceCount =
                rockApiClient().weaponEvidenceDetailCount();
            const auto evidenceCapacity = std::min(
                building.state.reportedEvidenceCount,
                PAPER_MAX_RELOAD_EVIDENCE_V1);
            std::array<
                rock::provider::RockProviderWeaponEvidenceDetailV1,
                PAPER_MAX_RELOAD_EVIDENCE_V1>
                sourceEvidence{};
            const auto copiedEvidence = evidenceCapacity > 0 ?
                std::min(
                    rockApiClient().copyWeaponEvidenceDetails(
                        sourceEvidence.data(),
                        evidenceCapacity),
                    evidenceCapacity) :
                0u;
            if (building.state.reportedEvidenceCount >
                    evidenceCapacity ||
                copiedEvidence <
                    building.state.reportedEvidenceCount) {
                building.state.statusFlags |= flag(
                    PaperReloadCatalogStatusFlagV1::EvidenceTruncated);
            }

            std::uint32_t scheduledPointCount = 0;
            for (std::uint32_t index = 0;
                 index < copiedEvidence;
                 ++index) {
                const auto& source = sourceEvidence[index];
                if (source.weaponGenerationKey !=
                    gripState.weaponGenerationKey) {
                    building.state.statusFlags |= flag(
                        PaperReloadCatalogStatusFlagV1::
                            EvidenceTruncated);
                    continue;
                }

                EvidenceRecord record{};
                record.value.evidenceId =
                    static_cast<std::uint32_t>(
                        building.evidence.size());
                record.value.bodyId = source.bodyId;
                record.value.sourceNodeId = nodeIdFor(
                    nodePointers,
                    reinterpret_cast<RE::NiAVObject*>(
                        source.sourceRoot));
                record.value.interactionNodeId = nodeIdFor(
                    nodePointers,
                    reinterpret_cast<RE::NiAVObject*>(
                        source.interactionRoot));
                if (record.value.sourceNodeId >= 0) {
                    record.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::
                            SourceNodeResolved);
                }
                if (record.value.interactionNodeId >= 0) {
                    record.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::
                            InteractionNodeResolved);
                }
                record.value.partKind = source.partKind;
                record.value.reloadRole = source.reloadRole;
                record.value.supportRole = source.supportRole;
                record.value.socketRole = source.socketRole;
                record.value.actionRole = source.actionRole;
                record.value.fallbackGripPose =
                    source.fallbackGripPose;
                record.value.classificationSource =
                    source.classificationSource;
                if (copyFixedString(
                        record.value.sourceName,
                        fixedStringView(
                            source.sourceName,
                            std::size(source.sourceName)))) {
                    record.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::
                            SourceNameTruncated);
                }
                if (source.localBoundsGame.valid != 0) {
                    record.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::
                            LocalBoundsValid);
                    record.value.localBoundsGame.valid = 1;
                    record.value.localBoundsGame.min = {
                        source.localBoundsGame.min.x,
                        source.localBoundsGame.min.y,
                        source.localBoundsGame.min.z,
                    };
                    record.value.localBoundsGame.max = {
                        source.localBoundsGame.max.x,
                        source.localBoundsGame.max.y,
                        source.localBoundsGame.max.z,
                    };
                }
                record.value.providerPointCount = source.pointCount;
                const auto remainingPointCapacity =
                    scheduledPointCount <
                            PAPER_MAX_RELOAD_EVIDENCE_POINTS_V1 ?
                        PAPER_MAX_RELOAD_EVIDENCE_POINTS_V1 -
                            scheduledPointCount :
                        0u;
                record.scheduledPointCount = collectEvidenceGeometry ?
                    std::min({
                        source.pointCount,
                        PAPER_MAX_RELOAD_EVIDENCE_POINTS_PER_DETAIL_V1,
                        remainingPointCapacity,
                    }) :
                    0u;
                scheduledPointCount += record.scheduledPointCount;
                if (collectEvidenceGeometry &&
                    record.scheduledPointCount < source.pointCount) {
                    record.value.flags |= flag(
                        PaperReloadEvidenceFlagV1::
                            GeometryTruncated);
                }
                record.value.omod = describeForm(source.omodFormId);
                record.value.attachPoint =
                    describeForm(source.attachPointFormId);
                building.evidence.push_back(std::move(record));
            }

            s_buildingCatalogActive = true;
            s_geometryEvidenceIndex = 0;
            s_lastGeometryAdvanceFrame =
                (std::numeric_limits<std::uint64_t>::max)();
            if (building.evidence.empty() ||
                scheduledPointCount == 0) {
                finalizeCatalog();
            }
            return true;
        }

        void advanceGeometry(const std::uint64_t frameIndex)
        {
            if (!s_buildingCatalogActive ||
                s_lastGeometryAdvanceFrame == frameIndex) {
                return;
            }
            s_lastGeometryAdvanceFrame = frameIndex;

            while (s_geometryEvidenceIndex <
                s_buildingCatalog.evidence.size()) {
                auto& evidence = s_buildingCatalog.evidence[
                    s_geometryEvidenceIndex];
                if (evidence.scheduledPointCount == 0) {
                    ++s_geometryEvidenceIndex;
                    continue;
                }

                std::array<
                    rock::provider::RockProviderPoint3,
                    PAPER_MAX_RELOAD_EVIDENCE_POINTS_PER_DETAIL_V1>
                    sourcePoints{};
                const auto copied = std::min(
                    rockApiClient().copyWeaponEvidencePoints(
                        evidence.value.bodyId,
                        sourcePoints.data(),
                        evidence.scheduledPointCount),
                    evidence.scheduledPointCount);
                if (copied < evidence.scheduledPointCount &&
                    ++evidence.shortCopyAttempts <
                        kMaxGeometryCopyAttempts) {
                    return;
                }

                evidence.points.resize(copied);
                for (std::uint32_t index = 0;
                     index < copied;
                     ++index) {
                    evidence.points[index] = {
                        sourcePoints[index].x,
                        sourcePoints[index].y,
                        sourcePoints[index].z,
                    };
                }
                ++s_geometryEvidenceIndex;
                break;
            }

            if (s_geometryEvidenceIndex >=
                s_buildingCatalog.evidence.size()) {
                finalizeCatalog();
            }
        }

        void beginWorkingFrame(
            const rock::provider::
                RockProviderAnimationPhaseContextV1& context)
        {
            if (s_workingFrame.active &&
                s_workingFrame.state.frameIndex == context.frameIndex &&
                s_workingFrame.state.catalogSequence ==
                    s_catalog.state.catalogSequence) {
                return;
            }
            s_workingFrame = {};
            if (!s_catalog.valid) {
                return;
            }
            auto& state = s_workingFrame.state;
            state.statusFlags =
                flag(PaperReloadFrameStatusFlagV1::Valid) |
                flag(PaperReloadFrameStatusFlagV1::CatalogValid);
            if (s_catalog.omittedTargetCount > 0) {
                state.statusFlags |= flag(
                    PaperReloadFrameStatusFlagV1::
                        ObservationsTruncated);
            }
            state.weaponFormId = s_catalog.state.weaponFormId;
            state.weaponGenerationKey =
                s_catalog.state.weaponGenerationKey;
            state.catalogSequence = s_catalog.state.catalogSequence;
            state.frameIndex = context.frameIndex;
            state.deltaSeconds = context.deltaSeconds;
            state.worldGeneration = context.worldGeneration;
            state.skeletonGeneration = context.skeletonGeneration;
            state.rockProviderGeneration = context.providerGeneration;
            state.paperProviderGeneration =
                s_catalog.state.paperProviderGeneration;
            state.observationTargetCount = s_catalog.targetCount;
            state.omittedObservationTargetCount =
                s_catalog.omittedTargetCount;
            s_workingFrame.active = true;
        }

        [[nodiscard]] PhaseCapture captureCurrentPhase(
            const rock::provider::
                RockProviderAnimationPhaseContextV1& context,
            const rock::provider::
                RockProviderEquippedWeaponGripStateV1& gripState,
            const PaperReloadObservationPhaseV1 phase)
        {
            PhaseCapture result{};
            auto* root = weaponRoot(gripState);
            if (!root || !finiteTransform(root->world)) {
                return result;
            }

            std::array<
                PaperReloadNodeObservationV1,
                PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1>
                captured{};
            std::array<
                std::uint8_t,
                PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1>
                present{};
            const auto weaponWorld = root->world;
            std::uint32_t catalogIndex = 0;
            std::uint32_t visitedCount = 0;
            bool mismatch = false;
            bool stop = false;

            const auto walk = [&](auto&& self,
                                  RE::NiAVObject* object,
                                  const std::int32_t parentNodeId,
                                  const std::uint32_t childIndex,
                                  const std::uint32_t depth) -> void {
                if (!object || mismatch || stop ||
                    visitedCount >= kMaxSceneVisits) {
                    if (visitedCount >= kMaxSceneVisits) {
                        mismatch = true;
                    }
                    return;
                }
                ++visitedCount;
                if (catalogIndex >= s_catalog.nodes.size()) {
                    if ((s_catalog.state.statusFlags & flag(
                            PaperReloadCatalogStatusFlagV1::
                                NodeCatalogTruncated)) != 0) {
                        stop = true;
                    } else {
                        mismatch = true;
                    }
                    return;
                }

                const auto& expected = s_catalog.nodes[catalogIndex];
                const char* rawName = object->name.c_str();
                const std::string_view name = rawName ? rawName : "";
                if (expected.value.parentNodeId != parentNodeId ||
                    expected.value.childIndex != childIndex ||
                    expected.identityName != name) {
                    mismatch = true;
                    return;
                }
                const auto nodeId = catalogIndex++;
                const auto targetIndex =
                    s_catalog.targetIndexByNode[nodeId];
                if (targetIndex >= 0) {
                    PaperReloadNodeObservationV1 observation{};
                    observation.nodeId = nodeId;
                    observation.phase = phase;
                    observation.frameIndex = context.frameIndex;
                    if (finiteTransform(object->local)) {
                        observation.flags |= flag(
                            PaperReloadNodeObservationFlagV1::
                                LocalTransformValid);
                        api_transform::fromNi(
                            object->local,
                            observation.local);
                    }
                    const auto weaponLocal = relativeTransform(
                        weaponWorld,
                        object->world);
                    if (finiteTransform(weaponLocal)) {
                        observation.flags |= flag(
                            PaperReloadNodeObservationFlagV1::
                                WeaponLocalTransformValid);
                        api_transform::fromNi(
                            weaponLocal,
                            observation.weaponLocal);
                    }
                    captured[static_cast<std::size_t>(targetIndex)] =
                        observation;
                    present[static_cast<std::size_t>(targetIndex)] = 1;
                }

                if (depth >= kMaxSceneDepth) {
                    if (auto* asNode = object->IsNode();
                        asNode &&
                        !asNode->GetRuntimeData().children.empty() &&
                        (s_catalog.state.statusFlags & flag(
                            PaperReloadCatalogStatusFlagV1::
                                NodeCatalogTruncated)) == 0) {
                        mismatch = true;
                    }
                    return;
                }
                if (auto* asNode = object->IsNode()) {
                    auto& children = asNode->GetRuntimeData().children;
                    for (std::uint16_t index = 0;
                         index < children.size();
                         ++index) {
                        self(
                            self,
                            children[index].get(),
                            static_cast<std::int32_t>(nodeId),
                            index,
                            depth + 1);
                    }
                }
            };
            walk(walk, root, -1, 0, 0);

            const bool catalogWasTruncated =
                (s_catalog.state.statusFlags & flag(
                    PaperReloadCatalogStatusFlagV1::
                        NodeCatalogTruncated)) != 0;
            if (!catalogWasTruncated &&
                catalogIndex != s_catalog.nodes.size()) {
                mismatch = true;
            }
            if (mismatch) {
                s_workingFrame.state.statusFlags |= flag(
                    PaperReloadFrameStatusFlagV1::TopologyMismatch);
                s_rebuildRequested = true;
                return result;
            }

            for (std::uint32_t index = 0;
                 index < s_catalog.targetCount;
                 ++index) {
                if (present[index] != 0) {
                    result.observations[result.count++] = captured[index];
                }
            }
            result.captured = true;
            api_transform::fromNi(
                root->world,
                s_workingFrame.state.weaponRootWorld);
            return result;
        }

        template <class Value>
        [[nodiscard]] PaperResultV1 copyRange(
            const std::vector<Value>& source,
            const std::uint32_t first,
            Value* destination,
            const std::uint32_t capacity,
            std::uint32_t& copied)
        {
            copied = 0;
            if (first > source.size()) {
                return PaperResultV1::OutOfRange;
            }
            if (capacity == 0) {
                return PaperResultV1::Ok;
            }
            if (!destination) {
                return PaperResultV1::InvalidArgument;
            }
            const auto count = std::min<std::size_t>(
                capacity,
                source.size() - first);
            for (std::size_t index = 0; index < count; ++index) {
                destination[index] = source[first + index];
            }
            copied = static_cast<std::uint32_t>(count);
            return PaperResultV1::Ok;
        }
    }

    void reset()
    {
        clearCatalogState();
    }

    void advanceFrame(
        const rock::provider::
            RockProviderAnimationPhaseContextV1& context,
        const rock::provider::
            RockProviderEquippedWeaponGripStateV1* gripState,
        const std::uint32_t paperProviderGeneration,
        const bool collectEvidenceGeometry)
    {
        if (!gripState || !validWeaponGrip(*gripState)) {
            clearCatalogState();
            return;
        }

        if (s_geometryDemandActive != collectEvidenceGeometry) {
            s_geometryDemandActive = collectEvidenceGeometry;
            s_rebuildRequested = true;
        }

        const bool publishedMatches =
            s_catalog.valid &&
            s_catalog.state.weaponFormId == gripState->weaponFormId &&
            s_catalog.state.weaponGenerationKey ==
                gripState->weaponGenerationKey &&
            s_catalog.state.worldGeneration == context.worldGeneration &&
            s_catalog.state.skeletonGeneration ==
                context.skeletonGeneration &&
            s_catalog.state.rockProviderGeneration ==
                context.providerGeneration;
        const bool buildingMatches =
            s_buildingCatalogActive &&
            s_buildingCatalog.state.weaponFormId ==
                gripState->weaponFormId &&
            s_buildingCatalog.state.weaponGenerationKey ==
                gripState->weaponGenerationKey &&
            s_buildingCatalog.state.worldGeneration ==
                context.worldGeneration &&
            s_buildingCatalog.state.skeletonGeneration ==
                context.skeletonGeneration &&
            s_buildingCatalog.state.rockProviderGeneration ==
                context.providerGeneration;

        if ((!publishedMatches && !buildingMatches) ||
            s_rebuildRequested) {
            if (!beginCatalog(
                    context,
                    *gripState,
                    paperProviderGeneration,
                    collectEvidenceGeometry)) {
                clearCatalogState();
                return;
            }
        }
        advanceGeometry(context.frameIndex);
        beginWorkingFrame(context);
    }

    void capturePhase(
        const rock::provider::
            RockProviderAnimationPhaseContextV1& context,
        const rock::provider::
            RockProviderEquippedWeaponGripStateV1& gripState,
        const PaperReloadObservationPhaseV1 phase)
    {
        if (!s_catalog.valid || !validWeaponGrip(gripState) ||
            s_catalog.state.weaponFormId != gripState.weaponFormId ||
            s_catalog.state.weaponGenerationKey !=
                gripState.weaponGenerationKey ||
            s_catalog.state.worldGeneration != context.worldGeneration ||
            s_catalog.state.skeletonGeneration !=
                context.skeletonGeneration ||
            s_catalog.state.rockProviderGeneration !=
                context.providerGeneration) {
            return;
        }

        beginWorkingFrame(context);
        if (!s_workingFrame.active) {
            return;
        }
        auto captured = captureCurrentPhase(
            context,
            gripState,
            phase);
        if (phase ==
            PaperReloadObservationPhaseV1::NativeGraphOutput) {
            s_workingFrame.nativeGraphOutput = std::move(captured);
            if (s_workingFrame.nativeGraphOutput.captured) {
                s_workingFrame.state.statusFlags |= flag(
                    PaperReloadFrameStatusFlagV1::
                        NativeGraphOutputCaptured);
            }
        } else {
            s_workingFrame.postRock = std::move(captured);
            if (s_workingFrame.postRock.captured) {
                s_workingFrame.state.statusFlags |= flag(
                    PaperReloadFrameStatusFlagV1::PostRockCaptured);
            }
        }
    }

    void completeFrame(
        const rock::provider::
            RockProviderAnimationPhaseContextV1& context,
        const std::uint32_t paperProviderGeneration)
    {
        if (!s_catalog.valid || !s_workingFrame.active ||
            s_workingFrame.state.frameIndex != context.frameIndex ||
            s_workingFrame.state.catalogSequence !=
                s_catalog.state.catalogSequence) {
            s_publishedFrame = {};
            s_workingFrame = {};
            return;
        }

        auto published = PublishedFrame{};
        published.state = s_workingFrame.state;
        published.state.paperProviderGeneration =
            paperProviderGeneration;
        published.state.snapshotSequence =
            nextSequence(s_snapshotSequence);
        published.state.nativeGraphOutputCount =
            s_workingFrame.nativeGraphOutput.count;
        published.state.postRockCount =
            s_workingFrame.postRock.count;

        std::uint32_t outputIndex = 0;
        for (std::uint32_t index = 0;
             index < s_workingFrame.nativeGraphOutput.count;
             ++index) {
            published.observations[outputIndex++] =
                s_workingFrame.nativeGraphOutput.observations[index];
        }
        for (std::uint32_t index = 0;
             index < s_workingFrame.postRock.count;
             ++index) {
            published.observations[outputIndex++] =
                s_workingFrame.postRock.observations[index];
        }
        published.state.observationCount = outputIndex;
        published.valid = true;
        s_publishedFrame = std::move(published);
        s_workingFrame = {};
    }

    PaperResultV1 getLimits(
        PaperReloadObservationLimitsV1& outLimits)
    {
        PaperReloadObservationLimitsV1 result{};
        result.featureBits = PAPER_PROVIDER_FEATURE_BITS_V1;
        result.maxCatalogNodes = PAPER_MAX_RELOAD_CATALOG_NODES_V1;
        result.maxEvidenceRecords = PAPER_MAX_RELOAD_EVIDENCE_V1;
        result.maxObservationTargets =
            PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1;
        result.maxNodeObservations =
            PAPER_MAX_RELOAD_NODE_OBSERVATIONS_V1;
        result.maxEvidencePointsPerDetail =
            PAPER_MAX_RELOAD_EVIDENCE_POINTS_PER_DETAIL_V1;
        result.maxEvidencePointsTotal =
            PAPER_MAX_RELOAD_EVIDENCE_POINTS_V1;
        result.nodeNameCapacity = PAPER_RELOAD_NODE_NAME_CAPACITY_V1;
        result.nodePathCapacity = PAPER_RELOAD_NODE_PATH_CAPACITY_V1;
        result.pluginNameCapacity = PAPER_FORM_PLUGIN_NAME_CAPACITY_V1;
        result.editorIdCapacity = PAPER_FORM_EDITOR_ID_CAPACITY_V1;
        result.displayNameCapacity = PAPER_FORM_DISPLAY_NAME_CAPACITY_V1;
        outLimits = result;
        return PaperResultV1::Ok;
    }

    PaperResultV1 getCatalogState(
        PaperReloadCatalogStateV1& outState)
    {
        if (!s_catalog.valid) {
            outState = {};
            return PaperResultV1::NotReady;
        }
        outState = s_catalog.state;
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyCatalogNodes(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstNode,
        PaperReloadNodeCatalogEntryV1* outNodes,
        const std::uint32_t maxNodes,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid) {
            return PaperResultV1::NotReady;
        }
        if (catalogSequence != s_catalog.state.catalogSequence) {
            return PaperResultV1::StaleSnapshot;
        }
        if (firstNode > s_catalog.nodes.size()) {
            return PaperResultV1::OutOfRange;
        }
        if (maxNodes == 0) {
            return PaperResultV1::Ok;
        }
        if (!outNodes) {
            return PaperResultV1::InvalidArgument;
        }
        const auto count = std::min<std::size_t>(
            maxNodes,
            s_catalog.nodes.size() - firstNode);
        for (std::size_t index = 0; index < count; ++index) {
            outNodes[index] = s_catalog.nodes[firstNode + index].value;
        }
        outCopied = static_cast<std::uint32_t>(count);
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyEvidence(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstEvidence,
        PaperReloadEvidenceV1* outEvidence,
        const std::uint32_t maxEvidence,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid) {
            return PaperResultV1::NotReady;
        }
        if (catalogSequence != s_catalog.state.catalogSequence) {
            return PaperResultV1::StaleSnapshot;
        }
        if (firstEvidence > s_catalog.evidence.size()) {
            return PaperResultV1::OutOfRange;
        }
        if (maxEvidence == 0) {
            return PaperResultV1::Ok;
        }
        if (!outEvidence) {
            return PaperResultV1::InvalidArgument;
        }
        const auto count = std::min<std::size_t>(
            maxEvidence,
            s_catalog.evidence.size() - firstEvidence);
        for (std::size_t index = 0; index < count; ++index) {
            outEvidence[index] =
                s_catalog.evidence[firstEvidence + index].value;
        }
        outCopied = static_cast<std::uint32_t>(count);
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyEvidencePoints(
        const std::uint64_t catalogSequence,
        const std::uint32_t evidenceId,
        const std::uint32_t firstPoint,
        PaperPoint3V1* outPoints,
        const std::uint32_t maxPoints,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid) {
            return PaperResultV1::NotReady;
        }
        if (catalogSequence != s_catalog.state.catalogSequence) {
            return PaperResultV1::StaleSnapshot;
        }
        if (evidenceId >= s_catalog.evidence.size()) {
            return PaperResultV1::OutOfRange;
        }
        return copyRange(
            s_catalog.evidence[evidenceId].points,
            firstPoint,
            outPoints,
            maxPoints,
            outCopied);
    }

    PaperResultV1 getFrameState(
        PaperReloadFrameStateV1& outState)
    {
        if (!s_publishedFrame.valid) {
            outState = {};
            return PaperResultV1::NotReady;
        }
        outState = s_publishedFrame.state;
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyNodeObservations(
        const std::uint64_t snapshotSequence,
        const std::uint32_t firstObservation,
        PaperReloadNodeObservationV1* outObservations,
        const std::uint32_t maxObservations,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_publishedFrame.valid) {
            return PaperResultV1::NotReady;
        }
        if (snapshotSequence !=
            s_publishedFrame.state.snapshotSequence) {
            return PaperResultV1::StaleSnapshot;
        }
        if (firstObservation >
            s_publishedFrame.state.observationCount) {
            return PaperResultV1::OutOfRange;
        }
        if (maxObservations == 0) {
            return PaperResultV1::Ok;
        }
        if (!outObservations) {
            return PaperResultV1::InvalidArgument;
        }
        const auto count = std::min<std::uint32_t>(
            maxObservations,
            s_publishedFrame.state.observationCount -
                firstObservation);
        for (std::uint32_t index = 0; index < count; ++index) {
            outObservations[index] = s_publishedFrame.observations[
                firstObservation + index];
        }
        outCopied = count;
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyEvidenceMotionSources(
        EvidenceMotionSource* outSources,
        const std::uint32_t maxSources,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid || !s_publishedFrame.valid) {
            return PaperResultV1::NotReady;
        }
        if (maxSources == 0) {
            return PaperResultV1::Ok;
        }
        if (!outSources) {
            return PaperResultV1::InvalidArgument;
        }

        const auto observationForNode = [](const std::int32_t nodeId)
            -> const PaperReloadNodeObservationV1* {
            if (nodeId < 0) {
                return nullptr;
            }
            for (std::uint32_t index = 0;
                 index < s_publishedFrame.state.observationCount;
                 ++index) {
                const auto& observation =
                    s_publishedFrame.observations[index];
                if (observation.nodeId ==
                        static_cast<std::uint32_t>(nodeId) &&
                    observation.phase ==
                        PaperReloadObservationPhaseV1::
                            NativeGraphOutput) {
                    return std::addressof(observation);
                }
            }
            return nullptr;
        };

        const auto count = (std::min)(
            maxSources,
            static_cast<std::uint32_t>(s_catalog.evidence.size()));
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto& evidence = s_catalog.evidence[index].value;
            EvidenceMotionSource source{};
            source.evidenceId = evidence.evidenceId;
            source.bodyId = evidence.bodyId;
            source.sourceNodeId = evidence.sourceNodeId;
            source.interactionNodeId = evidence.interactionNodeId;
            source.partKind = evidence.partKind;
            source.actionRole = evidence.actionRole;

            const PaperReloadNodeObservationV1* observation = nullptr;
            if (evidence.sourceNodeId >= 0) {
                source.selectedNodeId = evidence.sourceNodeId;
                source.flags |= static_cast<std::uint32_t>(
                    EvidenceMotionSourceFlag::SourceNodeSelected);
                observation = observationForNode(evidence.sourceNodeId);
            }
            if (!observation && evidence.interactionNodeId >= 0) {
                source.selectedNodeId = evidence.interactionNodeId;
                source.flags &= ~static_cast<std::uint32_t>(
                    EvidenceMotionSourceFlag::SourceNodeSelected);
                source.flags |= static_cast<std::uint32_t>(
                    EvidenceMotionSourceFlag::InteractionNodeFallback);
                observation = observationForNode(
                    evidence.interactionNodeId);
            }

            if (source.selectedNodeId >= 0 &&
                static_cast<std::size_t>(source.selectedNodeId) <
                    s_catalog.nodes.size()) {
                const auto& node = s_catalog.nodes[
                    static_cast<std::size_t>(source.selectedNodeId)].value;
                if ((node.flags & flag(
                        PaperReloadNodeFlagV1::
                            WeaponLocalTransformValid)) != 0) {
                    source.baselineWeaponLocal =
                        node.baselineWeaponLocal;
                    source.flags |= static_cast<std::uint32_t>(
                        EvidenceMotionSourceFlag::BaselineValid);
                }
            }
            if (observation &&
                (observation->flags & flag(
                    PaperReloadNodeObservationFlagV1::
                        WeaponLocalTransformValid)) != 0) {
                source.currentWeaponLocal = observation->weaponLocal;
                source.flags |= static_cast<std::uint32_t>(
                    EvidenceMotionSourceFlag::CurrentValid);
            }
            outSources[index] = source;
        }
        outCopied = count;
        return PaperResultV1::Ok;
    }
}
