#pragma once

#include "api/RockTypes.h"
#include <ROCK/Client.h>

#include <cstdint>
#include <span>

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
        [[nodiscard]] bool queryHandFrame(rock::api::Hand hand,rock::api::hands::HandFrame& output) const;
        [[nodiscard]] bool queryPresentedHandFrame(rock::api::Hand hand,rock::api::hands::HandFrame& output) const;
        void setFrameContext(const rock::api::animation::AnimationPhaseContext& context);
        [[nodiscard]] RE::NiAVObject* resolveWeaponSource(std::uint64_t generation,std::uint64_t key) const;


        [[nodiscard]] bool registerAnimationPhaseCallback(
            rock::api::animation::PhaseCallback callback,
            void* userData);
        [[nodiscard]] bool setNativeAnimationAuthority(std::uint32_t flags);
        void clearNativeAnimationAuthority();
        [[nodiscard]] bool queryNativeAnimationAuthorityState(
            rock::api::animation::NativeAnimationAuthorityState& outState) const;
        [[nodiscard]] bool queryEquippedWeaponGripState(
            paper::RockWeaponGripState& outState) const;
        [[nodiscard]] bool queryEquippedWeaponHandling(
            paper::RockWeaponHandling& outHandling) const;
        [[nodiscard]] bool queryHandInteractionState(
            rock::api::Hand hand,
            rock::api::grab::HandInteractionState& outState) const;
        [[nodiscard]] bool queryWeaponPartGripState(
            rock::api::Hand hand,
            rock::api::weaponparts::GripState& outState) const;
        [[nodiscard]] bool querySelectedAuthoredGripPose(
            rock::api::weapon::AuthoredGripPose& outPose) const;
        [[nodiscard]] bool queryPresentedHandPose(
            rock::api::Hand hand,
            rock::api::hands::PresentedHandPose& outPose) const;
        [[nodiscard]] bool queryEquippedWeaponClassification(
            rock::api::weapon::Classification& outClassification) const;
        [[nodiscard]] bool reloadObservationEvidenceReady() const;
        // Returns the records copied; outTotal receives ROCK's full count.
        [[nodiscard]] std::uint32_t copyWeaponEvidenceDetails(
            rock::api::weaponparts::EvidenceDetail* outDetails,
            std::uint32_t maxDetails,
            std::uint32_t& outTotal) const;
        [[nodiscard]] std::uint32_t copyWeaponEvidencePoints(
            std::uint32_t bodyId,
            rock::api::Point3* outPoints,
            std::uint32_t maxPoints) const;
        // Per weapon slot: what the bolt hold needs on both equipped weapons.
        [[nodiscard]] bool queryWeaponState(
            std::uint32_t weaponSlot,
            rock::api::weapon::WeaponState& outState) const;
        [[nodiscard]] bool queryWeaponClassification(
            std::uint32_t weaponSlot,
            rock::api::weapon::Classification& outClassification) const;
        [[nodiscard]] std::uint32_t copyWeaponEvidenceDetails(
            std::uint32_t weaponSlot,
            rock::api::weaponparts::EvidenceDetail* outDetails,
            std::uint32_t maxDetails,
            std::uint32_t& outTotal) const;
        [[nodiscard]] rock::api::Status copyWeaponSources(
            std::uint32_t weaponSlot,
            std::uint64_t generation,
            rock::api::weaponparts::Source* outSources,
            std::uint32_t maxSources,
            std::uint32_t& outTotal) const;
        [[nodiscard]] bool queryWeaponSourcePose(
            std::uint32_t weaponSlot,
            std::uint64_t generation,
            std::uint64_t sourceKey,
            rock::api::weaponparts::SourcePose& outPose) const;
        [[nodiscard]] bool setWeaponDrives(
            std::uint32_t weaponSlot,
            std::span<const rock::api::weaponparts::DriveTarget> drives) const;
        void clearWeaponDrives(std::uint32_t weaponSlot) const;
        // An empty second weapon dry-fires on its trigger instead of reloading.
        [[nodiscard]] bool setAutomaticReloadBlocked(bool blocked) const;
        [[nodiscard]] bool setHandVisualAuthority(
            const rock::api::animation::HandVisualAuthorityRequest& request) const;
        void clearHandVisualAuthority(rock::api::Hand hand) const;
        [[nodiscard]] bool publishNativeAnimationRuntime(
            const rock::api::animation::NativeAnimationRuntimePublication& publication) const;
        void clearNativeAnimationRuntime() const;

    private:
        rock::api::Client _client;
        const rock::api::core::Api* _core{};
        const rock::api::hands::Api* _hands{};
        const rock::api::grab::Api* _grab{};
        const rock::api::weapon::Api* _weapon{};
        const rock::api::weaponparts::Api* _parts{};
        const rock::api::animation::Api* _animation{};
        rock::api::Sample _sample{};
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
