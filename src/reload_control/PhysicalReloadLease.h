#pragma once
#include "api/PAPERPhysicalReloadApi.h"
#include "api/PAPERPhysicalReloadV3.h"

namespace paper::physical_reload {
    // Value policy: no engine pointers cross the DLL contract or enter persistence.
    struct Lease {
        Request request{};
        std::uint64_t owner{}, expires{};
        std::uint32_t contractVersion{1},flags{};
        void clear(){*this={};}
        api::PaperResultV1 setEarly(std::uint64_t token,const v3::Request& value,std::uint64_t frame) {
            using R=api::PaperResultV1;
            if(!token||!value.weaponFormId||!value.leaseFrames||value.leaseFrames>kMaximumLeaseFrames||value.flags&~v3::kKnownFlags)return R::InvalidArgument;
            if(value.observedFrame>frame+1||frame>value.observedFrame+1)return R::StaleSnapshot;
            if(owner&&owner!=token&&frame<=expires)return R::OwnerConflict;
            request={};request.weaponFormId=value.weaponFormId;request.weaponGenerationKey=value.weaponGenerationKey;
            request.observedFrame=value.observedFrame;request.leaseFrames=value.leaseFrames;
            request.worldGeneration=value.worldGeneration;request.skeletonGeneration=value.skeletonGeneration;
            request.rockProviderGeneration=value.rockProviderGeneration;
            owner=token;expires=value.observedFrame+value.leaseFrames;contractVersion=v3::kVersion;flags=value.flags;
            return R::Ok;
        }
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
