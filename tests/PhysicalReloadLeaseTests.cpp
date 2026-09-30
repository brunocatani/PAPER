#include "reload_control/PhysicalReloadLease.h"
#include "reload_control/PhysicalReloadPolicy.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace paper;
    api::PaperRuntimeStateV1 state;state.weaponFormId=42;state.weaponGenerationKey=55;
    state.worldGeneration=1;state.skeletonGeneration=2;state.rockProviderGeneration=3;state.paperProviderGeneration=4;
    physical_reload::Request request;request.weaponFormId=42;request.weaponGenerationKey=55;request.observedFrame=100;
    request.worldGeneration=1;request.skeletonGeneration=2;request.rockProviderGeneration=3;request.paperProviderGeneration=4;
    physical_reload::Lease lease;
    assert(lease.set(7,request,state,100)==api::PaperResultV1::Ok);
    assert(lease.matches(state,102)&&!lease.matches(state,103));
    assert(lease.set(8,request,state,100)==api::PaperResultV1::OwnerConflict);
    auto changed=state;++changed.weaponGenerationKey;assert(!lease.matches(changed,100));
    assert(lease.set(7,request,changed,100)==api::PaperResultV1::StaleSnapshot);
    request.observedFrame=99;assert(lease.set(7,request,state,101)==api::PaperResultV1::StaleSnapshot);
    request.observedFrame=101;request.leaseFrames=0;assert(lease.set(7,request,state,101)==api::PaperResultV1::InvalidArgument);
    request.leaseFrames=121;assert(lease.set(7,request,state,101)==api::PaperResultV1::InvalidArgument);
    request.leaseFrames=2;assert(lease.set(7,request,state,101)==api::PaperResultV1::Ok);
    for(int field=0;field<4;++field){changed=state;switch(field){case 0:++changed.worldGeneration;break;case 1:++changed.skeletonGeneration;break;case 2:++changed.rockProviderGeneration;break;default:++changed.paperProviderGeneration;}
        assert(!lease.matches(changed,101));}
    lease.clear();assert(!lease.matches(state,101));
    // Reproduce a live supported rifle's hand detach: native pose eligibility
    // disappears, physical identity/ownership must survive. V1 stays unchanged.
    namespace policy=native_animation_authority_policy;
    policy::NativeAnimationCompatibilityObservation weapon;
    weapon.handlingStateValid=weapon.gripStateValid=weapon.weaponPresent=weapon.weaponIdentityCoherent=true;
    weapon.weaponFormId=42;weapon.weaponGenerationKey=55;weapon.weaponNode=88;
    assert(physical_reload::physicalWeaponReady(true,weapon));
    assert(policy::advanceNativeAnimationCompatibility({},weapon,true).compatible());
    weapon.partCarryActive=true;
    assert(!policy::advanceNativeAnimationCompatibility({},weapon,true).compatible());
    assert(physical_reload::physicalWeaponReady(true,weapon));
    weapon.gripStateValid=false;weapon.firingHandIsLeft=true;
    assert(physical_reload::physicalWeaponReady(true,weapon));
    assert(!physical_reload::physicalWeaponReady(false,weapon));
    for(int i=0;i<6;++i) {
        auto invalid=weapon;
        switch(i){case 0:invalid.handlingStateValid=false;break;case 1:invalid.weaponPresent=false;break;
            case 2:invalid.weaponIdentityCoherent=false;break;case 3:invalid.weaponFormId=0;break;
            case 4:invalid.weaponGenerationKey=0;break;default:invalid.akimboActive=true;}
        assert(!physical_reload::physicalWeaponReady(true,invalid));
    }
    physical_reload::v2::Request second;
    second.weaponFormId=42;second.weaponGenerationKey=55;second.observedFrame=101;
    second.worldGeneration=1;second.skeletonGeneration=2;second.rockProviderGeneration=3;second.paperProviderGeneration=4;
    second.flags=static_cast<std::uint32_t>(physical_reload::v2::Flag::ManualCycle);
    assert(lease.set(7,physical_reload::identity(second),state,101)==api::PaperResultV1::Ok);
    lease.contractVersion=2;lease.flags=second.flags;
    state.frameIndex=101;state.statusFlags=0x0f;
    physical_reload::Environment environment{state,true};
    assert(environment.current(102)&&!environment.current(103)&&!environment.current(100));
    assert(lease.matches(environment.identity,102));
    environment.ready=false;assert(!environment.current(102));
    // Renewing through the old policy restores old semantics, never inherits
    // an earlier V2 manual-cycle flag accidentally.
    assert(lease.set(7,request,state,101)==api::PaperResultV1::Ok);
    assert(lease.contractVersion==1&&lease.flags==0);
    static_assert(sizeof(physical_reload::ApiV1)==32&&sizeof(physical_reload::Request)==48&&sizeof(physical_reload::State)==40);
    std::cout<<"PASS physical reload ownership, renewal, expiry, conflicts and generation loss\n";
}
