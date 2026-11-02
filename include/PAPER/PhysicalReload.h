#pragma once
#include "Abi.h"

// Physical reload control: a consumer that drives a reload or manual cycle
// with its own hands takes the equipped weapon away from PAPER's native
// reload animation for a short, renewed lease.
#pragma pack(push, 8)
namespace paper::api::physical_reload
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::PhysicalReload;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;
    inline constexpr std::uint32_t kMaxLeaseFrames = 120;

    enum class Flag : std::uint32_t
    {
        // The consumer owns bolt cycling, chambering, casing ejection and
        // their sounds; PAPER suppresses the native automatic cycle.
        ManualCycle = 1u << 0,
    };
    inline constexpr std::uint32_t kKnownFlags = 1;

    /*
     * Binds the exact native equipped item (form, equipped data and
     * instance). observedFrame is the ROCK frame the caller acted on and must
     * be within one frame of PAPER's; the lease ends after observedFrame +
     * leaseFrames (1..kMaxLeaseFrames), when the equipped item is replaced, on
     * a world reset, or when the owner is revoked. Renew it every frame while
     * physical control lasts. No PAPER animation snapshot or weapon-part
     * readiness is required. Generation fields describe the caller.
     */
    struct Request
    {
        std::uint32_t size{ sizeof(Request) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t weaponFormId{ 0 };
        std::uint32_t leaseFrames{ 2 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t observedFrame{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t flags{ 0 };
    };

    // active 0: no live lease; every other field is zero.
    struct State
    {
        std::uint32_t size{ sizeof(State) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t active{ 0 };
        std::uint32_t weaponFormId{ 0 };
        OwnerToken owner{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t expiresAfterFrame{ 0 };
        std::uint32_t flags{ 0 };
        std::uint32_t reserved{ 0 };
    };

    // Every call runs on the game thread (PAPER's owner thread) and copies its
    // arguments. Owner tokens come from the Provider interface.
    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;
        // Needs the AnimationAuthority capability. OwnerConflict while another
        // owner holds a live lease or any consumer holds animation authority;
        // StaleSnapshot when the item or frame does not match; NotReady before
        // PAPER is ready.
        Status(PAPER_CALL* acquire)(OwnerToken owner, const Request* request) noexcept;
        // Ends the caller's lease; OwnerConflict while another owner's lease
        // is live.
        Status(PAPER_CALL* clear)(OwnerToken owner) noexcept;
        // Needs the RuntimeState capability.
        Status(PAPER_CALL* read)(OwnerToken owner, State* output) noexcept;
    };

    static_assert(sizeof(Request) == 48 && alignof(Request) == 8);
    static_assert(sizeof(State) == 48 && alignof(State) == 8);
    static_assert(sizeof(Api) == 24);
}
#pragma pack(pop)
