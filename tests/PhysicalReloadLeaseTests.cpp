#include "reload_control/PhysicalReloadLease.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace paper;
    namespace contract=api::physical_reload;
    contract::Request request;request.weaponFormId=42;request.observedFrame=101;
    physical_reload::Lease lease;
    // Native item ownership begins before collision generation or a PAPER
    // weapon snapshot exists: no generation key is required.
    assert(lease.set(7,request,100)==api::Status::Ok);
    assert(lease.owner==7&&lease.expires==103&&lease.request.weaponGenerationKey==0&&!lease.manualCycle());
    // A live lease belongs to its owner until it expires; the owner renews it.
    assert(lease.set(8,request,101)==api::Status::OwnerConflict);
    assert(lease.set(8,request,104)==api::Status::StaleSnapshot);
    request.observedFrame=103;assert(lease.set(8,request,103)==api::Status::OwnerConflict);
    request.observedFrame=104;assert(lease.set(8,request,104)==api::Status::Ok&&lease.owner==8);
    // The caller's frame must be within one frame of PAPER's.
    assert(lease.set(8,request,102)==api::Status::StaleSnapshot);
    assert(lease.set(8,request,106)==api::Status::StaleSnapshot);
    request.leaseFrames=0;assert(lease.set(8,request,104)==api::Status::InvalidArgument);
    request.leaseFrames=contract::kMaxLeaseFrames+1;assert(lease.set(8,request,104)==api::Status::InvalidArgument);
    request.leaseFrames=2;request.flags=2;assert(lease.set(8,request,104)==api::Status::InvalidArgument);
    request.flags=static_cast<std::uint32_t>(contract::Flag::ManualCycle);
    assert(lease.set(8,request,104)==api::Status::Ok&&lease.manualCycle());
    request.weaponFormId=0;request.flags=0;assert(lease.set(8,request,104)==api::Status::InvalidArgument);
    assert(lease.set(0,request,104)==api::Status::InvalidArgument);
    lease.clear();assert(lease.owner==0&&!lease.manualCycle());
    std::cout<<"PASS physical reload ownership, renewal, expiry, frame admission and flags\n";
}
