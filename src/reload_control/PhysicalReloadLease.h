#pragma once
#include "api/PAPERPhysicalReloadApi.h"

namespace paper::physical_reload {
    // Value policy: no engine pointers cross the DLL contract or enter persistence.
    struct Lease {
        Request request{};
        std::uint64_t owner{}, expires{};
        std::uint32_t contractVersion{1},flags{};
        void clear(){*this={};}
        bool matches(const api::PaperRuntimeStateV1& state,std::uint64_t frame)const {
            return owner&&frame<=expires&&request.weaponFormId==state.weaponFormId&&
                request.weaponGenerationKey==state.weaponGenerationKey&&request.worldGeneration==state.worldGeneration&&
                request.skeletonGeneration==state.skeletonGeneration&&request.rockProviderGeneration==state.rockProviderGeneration&&
                request.paperProviderGeneration==state.paperProviderGeneration;
        }
        api::PaperResultV1 set(std::uint64_t token,const Request& value,const api::PaperRuntimeStateV1& state,std::uint64_t frame) {
            using R=api::PaperResultV1;
            if(!token||!value.weaponFormId||!value.weaponGenerationKey||!value.leaseFrames||value.leaseFrames>kMaximumLeaseFrames)
                return R::InvalidArgument;
            if(value.observedFrame>frame||frame-value.observedFrame>1)return R::StaleSnapshot;
            Lease candidate{value,token,frame+value.leaseFrames};
            if(!candidate.matches(state,frame))return R::StaleSnapshot;
            if(matches(state,frame)&&owner!=token)return R::OwnerConflict;
            *this=candidate;return R::Ok;
        }
    };
}
