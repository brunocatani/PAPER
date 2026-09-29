#pragma once
#pragma pack(push,8)
#include "PAPERApi.h"

namespace paper::physical_reload {
    inline constexpr std::uint32_t kVersion=1, kMaximumLeaseFrames=120;

    // Independently negotiated, opt-in contract. Published PAPER V1 is unchanged.
    // Register a normal PAPER consumer with RuntimeState|AnimationAuthority first.
    // Calls run on PAPER's owner thread (ROCK control callbacks), copy arguments,
    // and reject an active native reload or competing animation authority.
    struct Request {
        std::uint32_t size{sizeof(Request)}, version{kVersion};
        std::uint32_t weaponFormId{}, leaseFrames{2};
        std::uint64_t weaponGenerationKey{}, observedFrame{};
        std::uint32_t worldGeneration{}, skeletonGeneration{}, rockProviderGeneration{}, paperProviderGeneration{};
    };
    struct State {
        std::uint32_t size{sizeof(State)}, version{kVersion};
        std::uint32_t active{}, weaponFormId{};
        std::uint64_t owner{}, weaponGenerationKey{}, expiresAfterFrame{};
    };
    struct ApiV1 {
        std::uint32_t size{sizeof(ApiV1)}, version{kVersion};
        api::PaperResultV1(PAPER_CALL* acquire)(std::uint64_t,const Request*) noexcept{};
        api::PaperResultV1(PAPER_CALL* clear)(std::uint64_t) noexcept{};
        api::PaperResultV1(PAPER_CALL* read)(std::uint64_t,State*) noexcept{};
    };
    using Query=const ApiV1*(PAPER_CALL*)(std::uint32_t version,std::uint32_t bytes) noexcept;
    static_assert(sizeof(Request)==48 && sizeof(State)==40);
    static_assert(alignof(Request)==8 && alignof(State)==8 && sizeof(ApiV1)==32);
}
#pragma pack(pop)

extern "C" PAPER_API const paper::physical_reload::ApiV1* PAPER_CALL
PAPERAPI_QueryPhysicalReloadV1(std::uint32_t version,std::uint32_t bytes) noexcept;
