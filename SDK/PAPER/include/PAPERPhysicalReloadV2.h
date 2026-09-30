#pragma once
#pragma pack(push,8)
#include "PAPERPhysicalReloadApi.h"

namespace paper::physical_reload::v2 {
    inline constexpr std::uint32_t kVersion=2;
    inline constexpr char kExport[]="PAPERAPI_QueryPhysicalReloadV2";
    enum class Flag : std::uint32_t { ManualCycle=1 };
    inline constexpr std::uint32_t kKnownFlags=1;

    // Physical control of the same native equipped item, including support-hand
    // carry and either firing hand. This does not enable PAPER's native hand IK
    // for those topologies. Missing/stale identity, lifecycle loss, akimbo and
    // competing animation authority still reject/expire the lease.
    // ManualCycle opts out of the native automatic bolt action; the consumer
    // owns cycling, chambering, casing ejection and the corresponding sounds.
    // V1 remains separately discoverable with its original readiness semantics.
    // Owner-thread only. Register RuntimeState|AnimationAuthority first. Copy-in
    // values; renew within leaseFrames and clear when physical control ends.
    struct Request {
        std::uint32_t size{sizeof(Request)}, version{kVersion};
        std::uint32_t weaponFormId{}, leaseFrames{2};
        std::uint64_t weaponGenerationKey{}, observedFrame{};
        std::uint32_t worldGeneration{}, skeletonGeneration{}, rockProviderGeneration{}, paperProviderGeneration{};
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
    static_assert(sizeof(Request)==56 && sizeof(State)==48 && sizeof(Api)==32);
    static_assert(alignof(Request)==8 && alignof(State)==8);
}
#pragma pack(pop)
extern "C" PAPER_API const paper::physical_reload::v2::Api* PAPER_CALL
PAPERAPI_QueryPhysicalReloadV2(std::uint32_t version,std::uint32_t bytes) noexcept;
