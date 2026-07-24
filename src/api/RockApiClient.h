#pragma once

#include "api/ROCKProviderApi.h"

#include <array>
#include <cstdint>

namespace paper
{
    class RockApiClient
    {
    public:
        [[nodiscard]] bool initialize();
        void shutdown();

        [[nodiscard]] bool ready() const;
        [[nodiscard]] std::uint64_t ownerToken() const;
        [[nodiscard]] const rock::provider::RockProviderApi* api() const;

        [[nodiscard]] bool registerAnimationPhaseCallback(
            rock::provider::RockProviderAnimationPhaseCallbackV1 callback,
            void* userData);
        [[nodiscard]] bool setNativeAnimationAuthority(std::uint32_t flags);
        void clearNativeAnimationAuthority();
        [[nodiscard]] bool queryNativeAnimationAuthorityState(
            rock::provider::RockProviderNativeAnimationAuthorityStateV1& outState) const;
        [[nodiscard]] bool queryEquippedWeaponGripState(
            rock::provider::RockProviderEquippedWeaponGripStateV1& outState) const;
        [[nodiscard]] bool queryWeaponPartGripState(
            rock::provider::RockProviderHand hand,
            rock::provider::RockProviderWeaponPartGripStateV1& outState) const;
        [[nodiscard]] bool copyWeaponPartPoses(
            std::array<
                rock::provider::RockProviderWeaponPartPoseV1,
                rock::provider::ROCK_PROVIDER_MAX_WEAPON_BODIES>& outPoses,
            std::uint32_t& outCount) const;
        [[nodiscard]] bool queryEquippedWeaponClassification(
            rock::provider::RockProviderWeaponClassificationV1& outClassification) const;
        [[nodiscard]] bool setHandVisualAuthority(
            const rock::provider::RockProviderHandVisualAuthorityRequestV1& request) const;
        void clearHandVisualAuthority(rock::provider::RockProviderHand hand) const;
        [[nodiscard]] bool publishNativeAnimationRuntime(
            const rock::provider::RockProviderNativeAnimationRuntimePublicationV1& publication) const;
        void clearNativeAnimationRuntime() const;
        [[nodiscard]] bool publishDebugOverlay(
            const rock::provider::RockProviderDebugOverlayPublicationV1& publication) const;
        void clearDebugOverlay() const;

    private:
        const rock::provider::RockProviderApi* _api{ nullptr };
        std::uint64_t _ownerToken{ 0 };
        std::uint64_t _phaseCallbackToken{ 0 };
        std::uint32_t _publishedAuthorityFlags{ 0 };
        bool _weaponPartObservabilityAvailable{ false };
    };

    RockApiClient& rockApiClient();
}
