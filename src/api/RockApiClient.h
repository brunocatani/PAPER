#pragma once

#include "api/RockTypes.h"
#include <ROCK/Client.h>

#include <cstdint>

namespace RE { class NiAVObject; }

namespace paper
{
    class RockApiClient
    {
    public:
        [[nodiscard]] bool initialize();
        void shutdown();

        [[nodiscard]] bool ready() const;
        [[nodiscard]] std::uint64_t ownerToken() const;
        [[nodiscard]] bool queryPresentedHandFrame(rock::api::Hand hand,rock::api::hands::HandFrameV1& output) const;
        void setFrameContext(const rock::api::core::AnimationPhaseContextV1& context);
        [[nodiscard]] RE::NiAVObject* resolveWeaponSource(std::uint64_t generation,std::uint64_t key) const;


        [[nodiscard]] bool registerAnimationPhaseCallback(
            rock::api::core::PhaseCallbackV1 callback,
            void* userData);
        [[nodiscard]] bool setNativeAnimationAuthority(std::uint32_t flags);
        void clearNativeAnimationAuthority();
        [[nodiscard]] bool queryNativeAnimationAuthorityState(
            rock::api::animation::NativeAnimationAuthorityStateV1& outState) const;
        [[nodiscard]] bool queryEquippedWeaponGripState(
            paper::RockWeaponGripState& outState) const;
        [[nodiscard]] bool queryEquippedWeaponHandlingState(
            rock::api::weapon::EquippedWeaponHandlingStateV1& outState) const;
        [[nodiscard]] bool queryHandInteractionState(
            rock::api::Hand hand,
            rock::api::grab::HandInteractionStateV1& outState) const;
        [[nodiscard]] bool queryWeaponPartGripState(
            rock::api::Hand hand,
            rock::api::weaponparts::WeaponPartGripStateV1& outState) const;
        [[nodiscard]] bool querySelectedAuthoredGripPose(
            rock::api::weapon::AuthoredGripPoseV1& outPose) const;
        [[nodiscard]] bool queryPresentedHandPose(
            rock::api::Hand hand,
            rock::api::hands::PresentedHandPoseV1& outPose) const;
        [[nodiscard]] bool queryEquippedWeaponClassification(
            rock::api::weapon::WeaponClassificationV1& outClassification) const;
        [[nodiscard]] bool reloadObservationEvidenceReady() const;
        [[nodiscard]] std::uint32_t weaponEvidenceDetailCount() const;
        [[nodiscard]] std::uint32_t copyWeaponEvidenceDetails(
            rock::api::weaponparts::WeaponEvidenceDetailV1* outDetails,
            std::uint32_t maxDetails) const;
        [[nodiscard]] std::uint32_t copyWeaponEvidencePoints(
            std::uint32_t bodyId,
            rock::api::Point3* outPoints,
            std::uint32_t maxPoints) const;
        [[nodiscard]] bool setHandVisualAuthority(
            const rock::api::animation::HandVisualAuthorityRequestV1& request) const;
        void clearHandVisualAuthority(rock::api::Hand hand) const;
        [[nodiscard]] bool publishNativeAnimationRuntime(
            const rock::api::animation::NativeAnimationRuntimePublicationV1& publication) const;
        void clearNativeAnimationRuntime() const;
        [[nodiscard]] bool publishDebugOverlay(
            const rock::api::diagnostics::DebugOverlayPublicationV1& publication) const;
        void clearDebugOverlay() const;

    private:
        rock::api::Client _client;
        const rock::api::core::ApiV1* _core{};
        const rock::api::hands::ApiV1* _hands{};
        const rock::api::grab::ApiV1* _grab{};
        const rock::api::weapon::ApiV1* _weapon{};
        const rock::api::weaponparts::ApiV1* _parts{};
        const rock::api::animation::ApiV1* _animation{};
        const rock::api::diagnostics::ApiV1* _diagnostics{};
        rock::api::SampleV1 _sample{};
        template<class T> void stamp(T& value) const {
            value.worldGeneration=_sample.worldGeneration;
            value.skeletonGeneration=_sample.skeletonGeneration;
            value.providerGeneration=_sample.providerGeneration;
        }
        std::uint64_t _ownerToken{ 0 };
        std::uint64_t _phaseCallbackToken{ 0 };
        std::uint32_t _publishedAuthorityFlags{ 0 };
        bool _reloadObservationEvidenceReady{ false };
    };

    RockApiClient& rockApiClient();
}
