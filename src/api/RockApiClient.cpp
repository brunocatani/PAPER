#include "api/RockApiClient.h"

#include "PaperLog.h"

#include <cstring>
#include <Windows.h>
#include "support/Fo4VrRuntime.h"

namespace paper
{
    namespace
    {
        constexpr std::uint32_t kRollingLeaseFrames = 3;
    }

    RockApiClient& rockApiClient()
    {
        static RockApiClient client;
        return client;
    }

    bool RockApiClient::initialize() {
        if (ready()) return true;
        const auto module=GetModuleHandleA("ROCK.dll");
        const auto query=module?reinterpret_cast<rock::api::QueryInterfaceV1>(GetProcAddress(module,rock::api::kQueryExportName)):nullptr;
        auto status=_client.connect(query,"PAPER");
        if (status!=rock::api::Status::Ok) {
            PAPER_LOG_ERROR(Api,"ROCK modular registration failed: {}",static_cast<std::uint32_t>(status));
            return false;
        }
        if (_client.acquire(5,_core)!=rock::api::Status::Ok ||
            _client.acquire(1,_hands)!=rock::api::Status::Ok ||
            _client.acquire(1,_grab)!=rock::api::Status::Ok ||
            _client.acquire(1,_weapon)!=rock::api::Status::Ok ||
            _client.acquire(1,_parts)!=rock::api::Status::Ok ||
            _client.acquire(3,_animation)!=rock::api::Status::Ok ||
            _client.acquire(2,_diagnostics)!=rock::api::Status::Ok) {
            PAPER_LOG_ERROR(Api,"ROCK is missing a required modular PAPER interface");
            (void)_client.close(); return false;
        }
        _ownerToken=_client.owner();
        _reloadObservationEvidenceReady=true;
        PAPER_LOG_INFO(Api,"Registered modular ROCK interfaces for owner {:016X}",_ownerToken);
        return true;
    }
    void RockApiClient::shutdown() {
        if (!_ownerToken) return;
        const auto status=_client.close();
        if (status!=rock::api::Status::Ok && status!=rock::api::Status::OwnerNotRegistered) {
            PAPER_LOG_ERROR(Api,"ROCK owner teardown failed: {}",static_cast<std::uint32_t>(status)); return;
        }
        _ownerToken=0; _phaseCallbackToken=0; _publishedAuthorityFlags=0;
        _core=nullptr; _hands=nullptr; _grab=nullptr; _weapon=nullptr; _parts=nullptr; _animation=nullptr; _diagnostics=nullptr;
        _reloadObservationEvidenceReady=false; _sample={};
    }
    bool RockApiClient::ready() const { return _ownerToken && _client.owner()==_ownerToken; }
    std::uint64_t RockApiClient::ownerToken() const { return _ownerToken; }
    void RockApiClient::setFrameContext(const rock::api::core::AnimationPhaseContextV1& context) {
        _sample.frameIndex=context.frameIndex; _sample.worldGeneration=context.worldGeneration;
        _sample.skeletonGeneration=context.skeletonGeneration; _sample.providerGeneration=context.providerGeneration;
    }
    bool RockApiClient::queryPresentedHandFrame(rock::api::Hand hand,rock::api::hands::HandFrameV1& output) const {
        output={}; return ready() && _hands->getPresentedHandFrameV1(_ownerToken,hand,&output)==rock::api::Status::Ok;
    }

    bool RockApiClient::registerAnimationPhaseCallback(
        rock::api::core::PhaseCallbackV1 callback,
        void* userData)
    {
        if (!ready() || !callback) {
            return false;
        }
        if (_phaseCallbackToken != 0) {
            return true;
        }
        return _core->registerAnimationPhaseCallbackV1(
                   _ownerToken,
                   callback,
                   userData,
                   &_phaseCallbackToken) ==
               rock::api::Status::Ok;
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
        rock::api::animation::NativeAnimationAuthorityRequestV1 request{};
        request.flags = flags;
        request.leaseFrames = kRollingLeaseFrames;
        stamp(request);
        const auto result = _animation->setNativeAnimationAuthorityV1(
            _ownerToken,
            &request);
        if (result != rock::api::Status::Ok) {
            return false;
        }
        _publishedAuthorityFlags = flags;
        return true;
    }

    void RockApiClient::clearNativeAnimationAuthority()
    {
        if (ready() && _publishedAuthorityFlags != 0) {
            (void)_animation->clearNativeAnimationAuthorityV1(_ownerToken);
        }
        _publishedAuthorityFlags = 0;
    }

    bool RockApiClient::queryNativeAnimationAuthorityState(
        rock::api::animation::NativeAnimationAuthorityStateV1& outState) const
    {
        outState = {};
        return ready() && _animation->getNativeAnimationAuthorityStateV1(_ownerToken,&outState)==rock::api::Status::Ok;
    }

    bool RockApiClient::queryEquippedWeaponGripState(paper::RockWeaponGripState& outState) const {
        outState={};
        if (!ready() || _weapon->getEquippedWeaponGripStateV1(_ownerToken,&outState)!=rock::api::Status::Ok) return false;
        outState.weaponNode=reinterpret_cast<std::uintptr_t>(fo4vr::getFirstPersonWeaponNode());
        return true;
    }

    bool RockApiClient::queryEquippedWeaponHandlingState(
        rock::api::weapon::EquippedWeaponHandlingStateV1& outState) const
    {
        outState = {};
        return ready() &&
               _weapon->getEquippedWeaponHandlingStateV1(_ownerToken,&outState)==rock::api::Status::Ok;
    }

    bool RockApiClient::queryHandInteractionState(
        const rock::api::Hand hand,
        rock::api::grab::HandInteractionStateV1& outState) const
    {
        outState = {};
        return ready() && _grab->getHandInteractionStateV1 &&
               _grab->getHandInteractionStateV1(
                   _ownerToken,
                   hand,
                   &outState) ==
                   rock::api::Status::Ok;
    }

    bool RockApiClient::queryWeaponPartGripState(
        const rock::api::Hand hand,
        rock::api::weaponparts::WeaponPartGripStateV1& outState) const
    {
        outState = {};
        return ready() && _parts->getWeaponPartGripStateV1(_ownerToken,hand,&outState)==rock::api::Status::Ok;
    }

    bool RockApiClient::querySelectedAuthoredGripPose(
        rock::api::weapon::AuthoredGripPoseV1& outPose) const
    {
        outPose = {};
        return ready() &&
               _weapon->getSelectedAuthoredGripPoseV1(
                   _ownerToken,
                   &outPose) ==
                   rock::api::Status::Ok;
    }

    bool RockApiClient::queryPresentedHandPose(
        const rock::api::Hand hand,
        rock::api::hands::PresentedHandPoseV1& outPose) const
    {
        outPose = {};
        return ready() && _hands->getPresentedHandPoseV1 &&
               _hands->getPresentedHandPoseV1(
                   _ownerToken,
                   hand,
                   &outPose) ==
                   rock::api::Status::Ok;
    }

    bool RockApiClient::queryEquippedWeaponClassification(
        rock::api::weapon::WeaponClassificationV1& outClassification) const
    {
        outClassification = {};
        return ready() &&
               _weapon->queryEquippedWeaponClassificationV1(_ownerToken,&outClassification)==rock::api::Status::Ok;
    }

    bool RockApiClient::reloadObservationEvidenceReady() const
    {
        return ready() && _reloadObservationEvidenceReady;
    }

    std::uint32_t RockApiClient::weaponEvidenceDetailCount() const {
        std::uint32_t count=0;
        if (reloadObservationEvidenceReady()) (void)_parts->getWeaponEvidenceDetailCountV1(_ownerToken,&count);
        return count;
    }
    std::uint32_t RockApiClient::copyWeaponEvidenceDetails(rock::api::weaponparts::WeaponEvidenceDetailV1* output,std::uint32_t capacity) const {
        if (!reloadObservationEvidenceReady() || !output) return 0;
        const auto count=std::min(capacity,rock::api::weaponparts::kMaxEvidenceDetails);
        for (std::uint32_t i=0;i<count;++i) output[i]={};
        std::uint32_t copied=0;
        return _parts->copyWeaponEvidenceDetailsV1(_ownerToken,output,count,&copied)==rock::api::Status::Ok?copied:0;
    }
    std::uint32_t RockApiClient::copyWeaponEvidencePoints(std::uint32_t body,rock::api::Point3* output,std::uint32_t capacity) const {
        if (!reloadObservationEvidenceReady() || !output) return 0;
        std::uint32_t copied=0;
        return _parts->copyWeaponEvidenceDetailPointsV1(_ownerToken,body,output,std::min(capacity,rock::api::weaponparts::kMaxEvidencePoints),&copied)==rock::api::Status::Ok?copied:0;
    }

    bool RockApiClient::setHandVisualAuthority(
        const rock::api::animation::HandVisualAuthorityRequestV1& request) const
    {
        auto leasedRequest = request;
        leasedRequest.leaseFrames = kRollingLeaseFrames;
        stamp(leasedRequest);
        return ready() &&
               _animation->setHandVisualAuthorityV1(_ownerToken, &leasedRequest) ==
                   rock::api::Status::Ok;
    }

    void RockApiClient::clearHandVisualAuthority(
        const rock::api::Hand hand) const
    {
        if (ready()) {
            (void)_animation->clearHandVisualAuthorityV1(_ownerToken, hand);
        }
    }

    bool RockApiClient::publishNativeAnimationRuntime(
        const rock::api::animation::NativeAnimationRuntimePublicationV1& publication) const
    {
        auto leasedPublication = publication;
        leasedPublication.leaseFrames = kRollingLeaseFrames;
        stamp(leasedPublication);
        return ready() &&
               _animation->publishNativeAnimationRuntimeV1(
                   _ownerToken,
                   &leasedPublication) ==
                   rock::api::Status::Ok;
    }

    void RockApiClient::clearNativeAnimationRuntime() const
    {
        if (ready()) {
            (void)_animation->clearNativeAnimationRuntimeV1(_ownerToken);
        }
    }

    bool RockApiClient::publishDebugOverlay(
        const rock::api::diagnostics::DebugOverlayPublicationV1& publication) const
    {
        auto leasedPublication = publication;
        leasedPublication.leaseFrames = kRollingLeaseFrames;
        stamp(leasedPublication);
        return ready() &&
               _diagnostics->publishDebugOverlayV1(_ownerToken, &leasedPublication) ==
                   rock::api::Status::Ok;
    }

    void RockApiClient::clearDebugOverlay() const
    {
        if (ready()) {
            (void)_diagnostics->clearDebugOverlayV1(_ownerToken);
        }
    }

    RE::NiAVObject* RockApiClient::resolveWeaponSource(std::uint64_t generation,std::uint64_t key) const {
        if (!ready() || !key || !generation) return nullptr;
        std::array<std::uint32_t,64> path{};
        std::size_t depth=0;
        for (;;) {
            std::uint64_t parent{}; std::uint32_t index{};
            if (_parts->querySourcePath(_ownerToken,generation,key,&parent,&index)!=rock::api::Status::Ok) return nullptr;
            if (!parent) break;
            if (depth==path.size()) return nullptr;
            path[depth++]=index; key=parent;
        }
        RE::NiAVObject* node=fo4vr::getFirstPersonWeaponNode();
        while (node && depth) {
            auto* parent=node->IsNode();
            const auto index=path[--depth];
            if (!parent || index>=parent->children.size()) return nullptr;
            node=parent->children[static_cast<decltype(parent->children.size())>(index)].get();
        }
        return node;
    }
}
