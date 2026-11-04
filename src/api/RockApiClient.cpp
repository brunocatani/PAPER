#include "api/RockApiClient.h"

#include "PaperLog.h"
#include "api/RequiredUpdates.h"

#include <cstring>
#include <Windows.h>
#include "support/Fo4VrRuntime.h"

namespace paper
{
    namespace
    {
        constexpr std::uint32_t kRollingLeaseFrames = 3;

        template<class... Permission>
        [[nodiscard]] constexpr std::uint32_t permissions(Permission... values)
        {
            return (static_cast<std::uint32_t>(values) | ...);
        }
    }

    RockApiClient& rockApiClient()
    {
        static RockApiClient client;
        return client;
    }

    bool RockApiClient::initialize() {
        if (ready()) return true;
        const auto module=GetModuleHandleA("ROCK.dll");
        if (!module) {
            PAPER_LOG_ERROR(Api,"ROCK.dll is not loaded; PAPER stays idle");
            return false;
        }
        const auto query=reinterpret_cast<rock::api::QueryInterfaceV1>(GetProcAddress(module,rock::api::kQueryExportName));
        if (!query) {
            required_updates::noteRockMissingDiscovery();
            return false;
        }
        auto status=_client.connect(query,"PAPER");
        if (status!=rock::api::Status::Ok) {
            PAPER_LOG_ERROR(Api,"ROCK modular registration failed: {}",static_cast<std::uint32_t>(status));
            required_updates::noteRockRefusal(status,"Core");
            return false;
        }
        namespace api=rock::api;
        const char* missing{};
        if ((status=_client.acquire(permissions(api::core::Permission::Read),_core))!=api::Status::Ok) missing="Core";
        else if ((status=_client.acquire(permissions(api::hands::Permission::Read),_hands))!=api::Status::Ok) missing="Hands";
        else if ((status=_client.acquire(permissions(api::grab::Permission::Read),_grab))!=api::Status::Ok) missing="Grab";
        else if ((status=_client.acquire(permissions(api::weapon::Permission::Read,api::weapon::Permission::Write),_weapon))!=api::Status::Ok) missing="Weapon";
        else if ((status=_client.acquire(permissions(api::weaponparts::Permission::Read,api::weaponparts::Permission::Write),_parts))!=api::Status::Ok) missing="WeaponParts";
        else if ((status=_client.acquire(permissions(api::animation::Permission::Read,api::animation::Permission::Write,
                     api::animation::Permission::Callbacks),_animation))!=api::Status::Ok) missing="Animation";
        if (missing) {
            PAPER_LOG_ERROR(Api,"ROCK {} interface unavailable for PAPER: {}",missing,static_cast<std::uint32_t>(status));
            required_updates::noteRockRefusal(status,missing);
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
        _core=nullptr; _hands=nullptr; _grab=nullptr; _weapon=nullptr; _parts=nullptr; _animation=nullptr;
        _reloadObservationEvidenceReady=false; _sample={};
    }
    bool RockApiClient::ready() const { return _ownerToken && _client.owner()==_ownerToken; }
    std::uint64_t RockApiClient::ownerToken() const { return _ownerToken; }
    void RockApiClient::setFrameContext(const rock::api::animation::AnimationPhaseContext& context) {
        _sample.frameIndex=context.frameIndex; _sample.worldGeneration=context.worldGeneration;
        _sample.skeletonGeneration=context.skeletonGeneration; _sample.providerGeneration=context.providerGeneration;
    }
    bool RockApiClient::queryHandFrame(rock::api::Hand hand,rock::api::hands::HandFrame& output) const {
        output={}; return ready() && _hands->getHandFrame(_ownerToken,hand,&output)==rock::api::Status::Ok;
    }

    bool RockApiClient::queryPresentedHandFrame(rock::api::Hand hand,rock::api::hands::HandFrame& output) const {
        output={}; return ready() && _hands->getPresentedHandFrame(_ownerToken,hand,&output)==rock::api::Status::Ok;
    }

    bool RockApiClient::registerAnimationPhaseCallback(
        rock::api::animation::PhaseCallback callback,
        void* userData)
    {
        if (!ready() || !callback) {
            return false;
        }
        if (_phaseCallbackToken != 0) {
            return true;
        }
        // Paper's authority lifecycle spans every phase of the frame.
        rock::api::animation::PhaseCallbackRegistration registration{};
        registration.phaseMask = rock::api::animation::kAllPhases;
        registration.callback = callback;
        registration.userData = userData;
        const auto status = _animation->registerAnimationPhaseCallback(
            _ownerToken,
            &registration,
            &_phaseCallbackToken);
        if (status != rock::api::Status::Ok) {
            PAPER_LOG_ERROR(Api,"ROCK refused the animation-phase callback: status {}",static_cast<std::uint32_t>(status));
            return false;
        }
        return true;
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
        rock::api::animation::NativeAnimationAuthorityRequest request{};
        request.flags = flags;
        request.weaponSlot = kRockWeaponSlot;
        request.leaseFrames = kRollingLeaseFrames;
        stamp(request);
        const auto result = _animation->setNativeAnimationAuthority(
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
            (void)_animation->clearNativeAnimationAuthority(_ownerToken);
        }
        _publishedAuthorityFlags = 0;
    }

    bool RockApiClient::queryNativeAnimationAuthorityState(
        rock::api::animation::NativeAnimationAuthorityState& outState) const
    {
        outState = {};
        return ready() && _animation->getNativeAnimationAuthorityState(_ownerToken,&outState)==rock::api::Status::Ok;
    }

    bool RockApiClient::queryEquippedWeaponGripState(paper::RockWeaponGripState& outState) const {
        outState={};
        if (!ready() || _weapon->getGrip(_ownerToken,kRockWeaponSlot,&outState)!=rock::api::Status::Ok) return false;
        outState.weaponNode=reinterpret_cast<std::uintptr_t>(fo4vr::getFirstPersonWeaponNode());
        return true;
    }

    bool RockApiClient::queryEquippedWeaponHandling(paper::RockWeaponHandling& outHandling) const
    {
        outHandling = {};
        return ready() &&
               _weapon->getWeaponState(_ownerToken,kRockWeaponSlot,&outHandling.state)==rock::api::Status::Ok &&
               _weapon->getHandlingAuthority(_ownerToken,&outHandling.authority)==rock::api::Status::Ok;
    }

    bool RockApiClient::queryHandInteractionState(
        const rock::api::Hand hand,
        rock::api::grab::HandInteractionState& outState) const
    {
        outState = {};
        return ready() &&
               _grab->getHandInteractionState(
                   _ownerToken,
                   hand,
                   &outState) ==
                   rock::api::Status::Ok;
    }

    bool RockApiClient::queryWeaponPartGripState(
        const rock::api::Hand hand,
        rock::api::weaponparts::GripState& outState) const
    {
        outState = {};
        return ready() && _parts->getGripState(_ownerToken,hand,&outState)==rock::api::Status::Ok;
    }

    bool RockApiClient::querySelectedAuthoredGripPose(
        rock::api::weapon::AuthoredGripPose& outPose) const
    {
        outPose = {};
        return ready() &&
               _weapon->getAuthoredGripPose(
                   _ownerToken,
                   kRockWeaponSlot,
                   &outPose) ==
                   rock::api::Status::Ok;
    }

    bool RockApiClient::queryPresentedHandPose(
        const rock::api::Hand hand,
        rock::api::hands::PresentedHandPose& outPose) const
    {
        outPose = {};
        return ready() &&
               _hands->getPresentedHandPose(
                   _ownerToken,
                   hand,
                   &outPose) ==
                   rock::api::Status::Ok;
    }

    bool RockApiClient::queryEquippedWeaponClassification(
        rock::api::weapon::Classification& outClassification) const
    {
        outClassification = {};
        return ready() &&
               _weapon->getClassification(_ownerToken,kRockWeaponSlot,&outClassification)==rock::api::Status::Ok;
    }

    bool RockApiClient::reloadObservationEvidenceReady() const
    {
        return ready() && _reloadObservationEvidenceReady;
    }

    std::uint32_t RockApiClient::copyWeaponEvidenceDetails(rock::api::weaponparts::EvidenceDetail* output,std::uint32_t capacity,std::uint32_t& outTotal) const {
        return copyWeaponEvidenceDetails(kRockWeaponSlot,output,capacity,outTotal);
    }
    std::uint32_t RockApiClient::copyWeaponEvidenceDetails(std::uint32_t slot,rock::api::weaponparts::EvidenceDetail* output,std::uint32_t capacity,std::uint32_t& outTotal) const {
        outTotal=0;
        if (!reloadObservationEvidenceReady() || !output) return 0;
        const auto count=std::min(capacity,rock::api::weaponparts::kMaxEvidenceDetails);
        for (std::uint32_t i=0;i<count;++i) output[i]={};
        if (_parts->copyEvidenceDetails(_ownerToken,slot,output,count,&outTotal)!=rock::api::Status::Ok) {
            outTotal=0; return 0;
        }
        return std::min(outTotal,count);
    }
    bool RockApiClient::queryWeaponState(std::uint32_t slot,rock::api::weapon::WeaponState& outState) const {
        outState={};
        return ready() && _weapon->getWeaponState(_ownerToken,slot,&outState)==rock::api::Status::Ok;
    }
    bool RockApiClient::queryWeaponClassification(std::uint32_t slot,rock::api::weapon::Classification& outClassification) const {
        outClassification={};
        return ready() && _weapon->getClassification(_ownerToken,slot,&outClassification)==rock::api::Status::Ok;
    }
    rock::api::Status RockApiClient::copyWeaponSources(std::uint32_t slot,std::uint64_t generation,rock::api::weaponparts::Source* output,std::uint32_t capacity,std::uint32_t& outTotal) const {
        outTotal=0;
        if (!ready()) return rock::api::Status::NotReady;
        for (std::uint32_t i=0;i<capacity;++i) output[i]={};
        return _parts->copySources(_ownerToken,slot,generation,output,capacity,&outTotal);
    }
    bool RockApiClient::queryWeaponSourcePose(std::uint32_t slot,std::uint64_t generation,std::uint64_t key,rock::api::weaponparts::SourcePose& outPose) const {
        outPose={};
        return ready() && _parts->querySourcePose(_ownerToken,slot,generation,key,&outPose)==rock::api::Status::Ok;
    }
    bool RockApiClient::setWeaponDrives(std::uint32_t slot,std::span<const rock::api::weaponparts::DriveTarget> drives) const {
        return ready() && _parts->setDriveTargets(_ownerToken,slot,drives.data(),static_cast<std::uint32_t>(drives.size()))==rock::api::Status::Ok;
    }
    void RockApiClient::clearWeaponDrives(std::uint32_t slot) const {
        if (ready()) (void)_parts->clearDriveTargets(_ownerToken,slot);
    }
    bool RockApiClient::setAutomaticReloadBlocked(bool blocked) const {
        return ready() && _weapon->setAutomaticReloadBlocked(_ownerToken,blocked?1u:0u)==rock::api::Status::Ok;
    }
    std::uint32_t RockApiClient::copyWeaponEvidencePoints(std::uint32_t body,rock::api::Point3* output,std::uint32_t capacity) const {
        if (!reloadObservationEvidenceReady() || !output) return 0;
        const auto count=std::min(capacity,rock::api::weaponparts::kMaxEvidencePoints);
        std::uint32_t total=0;
        return _parts->copyEvidencePoints(_ownerToken,kRockWeaponSlot,body,output,count,&total)==rock::api::Status::Ok?std::min(total,count):0;
    }

    bool RockApiClient::setHandVisualAuthority(
        const rock::api::animation::HandVisualAuthorityRequest& request) const
    {
        auto leasedRequest = request;
        leasedRequest.leaseFrames = kRollingLeaseFrames;
        stamp(leasedRequest);
        return ready() &&
               _animation->setHandVisualAuthority(_ownerToken, &leasedRequest) ==
                   rock::api::Status::Ok;
    }

    void RockApiClient::clearHandVisualAuthority(
        const rock::api::Hand hand) const
    {
        if (ready()) {
            (void)_animation->clearHandVisualAuthority(_ownerToken, hand);
        }
    }

    bool RockApiClient::publishNativeAnimationRuntime(
        const rock::api::animation::NativeAnimationRuntimePublication& publication) const
    {
        auto leasedPublication = publication;
        leasedPublication.leaseFrames = kRollingLeaseFrames;
        stamp(leasedPublication);
        return ready() &&
               _animation->publishNativeAnimationRuntime(
                   _ownerToken,
                   &leasedPublication) ==
                   rock::api::Status::Ok;
    }

    void RockApiClient::clearNativeAnimationRuntime() const
    {
        if (ready()) {
            (void)_animation->clearNativeAnimationRuntime(_ownerToken);
        }
    }

    RE::NiAVObject* RockApiClient::resolveWeaponSource(std::uint64_t generation,std::uint64_t key) const {
        if (!ready() || !key || !generation) return nullptr;
        std::array<std::uint32_t,64> path{};
        std::size_t depth=0;
        for (;;) {
            std::uint64_t parent{}; std::uint32_t index{};
            if (_parts->querySourcePath(_ownerToken,kRockWeaponSlot,generation,key,&parent,&index)!=rock::api::Status::Ok) return nullptr;
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
