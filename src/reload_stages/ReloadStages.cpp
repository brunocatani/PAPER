#include "reload_stages/ReloadStages.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation_evidence/ClipTelemetry.h"
#include "reload_observation/ReloadObservation.h"
#include "reload_stages/ReloadStagePolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace paper::reload_stages
{
    namespace
    {
        using namespace api;

        template <class Enum>
        [[nodiscard]] constexpr std::uint32_t flag(const Enum value)
        {
            return static_cast<std::uint32_t>(value);
        }

        struct PublishedStages
        {
            PaperReloadStageStateV1 state{};
            std::array<
                PaperReloadStagePartV1,
                PAPER_MAX_RELOAD_STAGE_PARTS_V1>
                parts{};
            bool valid{ false };
        };

        PublishedStages s_published{};
        reload_stage_policy::FireCorrelationState s_fireCorrelation{};

        [[nodiscard]] bool hasFlag(
            const std::uint32_t flags,
            const reload_observation::EvidenceMotionSourceFlag value)
        {
            return (flags & flag(value)) != 0;
        }

        [[nodiscard]] bool isPistol(
            const PaperReloadCatalogStateV1& catalog)
        {
            return (catalog.classification.flags & flag(
                        PaperWeaponClassificationFlagV1::Valid)) != 0 &&
                   catalog.classification.sizeClass ==
                       static_cast<std::uint32_t>(
                           rock::provider::
                               RockProviderWeaponSizeClassV1::Pistol);
        }

        [[nodiscard]] bool isMagazine(
            const reload_observation::EvidenceMotionSource& source)
        {
            return source.partKind == static_cast<std::uint32_t>(
                rock::provider::
                    RockProviderWeaponPartKindV1::Magazine);
        }

        [[nodiscard]] bool isSlide(
            const reload_observation::EvidenceMotionSource& source)
        {
            return source.partKind == static_cast<std::uint32_t>(
                       rock::provider::
                           RockProviderWeaponPartKindV1::Slide) ||
                   source.actionRole == static_cast<std::uint32_t>(
                       rock::provider::
                           RockProviderWeaponActionRoleV1::Slide);
        }

        [[nodiscard]] bool contributesToAggregate(
            const PublishedStages& published,
            const PaperReloadStagePartV1& candidate)
        {
            if (candidate.selectedNodeId < 0) {
                return false;
            }
            for (std::uint32_t index = 0;
                 index < published.state.partCount;
                 ++index) {
                const auto& existing = published.parts[index];
                if ((existing.flags & flag(
                        PaperReloadStagePartFlagV1::
                            ContributesToAggregate)) != 0 &&
                    existing.selectedNodeId == candidate.selectedNodeId &&
                    existing.partKind == candidate.partKind) {
                    return false;
                }
            }
            return true;
        }
    }

    void reset()
    {
        s_published = {};
        s_fireCorrelation = {};
    }

    void completeFrame(
        const rock::provider::
            RockProviderAnimationPhaseContextV1& context)
    {
        PaperReloadCatalogStateV1 catalog{};
        PaperReloadFrameStateV1 frame{};
        if (reload_observation::getCatalogState(catalog) !=
                PaperResultV1::Ok ||
            reload_observation::getFrameState(frame) !=
                PaperResultV1::Ok ||
            frame.catalogSequence != catalog.catalogSequence ||
            frame.weaponGenerationKey !=
                catalog.weaponGenerationKey ||
            frame.frameIndex != context.frameIndex) {
            s_published = {};
            return;
        }

        PublishedStages next{};
        next.state.weaponFormId = frame.weaponFormId;
        next.state.weaponGenerationKey = frame.weaponGenerationKey;
        next.state.catalogSequence = frame.catalogSequence;
        next.state.snapshotSequence = frame.snapshotSequence;
        next.state.frameIndex = frame.frameIndex;
        next.state.restTranslationToleranceGameUnits =
            reload_stage_policy::
                kRestTranslationToleranceGameUnits;
        next.state.restRotationToleranceDegrees =
            reload_stage_policy::kRestRotationToleranceDegrees;
        next.state.restScaleTolerance =
            reload_stage_policy::kRestScaleTolerance;

        if ((catalog.classification.flags & flag(
                PaperWeaponClassificationFlagV1::Available)) != 0) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    ClassificationAvailable);
        }
        if ((catalog.classification.flags & flag(
                PaperWeaponClassificationFlagV1::Valid)) != 0) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    ClassificationValid);
        }
        const bool pistol = isPistol(catalog);
        if (pistol) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::Pistol);
        }
        const bool nativeOutputAvailable =
            (frame.statusFlags & flag(
                PaperReloadFrameStatusFlagV1::
                    NativeGraphOutputCaptured)) != 0 &&
            (frame.statusFlags & flag(
                PaperReloadFrameStatusFlagV1::
                    TopologyMismatch)) == 0;
        if (nativeOutputAvailable) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    NativeGraphOutputAvailable);
        }

        std::array<
            reload_observation::EvidenceMotionSource,
            PAPER_MAX_RELOAD_STAGE_PARTS_V1>
            sources{};
        std::uint32_t sourceCount = 0;
        const auto sourceResult =
            reload_observation::copyEvidenceMotionSources(
                sources.data(),
                static_cast<std::uint32_t>(sources.size()),
                sourceCount);
        if (sourceResult != PaperResultV1::Ok) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    PartDataIncomplete);
            sourceCount = 0;
        }
        if (catalog.evidenceCount > sources.size()) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    PartCapacityTruncated);
        }

        for (std::uint32_t index = 0;
             index < sourceCount;
             ++index) {
            const auto& source = sources[index];
            auto& part = next.parts[next.state.partCount++];
            part.evidenceId = source.evidenceId;
            part.bodyId = source.bodyId;
            part.sourceNodeId = source.sourceNodeId;
            part.interactionNodeId = source.interactionNodeId;
            part.selectedNodeId = source.selectedNodeId;
            part.partKind = source.partKind;
            part.actionRole = source.actionRole;
            part.frameIndex = frame.frameIndex;
            if (hasFlag(
                    source.flags,
                    reload_observation::
                        EvidenceMotionSourceFlag::SourceNodeSelected)) {
                part.flags |= flag(
                    PaperReloadStagePartFlagV1::
                        SourceNodeSelected);
            }
            if (hasFlag(
                    source.flags,
                    reload_observation::
                        EvidenceMotionSourceFlag::InteractionNodeFallback)) {
                part.flags |= flag(
                    PaperReloadStagePartFlagV1::
                        InteractionNodeFallback);
            }
            const bool baselineValid = hasFlag(
                source.flags,
                reload_observation::
                    EvidenceMotionSourceFlag::BaselineValid);
            const bool currentValid = hasFlag(
                source.flags,
                reload_observation::
                    EvidenceMotionSourceFlag::CurrentValid);
            if (baselineValid) {
                part.flags |= flag(
                    PaperReloadStagePartFlagV1::BaselineValid);
                part.baselineWeaponLocal =
                    source.baselineWeaponLocal;
            }
            if (currentValid) {
                part.flags |= flag(
                    PaperReloadStagePartFlagV1::CurrentValid);
                part.currentWeaponLocal = source.currentWeaponLocal;
            }

            const bool magazine = isMagazine(source);
            const bool slide = isSlide(source);
            if (magazine) {
                part.flags |= flag(
                    PaperReloadStagePartFlagV1::Magazine);
            }
            if (slide) {
                part.flags |= flag(
                    PaperReloadStagePartFlagV1::Slide);
            }

            if (!baselineValid || !currentValid) {
                next.state.statusFlags |= flag(
                    PaperReloadStageStatusFlagV1::
                        PartDataIncomplete);
                continue;
            }
            const auto delta =
                reload_stage_policy::calculateTransformDelta(
                    source.baselineWeaponLocal,
                    source.currentWeaponLocal);
            if (!delta.valid) {
                next.state.statusFlags |= flag(
                    PaperReloadStageStatusFlagV1::
                        PartDataIncomplete);
                continue;
            }
            part.flags |= flag(PaperReloadStagePartFlagV1::Valid);
            part.translationDeltaGameUnits =
                delta.translationGameUnits;
            part.rotationDeltaDegrees = delta.rotationDegrees;
            part.scaleDelta = delta.scale;
            next.state.maximumTranslationDeltaGameUnits = (std::max)(
                next.state.maximumTranslationDeltaGameUnits,
                delta.translationGameUnits);
            next.state.maximumRotationDeltaDegrees = (std::max)(
                next.state.maximumRotationDeltaDegrees,
                delta.rotationDegrees);
            next.state.maximumScaleDelta = (std::max)(
                next.state.maximumScaleDelta,
                delta.scale);

            const bool atRest = reload_stage_policy::isAtRest(delta);
            part.flags |= flag(
                atRest ?
                    PaperReloadStagePartFlagV1::AtRest :
                    PaperReloadStagePartFlagV1::Displaced);
            const bool aggregate = contributesToAggregate(next, part);
            if (!aggregate) {
                continue;
            }
            part.flags |= flag(
                PaperReloadStagePartFlagV1::
                    ContributesToAggregate);
            ++next.state.aggregatePartCount;
            if (atRest) {
                ++next.state.atRestPartCount;
            } else {
                ++next.state.displacedPartCount;
            }
            if (magazine) {
                ++next.state.magazinePartCount;
                part.flags |= flag(
                    atRest ?
                        PaperReloadStagePartFlagV1::MagazineIn :
                        PaperReloadStagePartFlagV1::MagazineOut);
                if (atRest) {
                    ++next.state.magazineInCount;
                } else {
                    ++next.state.magazineOutCount;
                }
            }
            if (slide) {
                ++next.state.slidePartCount;
                if (!atRest) {
                    ++next.state.slideBackCount;
                    part.flags |= flag(
                        PaperReloadStagePartFlagV1::SlideBack);
                }
            }
        }

        if (next.state.magazinePartCount > 0) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    MagazineObserved);
        }
        if (next.state.slidePartCount > 0) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::SlideObserved);
        }

        std::array<clip_telemetry::ActivityState, 8> rawActivities{};
        const auto rawActivityCount =
            clip_telemetry::copyActivityStates(
                rawActivities.data(),
                static_cast<std::uint32_t>(rawActivities.size()));
        std::array<
            reload_stage_policy::ActivityIdentity,
            rawActivities.size()>
            activities{};
        std::uint32_t activityCount = 0;
        for (std::uint32_t index = 0;
             index < rawActivityCount;
             ++index) {
            if (rawActivities[index].weaponGenerationKey !=
                frame.weaponGenerationKey) {
                continue;
            }
            activities[activityCount++] = {
                rawActivities[index].activationOrder,
            };
        }
        next.state.activeAnimationActivityCount = activityCount;

        const auto native =
            native_animation_authority::queryRuntimeStatus();
        if (native.weaponFireHookReady) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    WeaponFireHookReady);
        }
        const auto fireStep =
            reload_stage_policy::advanceFireCorrelation(
                s_fireCorrelation,
                frame.weaponGenerationKey,
                native.fireSequence,
                native.fireActivityOrderAtEvent,
                std::span<const reload_stage_policy::ActivityIdentity>{
                    activities.data(),
                    activityCount,
                });
        s_fireCorrelation = fireStep.state;
        next.state.fireSequence = native.fireSequence;
        next.state.fireActivityOrderAtEvent =
            native.fireActivityOrderAtEvent;
        next.state.lastObservedActivityOrder =
            fireStep.state.highestObservedActivationOrder;
        next.state.correlatedFireActivityCount =
            fireStep.activeCorrelatedCount;
        next.state.fireAdmissionFramesRemaining =
            fireStep.state.admissionFramesRemaining;
        next.state.fireCorrelation = fireStep.correlation;
        if (fireStep.newFireEvent) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    FireEventObserved);
        }
        if (fireStep.state.admissionFramesRemaining > 0) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    FireCorrelationPending);
        }
        if (fireStep.activeCorrelatedCount > 0) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::
                    FireActivityCorrelated);
        }

        const bool stageFrameValid = pistol && nativeOutputAvailable;
        if (stageFrameValid) {
            next.state.statusFlags |= flag(
                PaperReloadStageStatusFlagV1::Valid);
            if (next.state.aggregatePartCount > 0 &&
                next.state.displacedPartCount == 0) {
                next.state.stageFlags |= flag(
                    PaperReloadStageFlagV1::Rest);
            }
            if (fireStep.active) {
                next.state.stageFlags |= flag(
                    PaperReloadStageFlagV1::Fire);
            }
            if (next.state.slideBackCount > 0) {
                next.state.stageFlags |= flag(
                    PaperReloadStageFlagV1::SlideBack);
            }
            if (next.state.magazineInCount > 0) {
                next.state.stageFlags |= flag(
                    PaperReloadStageFlagV1::MagazineIn);
            }
            if (next.state.magazineOutCount > 0) {
                next.state.stageFlags |= flag(
                    PaperReloadStageFlagV1::MagazineOut);
            }
        }
        next.valid = true;
        s_published = next;
    }

    PaperResultV1 getState(PaperReloadStageStateV1& outState)
    {
        if (!s_published.valid) {
            outState = {};
            return PaperResultV1::NotReady;
        }
        outState = s_published.state;
        return PaperResultV1::Ok;
    }

    PaperResultV1 copyParts(
        const std::uint64_t snapshotSequence,
        const std::uint32_t firstPart,
        PaperReloadStagePartV1* outParts,
        const std::uint32_t maxParts,
        std::uint32_t& outCopied)
    {
        outCopied = 0;
        if (!s_published.valid) {
            return PaperResultV1::NotReady;
        }
        if (snapshotSequence !=
            s_published.state.snapshotSequence) {
            return PaperResultV1::StaleSnapshot;
        }
        if (firstPart > s_published.state.partCount) {
            return PaperResultV1::OutOfRange;
        }
        if (maxParts == 0) {
            return PaperResultV1::Ok;
        }
        if (!outParts) {
            return PaperResultV1::InvalidArgument;
        }
        const auto count = (std::min)(
            maxParts,
            s_published.state.partCount - firstPart);
        for (std::uint32_t index = 0; index < count; ++index) {
            outParts[index] =
                s_published.parts[firstPart + index];
        }
        outCopied = count;
        return PaperResultV1::Ok;
    }
}
