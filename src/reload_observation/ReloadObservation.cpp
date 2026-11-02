#include "PCH.h"

#include "reload_observation/ReloadObservation.h"

#include "api/ApiTransform.h"
#include "api/RockApiClient.h"
#include "PaperLog.h"
#include "reload_observation/ReloadObservationPolicy.h"
#include "reload_observation/WeaponClassificationPolicy.h"
#include "support/Fo4VrRuntime.h"
#include "support/TransformMath.h"

#include "RE/Bethesda/TESBoundObjects.h"

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
            const rock::api::weapon::GripFlag value)
        {
            return (flags & flag(value)) != 0;
        }

        [[nodiscard]] bool validWeaponGrip(
            const paper::RockWeaponGripState& gripState)
        {
            return hasGripFlag(
                       gripState.flags,
                       rock::api::weapon::GripFlag::Valid) &&
                   gripState.weaponFormId != 0 &&
                   gripState.weaponGenerationKey != 0 &&
                   gripState.weaponNode != 0;
        }

        [[nodiscard]] RE::NiNode* weaponRoot(
            const paper::RockWeaponGripState& gripState)
        {
            if (!validWeaponGrip(gripState)) {
                return nullptr;
            }
            auto* object = reinterpret_cast<RE::NiAVObject*>(
                gripState.weaponNode);
            return object ? object->IsNode() : nullptr;
        }

        [[nodiscard]] FormIdentity describeForm(
            const std::uint32_t runtimeFormId)
        {
            FormIdentity identity{};
            identity.runtimeFormId = runtimeFormId;
            if (runtimeFormId == 0) {
                return identity;
            }

            auto* form = RE::TESForm::GetFormByID(runtimeFormId);
            if (!form) {
                return identity;
            }
            identity.flags |= flag(FormIdentityFlag::Resolved);
            identity.formType =
                static_cast<std::uint32_t>(form->GetFormType());

            if (auto* file = form->GetFile(0)) {
                const auto filename = file->GetFilename();
                if (!filename.empty()) {
                    identity.localFormId = form->GetLocalFormID();
                    identity.flags |= flag(
                        FormIdentityFlag::PluginIdentityValid);
                    if (copyFixedString(
                            identity.pluginName,
                            filename)) {
                        identity.flags |= flag(
                            FormIdentityFlag::
                                PluginNameTruncated);
                    }
                }
            }

            if (const char* editorId = form->GetFormEditorID();
                editorId && editorId[0] != '\0') {
                identity.flags |= flag(
                    FormIdentityFlag::EditorIdValid);
                if (copyFixedString(identity.editorId, editorId)) {
                    identity.flags |= flag(
                        FormIdentityFlag::EditorIdTruncated);
                }
            }

            const auto fullName = RE::TESFullName::GetFullName(*form);
            if (!fullName.empty()) {
                identity.flags |= flag(
                    FormIdentityFlag::DisplayNameValid);
                if (copyFixedString(identity.displayName, fullName)) {
                    identity.flags |= flag(
                        FormIdentityFlag::
                            DisplayNameTruncated);
                }
            }
            return identity;
        }

        struct RuntimeWeaponFamilySignals
        {
            bool weaponDataAvailable{ false };
            bool automaticWeaponFlag{ false };
            bool boltActionWeaponFlag{ false };
            bool revolverAnimationKeyword{ false };
        };

        [[nodiscard]] bool hasRevolverAnimationKeyword(
            const RE::BGSKeywordForm* keywords)
        {
            if (!keywords) {
                return false;
            }
            bool matched = false;
            keywords->ForEachKeyword(
                [&](const RE::BGSKeyword* keyword) {
                    const char* editorId =
                        keyword ? keyword->formEditorID.c_str() : nullptr;
                    const std::string_view value = editorId ?
                        std::string_view{ editorId } :
                        std::string_view{};
                    if (weapon_classification_policy::containsIgnoreCase(
                            value,
                            "revolver") ||
                        weapon_classification_policy::containsIgnoreCase(
                            value,
                            "anims44")) {
                        matched = true;
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            return matched;
        }

        [[nodiscard]] RuntimeWeaponFamilySignals runtimeWeaponFamilySignals(
            const std::uint32_t weaponFormId)
        {
            RuntimeWeaponFamilySignals result{};
            auto* form = RE::TESForm::GetFormByID(weaponFormId);
            auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
            if (!weapon) {
                return result;
            }

            const RE::TESObjectWEAP::InstanceData* weaponData =
                &weapon->weaponData;
            if (auto* equipData = f4vr::getEquippedItem();
                equipData && equipData->item.object == weapon &&
                equipData->item.instanceData) {
                weaponData = static_cast<RE::TESObjectWEAP::InstanceData*>(
                    equipData->item.instanceData.get());
            }
            if (!weaponData) {
                return result;
            }

            result.weaponDataAvailable = true;
            result.automaticWeaponFlag = weaponData->flags.any(
                RE::WEAPON_FLAGS::kAutomatic);
            result.boltActionWeaponFlag = weaponData->flags.any(
                RE::WEAPON_FLAGS::kBoltAction);
            result.revolverAnimationKeyword =
                hasRevolverAnimationKeyword(weapon) ||
                hasRevolverAnimationKeyword(weaponData->keywords);
            return result;
        }

        struct NodeRecord
        {
            ReloadNodeCatalogEntry value{};
            std::string identityName;
        };

        struct EvidenceRecord
        {
            ReloadEvidence value{};
            std::vector<Point3> points;
            std::uint32_t scheduledPointCount{ 0 };
            std::uint32_t shortCopyAttempts{ 0 };
        };

        struct CatalogBuffer
        {
            ReloadCatalogState state{};
            std::vector<NodeRecord> nodes;
            std::vector<EvidenceRecord> evidence;
            std::array<
                std::uint32_t,
                kMaxReloadObservationTargets>
                targetNodeIds{};
            std::array<
                std::int16_t,
                kMaxReloadCatalogNodes>
                targetIndexByNode{};
            std::uint32_t targetCount{ 0 };
            std::uint32_t omittedTargetCount{ 0 };
            bool geometryRequested{ false };
            bool valid{ false };
        };

        void classifyCatalogWeaponFamily(CatalogBuffer& building)
        {
            auto& classification = building.state.classification;
            const auto runtimeSignals = runtimeWeaponFamilySignals(
                building.state.weaponFormId);
            weapon_classification_policy::WeaponFamilySignals signals{
                .rockClassificationAvailable =
                    (classification.flags & flag(
                        WeaponClassificationFlag::Available)) != 0,
                .rockClassificationValid =
                    (classification.flags & flag(
                        WeaponClassificationFlag::Valid)) != 0,
                .keywordFlags = classification.keywordFlags,
                .sizeClass = classification.sizeClass,
                .weaponDataAvailable =
                    runtimeSignals.weaponDataAvailable,
                .automaticWeaponFlag =
                    runtimeSignals.automaticWeaponFlag,
                .boltActionWeaponFlag =
                    runtimeSignals.boltActionWeaponFlag,
                .revolverAnimationKeyword =
                    runtimeSignals.revolverAnimationKeyword,
                .partEvidenceComplete =
                    building.state.reportedEvidenceCount > 0 &&
                    building.evidence.size() ==
                        building.state.reportedEvidenceCount &&
                    (building.state.statusFlags & flag(
                        ReloadCatalogStatusFlag::
                            EvidenceUnavailable)) == 0 &&
                    (building.state.statusFlags & flag(
                        ReloadCatalogStatusFlag::
                            EvidenceTruncated)) == 0,
                .pluginName = fixedStringView(
                    building.state.weapon.pluginName,
                    std::size(building.state.weapon.pluginName)),
                .editorId = fixedStringView(
                    building.state.weapon.editorId,
                    std::size(building.state.weapon.editorId)),
                .displayName = fixedStringView(
                    building.state.weapon.displayName,
                    std::size(building.state.weapon.displayName)),
            };

            for (const auto& record : building.evidence) {
                const auto partKind = static_cast<rock::api::weaponparts::PartKind>(record.value.partKind);
                switch (partKind) {
                case rock::api::weaponparts::PartKind::Magazine:
                    signals.magazinePart = true;
                    break;
                case rock::api::weaponparts::PartKind::Cylinder:
                    signals.cylinderPart = true;
                    break;
                case rock::api::weaponparts::PartKind::Slide:
                    signals.slidePart = true;
                    break;
                case rock::api::weaponparts::PartKind::Stock:
                    signals.stockPart = true;
                    break;
                case rock::api::weaponparts::PartKind::Handguard:
                    signals.handguardPart = true;
                    break;
                case rock::api::weaponparts::PartKind::Foregrip:
                    signals.foregripPart = true;
                    break;
                case rock::api::weaponparts::PartKind::Lever:
                    signals.leverPart = true;
                    break;
                case rock::api::weaponparts::PartKind::Pump:
                    signals.pumpPart = true;
                    break;
                case rock::api::weaponparts::PartKind::BreakAction:
                    signals.breakActionPart = true;
                    break;
                case rock::api::weaponparts::PartKind::LaserCell:
                    signals.laserCellPart = true;
                    break;
                case rock::api::weaponparts::PartKind::Shell:
                    signals.shellPart = true;
                    break;
                case rock::api::weaponparts::PartKind::Round:
                    signals.looseRoundPart = true;
                    break;
                default:
                    break;
                }

                const auto actionRole = static_cast<rock::api::weaponparts::ActionRole>(record.value.actionRole);
                switch (actionRole) {
                case rock::api::weaponparts::ActionRole::Slide:
                    signals.slidePart = true;
                    break;
                case rock::api::weaponparts::ActionRole::Cylinder:
                    signals.cylinderPart = true;
                    break;
                case rock::api::weaponparts::ActionRole::Lever:
                    signals.leverPart = true;
                    break;
                case rock::api::weaponparts::ActionRole::Pump:
                    signals.pumpPart = true;
                    break;
                case rock::api::weaponparts::ActionRole::BreakAction:
                    signals.breakActionPart = true;
                    break;
                default:
                    break;
                }
            }

            const auto result =
                weapon_classification_policy::classifyWeaponFamily(signals);
            classification.familyFlags = result.familyFlags;
            classification.primaryFamily = result.primaryFamily;
            classification.familyEvidenceFlags = result.evidenceFlags;
            if (result.familyFlags != 0) {
                classification.flags |= flag(
                    WeaponClassificationFlag::FamilyAvailable);
                building.state.statusFlags |= flag(
                    ReloadCatalogStatusFlag::
                        FamilyClassificationAvailable);
            }
            if (result.primaryFamily !=
                WeaponPrimaryFamily::Unknown) {
                classification.flags |= flag(
                    WeaponClassificationFlag::PrimaryFamilyValid);
            }
        }

        struct PhaseCapture
        {
            std::array<
                ReloadNodeObservation,
                kMaxReloadObservationTargets>
                observations{};
            std::uint32_t count{ 0 };
            bool captured{ false };
        };

        struct WorkingFrame
        {
            ReloadFrameState state{};
            PhaseCapture nativeGraphOutput{};
            PhaseCapture postRock{};
            bool active{ false };
        };

        struct PublishedFrame
        {
            ReloadFrameState state{};
            std::array<
                ReloadNodeObservation,
                kMaxReloadNodeObservations>
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
                        ReloadEvidenceFlag::GeometryPresent);
                }
                if (evidence.value.copiedPointCount ==
                    evidence.value.providerPointCount) {
                    evidence.value.flags |= flag(
                        ReloadEvidenceFlag::GeometryComplete);
                } else if (building.geometryRequested) {
                    geometryComplete = false;
                    evidence.value.flags |= flag(
                        ReloadEvidenceFlag::GeometryTruncated);
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
                ReloadCatalogStatusFlag::Valid);
            building.state.statusFlags |= geometryComplete ?
                flag(ReloadCatalogStatusFlag::GeometryComplete) :
                flag(ReloadCatalogStatusFlag::GeometryIncomplete);
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
            const rock::api::animation::AnimationPhaseContext& context,
            const paper::RockWeaponGripState& gripState,
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
            building.evidence.reserve(kMaxReloadEvidence);
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
                    ReloadCatalogStatusFlag::
                        EvidenceUnavailable);
            }
            building.state.weapon = describeForm(
                gripState.weaponFormId);
            building.state.classification.formId =
                gripState.weaponFormId;
            building.state.classification.weaponGenerationKey =
                gripState.weaponGenerationKey;
            api_transform::fromNi(
                root->world,
                building.state.weaponRootWorld);

            rock::api::weapon::Classification
                sourceClassification{};
            if (rockApiClient().queryEquippedWeaponClassification(
                    sourceClassification) &&
                sourceClassification.weaponFormId == gripState.weaponFormId &&
                sourceClassification.weaponGenerationKey ==
                    gripState.weaponGenerationKey) {
                auto& classification = building.state.classification;
                classification.flags |= flag(
                    WeaponClassificationFlag::Available);
                classification.formId = sourceClassification.weaponFormId;
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
                    ReloadCatalogStatusFlag::
                        ClassificationAvailable);
                if (sourceClassification.valid != 0) {
                    classification.flags |= flag(
                        WeaponClassificationFlag::Valid);
                    building.state.statusFlags |= flag(
                        ReloadCatalogStatusFlag::
                            ClassificationValid);
                }
            }

            std::vector<RE::NiAVObject*> nodePointers;
            nodePointers.reserve(256);
            std::uint32_t visitedNodeCount = 0;
            const auto identity =
                transform_math::identityTransform<RE::NiTransform>();
            const auto walk = [&](auto&& self,
                                  RE::NiAVObject* object,
                                  const std::int32_t parentNodeId,
                                  const std::uint32_t childIndex,
                                  const std::uint32_t depth,
                                  const std::string& parentPath,
                                  const RE::NiTransform& parentWeaponLocal,
                                  const bool parentWeaponLocalValid) -> void {
                if (!object || visitedNodeCount >= kMaxSceneVisits) {
                    if (object) {
                        building.state.statusFlags |= flag(
                            ReloadCatalogStatusFlag::
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

                const bool localValid = finiteTransform(object->local);
                bool weaponLocalValid = object == root ||
                    (parentWeaponLocalValid && localValid);
                RE::NiTransform weaponLocal = identity;
                if (weaponLocalValid) {
                    weaponLocal = reload_observation_policy::
                        resolveWeaponLocalFromGraph(
                            object == root,
                            identity,
                            parentWeaponLocal,
                            object->local,
                            [](const RE::NiTransform& parent,
                               const RE::NiTransform& child) {
                                return transform_math::composeTransforms(
                                    parent,
                                    child);
                            });
                    weaponLocalValid = finiteTransform(weaponLocal);
                }

                std::int32_t thisNodeId = -1;
                if (depth <= kMaxSceneDepth &&
                    building.nodes.size() <
                        kMaxReloadCatalogNodes) {
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
                            ReloadNodeFlag::NameTruncated);
                    }
                    if (copyFixedString(
                            record.value.rootRelativePath,
                            path)) {
                        record.value.flags |= flag(
                            ReloadNodeFlag::PathTruncated);
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
                            ReloadNodeFlag::IsNode);
                        record.value.childCount =
                            asNode->GetRuntimeData().children.size();
                    }
                    if (localValid) {
                        record.value.flags |= flag(
                            ReloadNodeFlag::
                                LocalTransformValid);
                        api_transform::fromNi(
                            object->local,
                            record.value.baselineLocal);
                    }
                    if (weaponLocalValid) {
                        record.value.flags |= flag(
                            ReloadNodeFlag::
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
                        ReloadCatalogStatusFlag::
                            NodeCatalogTruncated);
                }

                if (depth >= kMaxSceneDepth) {
                    if (auto* asNode = object->IsNode();
                        asNode &&
                        !asNode->GetRuntimeData().children.empty()) {
                        building.state.omittedNodeCount +=
                            asNode->GetRuntimeData().children.size();
                        building.state.statusFlags |= flag(
                            ReloadCatalogStatusFlag::
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
                            path,
                            weaponLocal,
                            weaponLocalValid);
                    }
                }
            };
            walk(walk, root, -1, 0, 0, {}, identity, true);
            if (building.nodes.empty()) {
                s_buildingCatalog = {};
                return false;
            }

            std::array<
                rock::api::weaponparts::EvidenceDetail,
                kMaxReloadEvidence>
                sourceEvidence{};
            std::uint32_t reportedEvidence = 0;
            const auto copiedEvidence =
                rockApiClient().copyWeaponEvidenceDetails(
                    sourceEvidence.data(),
                    kMaxReloadEvidence,
                    reportedEvidence);
            building.state.reportedEvidenceCount = reportedEvidence;
            if (copiedEvidence <
                    building.state.reportedEvidenceCount) {
                building.state.statusFlags |= flag(
                    ReloadCatalogStatusFlag::EvidenceTruncated);
            }

            std::uint32_t scheduledPointCount = 0;
            for (std::uint32_t index = 0;
                 index < copiedEvidence;
                 ++index) {
                const auto& source = sourceEvidence[index];
                if (source.weaponGenerationKey !=
                    gripState.weaponGenerationKey) {
                    building.state.statusFlags |= flag(
                        ReloadCatalogStatusFlag::
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
                    rockApiClient().resolveWeaponSource(source.weaponGenerationKey,source.sourceKey));
                record.value.interactionNodeId = nodeIdFor(
                    nodePointers,
                    rockApiClient().resolveWeaponSource(source.weaponGenerationKey,source.interactionKey));
                if (record.value.sourceNodeId >= 0) {
                    record.value.flags |= flag(
                        ReloadEvidenceFlag::
                            SourceNodeResolved);
                }
                if (record.value.interactionNodeId >= 0) {
                    record.value.flags |= flag(
                        ReloadEvidenceFlag::
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
                        ReloadEvidenceFlag::
                            SourceNameTruncated);
                }
                if (source.localBoundsGame.valid != 0) {
                    record.value.flags |= flag(
                        ReloadEvidenceFlag::
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
                            kMaxReloadEvidencePoints ?
                        kMaxReloadEvidencePoints -
                            scheduledPointCount :
                        0u;
                record.scheduledPointCount = collectEvidenceGeometry ?
                    std::min({
                        source.pointCount,
                        kMaxReloadEvidencePointsPerDetail,
                        remainingPointCapacity,
                    }) :
                    0u;
                scheduledPointCount += record.scheduledPointCount;
                if (collectEvidenceGeometry &&
                    record.scheduledPointCount < source.pointCount) {
                    record.value.flags |= flag(
                        ReloadEvidenceFlag::
                            GeometryTruncated);
                }
                record.value.omod = describeForm(source.omodFormId);
                record.value.attachPoint =
                    describeForm(source.attachPointFormId);
                building.evidence.push_back(std::move(record));
            }

            classifyCatalogWeaponFamily(building);

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
                    rock::api::Point3,
                    kMaxReloadEvidencePointsPerDetail>
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
            const rock::api::animation::AnimationPhaseContext& context)
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
                flag(ReloadFrameStatusFlag::Valid) |
                flag(ReloadFrameStatusFlag::CatalogValid);
            if (s_catalog.omittedTargetCount > 0) {
                state.statusFlags |= flag(
                    ReloadFrameStatusFlag::
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
            const rock::api::animation::AnimationPhaseContext& context,
            const paper::RockWeaponGripState& gripState,
            const ReloadObservationPhase phase)
        {
            PhaseCapture result{};
            auto* root = weaponRoot(gripState);
            if (!root || !finiteTransform(root->world)) {
                return result;
            }

            std::array<
                ReloadNodeObservation,
                kMaxReloadObservationTargets>
                captured{};
            std::array<
                std::uint8_t,
                kMaxReloadObservationTargets>
                present{};
            std::uint32_t catalogIndex = 0;
            std::uint32_t visitedCount = 0;
            bool mismatch = false;
            bool stop = false;
            const auto identity =
                transform_math::identityTransform<RE::NiTransform>();

            const auto walk = [&](auto&& self,
                                  RE::NiAVObject* object,
                                  const std::int32_t parentNodeId,
                                  const std::uint32_t childIndex,
                                  const std::uint32_t depth,
                                  const RE::NiTransform& parentWeaponLocal,
                                  const bool parentWeaponLocalValid) -> void {
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
                            ReloadCatalogStatusFlag::
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
                const bool localValid = finiteTransform(object->local);
                bool weaponLocalValid = object == root ||
                    (parentWeaponLocalValid && localValid);
                RE::NiTransform weaponLocal = identity;
                if (weaponLocalValid) {
                    // Compose authored graph locals so controller/world motion
                    // cannot leak through mixed-frame world-transform caches.
                    weaponLocal = reload_observation_policy::
                        resolveWeaponLocalFromGraph(
                            object == root,
                            identity,
                            parentWeaponLocal,
                            object->local,
                            [](const RE::NiTransform& parent,
                               const RE::NiTransform& child) {
                                return transform_math::composeTransforms(
                                    parent,
                                    child);
                            });
                    weaponLocalValid = finiteTransform(weaponLocal);
                }
                if (targetIndex >= 0) {
                    ReloadNodeObservation observation{};
                    observation.nodeId = nodeId;
                    observation.phase = phase;
                    observation.frameIndex = context.frameIndex;
                    if (localValid) {
                        observation.flags |= flag(
                            ReloadNodeObservationFlag::
                                LocalTransformValid);
                        api_transform::fromNi(
                            object->local,
                            observation.local);
                    }
                    if (weaponLocalValid) {
                        observation.flags |= flag(
                            ReloadNodeObservationFlag::
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
                            ReloadCatalogStatusFlag::
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
                            depth + 1,
                            weaponLocal,
                            weaponLocalValid);
                    }
                }
            };
            walk(walk, root, -1, 0, 0, identity, true);

            const bool catalogWasTruncated =
                (s_catalog.state.statusFlags & flag(
                    ReloadCatalogStatusFlag::
                        NodeCatalogTruncated)) != 0;
            if (!catalogWasTruncated &&
                catalogIndex != s_catalog.nodes.size()) {
                mismatch = true;
            }
            if (mismatch) {
                s_workingFrame.state.statusFlags |= flag(
                    ReloadFrameStatusFlag::TopologyMismatch);
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
        [[nodiscard]] Status copyRange(
            const std::vector<Value>& source,
            const std::uint32_t first,
            Value* destination,
            const std::uint32_t capacity,
            std::uint32_t& copied)
        {
            copied = 0;
            if (first > source.size()) {
                return Status::OutOfRange;
            }
            if (capacity == 0) {
                return Status::Ok;
            }
            if (!destination) {
                return Status::InvalidArgument;
            }
            const auto count = std::min<std::size_t>(
                capacity,
                source.size() - first);
            for (std::size_t index = 0; index < count; ++index) {
                destination[index] = source[first + index];
            }
            copied = static_cast<std::uint32_t>(count);
            return Status::Ok;
        }
    }

    void reset()
    {
        clearCatalogState();
    }

    void advanceFrame(
        const rock::api::animation::AnimationPhaseContext& context,
        const paper::RockWeaponGripState* gripState,
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
        const rock::api::animation::AnimationPhaseContext& context,
        const paper::RockWeaponGripState& gripState,
        const ReloadObservationPhase phase)
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
            ReloadObservationPhase::NativeGraphOutput) {
            s_workingFrame.nativeGraphOutput = std::move(captured);
            if (s_workingFrame.nativeGraphOutput.captured) {
                s_workingFrame.state.statusFlags |= flag(
                    ReloadFrameStatusFlag::
                        NativeGraphOutputCaptured);
            }
        } else {
            s_workingFrame.postRock = std::move(captured);
            if (s_workingFrame.postRock.captured) {
                s_workingFrame.state.statusFlags |= flag(
                    ReloadFrameStatusFlag::PostRockCaptured);
            }
        }
    }

    void completeFrame(
        const rock::api::animation::AnimationPhaseContext& context,
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

    Status getLimits(
        ReloadObservationLimits& outLimits)
    {
        ReloadObservationLimits result{};
        result.maxCatalogNodes = kMaxReloadCatalogNodes;
        result.maxEvidenceRecords = kMaxReloadEvidence;
        result.maxObservationTargets =
            kMaxReloadObservationTargets;
        result.maxNodeObservations =
            kMaxReloadNodeObservations;
        result.maxEvidencePointsPerDetail =
            kMaxReloadEvidencePointsPerDetail;
        result.maxEvidencePointsTotal =
            kMaxReloadEvidencePoints;
        result.nodeNameCapacity = kReloadNodeNameCapacity;
        result.nodePathCapacity = kReloadNodePathCapacity;
        result.pluginNameCapacity = kFormPluginNameCapacity;
        result.editorIdCapacity = kFormEditorIdCapacity;
        result.displayNameCapacity = kFormDisplayNameCapacity;
        outLimits = result;
        return Status::Ok;
    }

    Status getCatalogState(
        ReloadCatalogState& outState)
    {
        if (!s_catalog.valid) {
            outState = {};
            return Status::NotReady;
        }
        outState = s_catalog.state;
        return Status::Ok;
    }

    Status copyCatalogNodes(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstNode,
        ReloadNodeCatalogEntry* outNodes,
        const std::uint32_t maxNodes,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid) {
            return Status::NotReady;
        }
        if (catalogSequence != s_catalog.state.catalogSequence) {
            return Status::StaleSnapshot;
        }
        if (firstNode > s_catalog.nodes.size()) {
            return Status::OutOfRange;
        }
        if (maxNodes == 0) {
            return Status::Ok;
        }
        if (!outNodes) {
            return Status::InvalidArgument;
        }
        const auto count = std::min<std::size_t>(
            maxNodes,
            s_catalog.nodes.size() - firstNode);
        for (std::size_t index = 0; index < count; ++index) {
            outNodes[index] = s_catalog.nodes[firstNode + index].value;
        }
        outCopied = static_cast<std::uint32_t>(count);
        return Status::Ok;
    }

    Status copyEvidence(
        const std::uint64_t catalogSequence,
        const std::uint32_t firstEvidence,
        ReloadEvidence* outEvidence,
        const std::uint32_t maxEvidence,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid) {
            return Status::NotReady;
        }
        if (catalogSequence != s_catalog.state.catalogSequence) {
            return Status::StaleSnapshot;
        }
        if (firstEvidence > s_catalog.evidence.size()) {
            return Status::OutOfRange;
        }
        if (maxEvidence == 0) {
            return Status::Ok;
        }
        if (!outEvidence) {
            return Status::InvalidArgument;
        }
        const auto count = std::min<std::size_t>(
            maxEvidence,
            s_catalog.evidence.size() - firstEvidence);
        for (std::size_t index = 0; index < count; ++index) {
            outEvidence[index] =
                s_catalog.evidence[firstEvidence + index].value;
        }
        outCopied = static_cast<std::uint32_t>(count);
        return Status::Ok;
    }

    Status copyEvidencePoints(
        const std::uint64_t catalogSequence,
        const std::uint32_t evidenceId,
        const std::uint32_t firstPoint,
        Point3* outPoints,
        const std::uint32_t maxPoints,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid) {
            return Status::NotReady;
        }
        if (catalogSequence != s_catalog.state.catalogSequence) {
            return Status::StaleSnapshot;
        }
        if (evidenceId >= s_catalog.evidence.size()) {
            return Status::OutOfRange;
        }
        return copyRange(
            s_catalog.evidence[evidenceId].points,
            firstPoint,
            outPoints,
            maxPoints,
            outCopied);
    }

    Status getFrameState(
        ReloadFrameState& outState)
    {
        if (!s_publishedFrame.valid) {
            outState = {};
            return Status::NotReady;
        }
        outState = s_publishedFrame.state;
        return Status::Ok;
    }

    Status copyNodeObservations(
        const std::uint64_t snapshotSequence,
        const std::uint32_t firstObservation,
        ReloadNodeObservation* outObservations,
        const std::uint32_t maxObservations,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_publishedFrame.valid) {
            return Status::NotReady;
        }
        if (snapshotSequence !=
            s_publishedFrame.state.snapshotSequence) {
            return Status::StaleSnapshot;
        }
        if (firstObservation >
            s_publishedFrame.state.observationCount) {
            return Status::OutOfRange;
        }
        if (maxObservations == 0) {
            return Status::Ok;
        }
        if (!outObservations) {
            return Status::InvalidArgument;
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
        return Status::Ok;
    }

    Status copyEvidenceMotionSources(
        EvidenceMotionSource* outSources,
        const std::uint32_t maxSources,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_catalog.valid || !s_publishedFrame.valid) {
            return Status::NotReady;
        }
        if (maxSources == 0) {
            return Status::Ok;
        }
        if (!outSources) {
            return Status::InvalidArgument;
        }

        const auto observationForNode = [](const std::int32_t nodeId)
            -> const ReloadNodeObservation* {
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
                        ReloadObservationPhase::
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

            const ReloadNodeObservation* observation = nullptr;
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
                        ReloadNodeFlag::
                            WeaponLocalTransformValid)) != 0) {
                    source.baselineWeaponLocal =
                        node.baselineWeaponLocal;
                    source.flags |= static_cast<std::uint32_t>(
                        EvidenceMotionSourceFlag::BaselineValid);
                }
            }
            if (observation &&
                (observation->flags & flag(
                    ReloadNodeObservationFlag::
                        WeaponLocalTransformValid)) != 0) {
                source.currentWeaponLocal = observation->weaponLocal;
                source.flags |= static_cast<std::uint32_t>(
                    EvidenceMotionSourceFlag::CurrentValid);
            }
            outSources[index] = source;
        }
        outCopied = count;
        return Status::Ok;
    }
}
