#pragma once
#pragma pack(push,8)
#include "PAPERPhysicalReloadApi.h"

namespace paper::physical_reload::v3 {
    inline constexpr std::uint32_t kVersion=3;
    inline constexpr char kExport[]="PAPERAPI_QueryPhysicalReloadV3";
    enum class Flag : std::uint32_t { ManualCycle=1 };
    inline constexpr std::uint32_t kKnownFlags=1;

    // Early physical-reload takeover of the exact native equipped item.
    // No PAPER animation snapshot, collision publication, or weapon-part
    // readiness is required. The owner supplies the current ROCK frame.
    // Identity is bound to the native equipped-data and instance pointers;
    // replacing the equipped item or expiration ends the lease. World reset
    // and owner revocation also end it. Owner-thread only, same capabilities
    // as V2. V1/V2 remain independently available with unchanged semantics.
    // Generation fields describe the caller; PAPER does not require its own
    // per-weapon publication to have caught up before yielding authority.
    struct Request {
        std::uint32_t size{sizeof(Request)}, version{kVersion};
        std::uint32_t weaponFormId{}, leaseFrames{2};
        std::uint64_t weaponGenerationKey{}, observedFrame{};
        std::uint32_t worldGeneration{}, skeletonGeneration{}, rockProviderGeneration{};
        std::uint32_t flags{};
    };
    struct State {
        std::uint32_t size{sizeof(State)}, version{kVersion};
        std::uint32_t active{}, weaponFormId{};
        std::uint64_t owner{}, weaponGenerationKey{}, expiresAfterFrame{};
        std::uint32_t flags{};
    };
    struct Api {
        std::uint32_t size{sizeof(Api)}, version{kVersion};
        api::PaperResultV1(PAPER_CALL* acquire)(std::uint64_t,const Request*) noexcept{};
        api::PaperResultV1(PAPER_CALL* clear)(std::uint64_t) noexcept{};
        api::PaperResultV1(PAPER_CALL* read)(std::uint64_t,State*) noexcept{};
    };
    using Query=const Api*(PAPER_CALL*)(std::uint32_t version,std::uint32_t bytes) noexcept;
    static_assert(sizeof(Request)==48 && sizeof(State)==48 && sizeof(Api)==32);
    static_assert(alignof(Request)==8 && alignof(State)==8);
}
#pragma pack(pop)
extern "C" PAPER_API const paper::physical_reload::v3::Api* PAPER_CALL
PAPERAPI_QueryPhysicalReloadV3(std::uint32_t version,std::uint32_t bytes) noexcept;
