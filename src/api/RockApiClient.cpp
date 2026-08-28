#include "api/RockApiClient.h"

#include "PaperLog.h"

#include <cstring>

namespace paper
{
    namespace
    {
        constexpr std::uint32_t kRequiredCapabilities =
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::NativeAnimationAuthority) |
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::AnimationPhases) |
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::EquippedWeaponGripState) |
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::HandVisualAuthority) |
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::NativeAnimationRuntimeProvider) |
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::DebugOverlayPublication) |
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::PoseReadback) |
            static_cast<std::uint32_t>(
                rock::provider::RockProviderConsumerCapabilityV1::HandInteractionState);
        constexpr std::uint32_t kRollingLeaseFrames = 3;
    }

    RockApiClient& rockApiClient()
    {
        static RockApiClient client;
        return client;
    }

    bool RockApiClient::initialize()
    {
        if (_api && _ownerToken != 0) {
            return true;
        }
        const int initializeResult =
            rock::provider::RockProviderApi::initialize(
                rock::provider::ROCK_PROVIDER_API_VERSION,
                rock::provider::ROCK_PROVIDER_API_V1_NATIVE_ANIMATION_RUNTIME_CLEAR_TABLE_BYTES);
        if (initializeResult != 0) {
            PAPER_LOG_ERROR(
                Api,
                "ROCK V1 provider initialization failed with result {}",
                initializeResult);
            return false;
        }
        _api = rock::provider::RockProviderApi::inst;
        if (!_api) {
            return false;
        }

        rock::provider::RockProviderLimitsV1 limits{};
        if (!_api->getProviderLimitsV1(&limits) ||
            !rock::provider::supportsAnimationPhasesV1(limits) ||
            !rock::provider::supportsEquippedWeaponGripStateV1(limits) ||
            !rock::provider::supportsEquippedWeaponHandlingAuthorityV1(limits) ||
            !rock::provider::supportsWeaponClassificationV1(limits) ||
            !rock::provider::supportsHandVisualAuthorityV1(limits) ||
            !rock::provider::supportsNativeAnimationRuntimeProviderV1(limits) ||
            !rock::provider::supportsDebugOverlayPublicationV1(limits) ||
            !rock::provider::supportsPresentedHandFramesV1(limits) ||
            !rock::provider::supportsWeaponPartGripStateV1(limits) ||
            !rock::provider::supportsHandInteractionStateV1() ||
            !rock::provider::supportsPoseReadbackV1() ||
            !rock::provider::supportsNativeAnimationAuthorityV1(limits)) {
            PAPER_LOG_ERROR(
                Api,
                "Loaded ROCK provider does not expose the complete Paper V1 support surface");
            _api = nullptr;
            return false;
        }

        rock::provider::RockProviderLimitsExtV1 extendedLimits{};
        const bool extendedLimitsReady =
            rock::provider::queryProviderLimitsExtV1(extendedLimits);
        _reloadObservationEvidenceReady =
            rock::provider::hasFeatureBitV1(
                limits.featureBits,
                rock::provider::RockProviderFeatureBitV1::WeaponEvidence) &&
            extendedLimitsReady &&
            extendedLimits.maxWeaponEvidenceDetails > 0 &&
            extendedLimits.maxWeaponEvidencePointsPerDetail > 0 &&
            _api->getWeaponEvidenceDetailCountV1 &&
            _api->copyWeaponEvidenceDetailsV1 &&
            _api->copyWeaponEvidenceDetailPointsV1;
        if (!_reloadObservationEvidenceReady) {
            PAPER_LOG_WARN(
                Api,
                "ROCK V1 weapon evidence is unavailable; PAPER will publish scene observations without evidence records or geometry");
        }

        rock::provider::RockProviderConsumerRegistrationV1 registration{};
        std::memcpy(
            registration.modName,
            "PAPER",
            sizeof("PAPER"));
        registration.requestedCapabilities = kRequiredCapabilities;
        rock::provider::RockProviderConsumerHandleV1 handle{};
        const auto result = _api->registerConsumerV1(&registration, &handle);
        if (result != rock::provider::RockProviderResultV1::Ok ||
            handle.ownerToken == 0 ||
            (handle.grantedCapabilities & kRequiredCapabilities) !=
                kRequiredCapabilities) {
            PAPER_LOG_ERROR(
                Api,
                "ROCK consumer registration failed result={} granted=0x{:08X}",
                static_cast<std::uint32_t>(result),
                handle.grantedCapabilities);
            if (handle.ownerToken != 0 && _api->unregisterConsumerV1) {
                (void)_api->unregisterConsumerV1(handle.ownerToken);
            }
            _api = nullptr;
            return false;
        }

        _ownerToken = handle.ownerToken;
        PAPER_LOG_INFO(
            Api,
            "Registered with ROCK V1 owner={:016X} capabilities=0x{:08X}",
            _ownerToken,
            handle.grantedCapabilities);
        return true;
    }

    void RockApiClient::shutdown()
    {
        if (!_api || _ownerToken == 0) {
            return;
        }
        clearNativeAnimationAuthority();
        clearHandVisualAuthority(rock::provider::RockProviderHand::None);
        clearNativeAnimationRuntime();
        clearDebugOverlay();
        if (_phaseCallbackToken != 0) {
            (void)_api->unregisterAnimationPhaseCallbackV1(
                _ownerToken,
                _phaseCallbackToken);
            _phaseCallbackToken = 0;
        }
        (void)_api->unregisterConsumerV1(_ownerToken);
        _ownerToken = 0;
        _api = nullptr;
        _reloadObservationEvidenceReady = false;
    }

    bool RockApiClient::ready() const
    {
        return _api && _ownerToken != 0;
    }

    std::uint64_t RockApiClient::ownerToken() const
    {
        return _ownerToken;
    }

    const rock::provider::RockProviderApi* RockApiClient::api() const
    {
        return _api;
    }

    bool RockApiClient::registerAnimationPhaseCallback(
        rock::provider::RockProviderAnimationPhaseCallbackV1 callback,
        void* userData)
    {
        if (!ready() || !callback) {
            return false;
        }
        if (_phaseCallbackToken != 0) {
            return true;
        }
        return _api->registerAnimationPhaseCallbackV1(
                   _ownerToken,
                   callback,
                   userData,
                   &_phaseCallbackToken) ==
               rock::provider::RockProviderResultV1::Ok;
    }

    bool RockApiClient::setNativeAnimationAuthority(const std::uint32_t flags)
    {
        if (!ready()) {
            return false;
        }
        if (flags == 0) {
            clearNativeAnimationAuthority();
            return true;
        }
        rock::provider::RockProviderNativeAnimationAuthorityRequestV1 request{};
        request.flags = flags;
        request.leaseFrames = kRollingLeaseFrames;
        const auto result = _api->setNativeAnimationAuthorityV1(
            _ownerToken,
            &request);
        if (result != rock::provider::RockProviderResultV1::Ok) {
            return false;
        }
        _publishedAuthorityFlags = flags;
        return true;
    }

    void RockApiClient::clearNativeAnimationAuthority()
    {
        if (ready() && _publishedAuthorityFlags != 0) {
            (void)_api->clearNativeAnimationAuthorityV1(_ownerToken);
        }
        _publishedAuthorityFlags = 0;
    }

    bool RockApiClient::queryNativeAnimationAuthorityState(
        rock::provider::RockProviderNativeAnimationAuthorityStateV1& outState) const
    {
        outState = {};
        return ready() && _api->getNativeAnimationAuthorityStateV1(&outState);
    }

    bool RockApiClient::queryEquippedWeaponGripState(
        rock::provider::RockProviderEquippedWeaponGripStateV1& outState) const
    {
        outState = {};
        return ready() &&
               _api->getEquippedWeaponGripStateV1(_ownerToken, &outState);
    }

    bool RockApiClient::queryEquippedWeaponHandlingState(
        rock::provider::RockProviderEquippedWeaponHandlingStateV1& outState) const
    {
        outState = {};
        return ready() &&
               _api->getEquippedWeaponHandlingStateV1(&outState);
    }

    bool RockApiClient::queryHandInteractionState(
        const rock::provider::RockProviderHand hand,
        rock::provider::RockProviderHandInteractionStateV1& outState) const
    {
        outState = {};
        return ready() && _api->getHandInteractionStateV1 &&
               _api->getHandInteractionStateV1(
                   _ownerToken,
                   hand,
                   &outState) ==
                   rock::provider::RockProviderResultV1::Ok;
    }

    bool RockApiClient::queryWeaponPartGripState(
        const rock::provider::RockProviderHand hand,
        rock::provider::RockProviderWeaponPartGripStateV1& outState) const
    {
        outState = {};
        return ready() && _api->getWeaponPartGripStateV1(hand, &outState);
    }

    bool RockApiClient::querySelectedAuthoredGripPose(
        rock::provider::RockProviderAuthoredGripPoseV1& outPose) const
    {
        outPose = {};
        return ready() &&
               _api->getSelectedAuthoredGripPoseV1(
                   _ownerToken,
                   &outPose) ==
                   rock::provider::RockProviderResultV1::Ok;
    }

    bool RockApiClient::queryPresentedHandPose(
        const rock::provider::RockProviderHand hand,
        rock::provider::RockProviderPresentedHandPoseV1& outPose) const
    {
        outPose = {};
        return ready() && _api->getPresentedHandPoseV1 &&
               _api->getPresentedHandPoseV1(
                   _ownerToken,
                   hand,
                   &outPose) ==
                   rock::provider::RockProviderResultV1::Ok;
    }

    bool RockApiClient::queryEquippedWeaponClassification(
        rock::provider::RockProviderWeaponClassificationV1& outClassification) const
    {
        outClassification = {};
        return ready() &&
               _api->queryEquippedWeaponClassificationV1(&outClassification);
    }

    bool RockApiClient::reloadObservationEvidenceReady() const
    {
        return ready() && _reloadObservationEvidenceReady;
    }

    std::uint32_t RockApiClient::weaponEvidenceDetailCount() const
    {
        return reloadObservationEvidenceReady() ?
            _api->getWeaponEvidenceDetailCountV1() :
            0;
    }

    std::uint32_t RockApiClient::copyWeaponEvidenceDetails(
        rock::provider::RockProviderWeaponEvidenceDetailV1* outDetails,
        const std::uint32_t maxDetails) const
    {
        if (!reloadObservationEvidenceReady() ||
            !outDetails ||
            maxDetails == 0) {
            return 0;
        }
        return _api->copyWeaponEvidenceDetailsV1(outDetails, maxDetails);
    }

    std::uint32_t RockApiClient::copyWeaponEvidencePoints(
        const std::uint32_t bodyId,
        rock::provider::RockProviderPoint3* outPoints,
        const std::uint32_t maxPoints) const
    {
        if (!reloadObservationEvidenceReady() ||
            !outPoints ||
            maxPoints == 0) {
            return 0;
        }
        return _api->copyWeaponEvidenceDetailPointsV1(
            bodyId,
            outPoints,
            maxPoints);
    }

    bool RockApiClient::setHandVisualAuthority(
        const rock::provider::RockProviderHandVisualAuthorityRequestV1& request) const
    {
        auto leasedRequest = request;
        leasedRequest.leaseFrames = kRollingLeaseFrames;
        return ready() &&
               _api->setHandVisualAuthorityV1(_ownerToken, &leasedRequest) ==
                   rock::provider::RockProviderResultV1::Ok;
    }

    void RockApiClient::clearHandVisualAuthority(
        const rock::provider::RockProviderHand hand) const
    {
        if (ready()) {
            (void)_api->clearHandVisualAuthorityV1(_ownerToken, hand);
        }
    }

    bool RockApiClient::publishNativeAnimationRuntime(
        const rock::provider::RockProviderNativeAnimationRuntimePublicationV1& publication) const
    {
        auto leasedPublication = publication;
        leasedPublication.leaseFrames = kRollingLeaseFrames;
        return ready() &&
               _api->publishNativeAnimationRuntimeV1(
                   _ownerToken,
                   &leasedPublication) ==
                   rock::provider::RockProviderResultV1::Ok;
    }

    void RockApiClient::clearNativeAnimationRuntime() const
    {
        if (ready()) {
            (void)_api->clearNativeAnimationRuntimeV1(_ownerToken);
        }
    }

    bool RockApiClient::publishDebugOverlay(
        const rock::provider::RockProviderDebugOverlayPublicationV1& publication) const
    {
        auto leasedPublication = publication;
        leasedPublication.leaseFrames = kRollingLeaseFrames;
        return ready() &&
               _api->publishDebugOverlayV1(_ownerToken, &leasedPublication) ==
                   rock::provider::RockProviderResultV1::Ok;
    }

    void RockApiClient::clearDebugOverlay() const
    {
        if (ready()) {
            (void)_api->clearDebugOverlayV1(_ownerToken);
        }
    }
}
