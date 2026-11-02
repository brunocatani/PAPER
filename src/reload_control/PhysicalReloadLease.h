#pragma once
#include <PAPER/PhysicalReload.h>

namespace paper::physical_reload {
    // Value policy: no engine pointers cross the DLL contract or enter persistence.
    // The provider binds the native equipped item separately.
    struct Lease {
        api::physical_reload::Request request{};
        api::OwnerToken owner{};
        std::uint64_t expires{};
        void clear(){*this={};}
        [[nodiscard]] bool manualCycle() const {
            return (request.flags&static_cast<std::uint32_t>(api::physical_reload::Flag::ManualCycle))!=0;
        }
        api::Status set(api::OwnerToken token,const api::physical_reload::Request& value,std::uint64_t frame) {
            using R=api::Status;
            if(!token||!value.weaponFormId||!value.leaseFrames||value.leaseFrames>api::physical_reload::kMaxLeaseFrames||
                value.flags&~api::physical_reload::kKnownFlags)return R::InvalidArgument;
            if(value.observedFrame>frame+1||frame>value.observedFrame+1)return R::StaleSnapshot;
            if(owner&&owner!=token&&frame<=expires)return R::OwnerConflict;
            request=value;owner=token;expires=value.observedFrame+value.leaseFrames;
            return R::Ok;
        }
    };
}
