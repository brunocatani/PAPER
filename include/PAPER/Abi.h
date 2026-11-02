#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#define PAPER_CALL __cdecl

// PAPER's shared ABI vocabulary. Each interface header carries its own
// kMajor and kMinor; records and tables belong to their interface headers.
#pragma pack(push, 8)
namespace paper::api
{
    // Layout version of every size/version-tagged record. Interface versions
    // are separate and negotiated through discovery.
    inline constexpr std::uint32_t kRecordVersion = 1;

    enum class Status : std::uint32_t
    {
        Ok = 0,
        InvalidArgument = 1,
        UnsupportedVersion = 2,
        NotReady = 3,
        CapacityReached = 4,
        UnknownOwner = 5,
        PermissionDenied = 6,
        NotFound = 7,
        WrongThread = 8,
        OwnerConflict = 9,
        InvalidSize = 10,
        StaleSnapshot = 11,
        OutOfRange = 12,
        // Discovery.
        UnknownInterface = 100,
        UnsupportedMajor = 101,
        UnsupportedMinor = 102,
        TableTooSmall = 103,
        InternalError = 104,
    };

    // Issued by the Provider interface's registerConsumer; every other
    // interface takes the same token.
    using OwnerToken = std::uint64_t;

    // Interface IDs are permanent and never reused.
    enum class InterfaceId : std::uint32_t
    {
        Provider = 1,
        PhysicalReload = 2,
    };

    static_assert(sizeof(void*) == 8, "PAPER requires a 64-bit consumer");
    static_assert(sizeof(Status) == 4 && sizeof(InterfaceId) == 4);
}
#pragma pack(pop)
