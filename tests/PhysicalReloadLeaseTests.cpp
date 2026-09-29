#include "reload_control/PhysicalReloadLease.h"
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
    std::cout<<"PASS physical reload ownership, renewal, expiry, conflicts and generation loss\n";
}
