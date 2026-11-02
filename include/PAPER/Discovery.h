#pragma once
#include "Abi.h"

// Resolve kQueryExportName from PAPER.dll and ask for an interface by ID,
// exact major, minimum minor and minimum table size. A major this PAPER does
// not serve returns UnsupportedMajor and no table; PAPER logs the refusal once.
// The pre-2.0 exports (PAPERAPI_GetProviderApi,
// PAPERAPI_GetProviderDescriptorV1 and PAPERAPI_QueryPhysicalReloadV1/V2/V3)
// only refuse outdated consumers; their names are never reused.
#pragma pack(push, 8)
namespace paper::api
{
    struct InterfaceDescriptorV1
    {
        std::uint32_t size{ sizeof(InterfaceDescriptorV1) };
        InterfaceId interfaceId{};
        std::uint32_t major{};
        std::uint32_t minor{};
        std::uint32_t tableByteSize{};
        // Owner tokens come from the Provider interface; a consumer checks it
        // negotiated at least this Provider version.
        std::uint32_t requiredProviderMajor{};
        std::uint32_t requiredProviderMinor{};
        std::uint32_t reserved{};
        const void* table{};
    };

    // Any thread. The descriptor and table stay valid for the process lifetime.
    using QueryInterfaceV1 = Status(PAPER_CALL*)(InterfaceId interfaceId,
        std::uint32_t exactMajor, std::uint32_t minimumMinor,
        std::uint32_t minimumTableBytes,
        const InterfaceDescriptorV1** outDescriptor) noexcept;
    inline constexpr char kQueryExportName[] = "PAPERAPI_QueryInterfaceV1";

    static_assert(sizeof(InterfaceDescriptorV1) == 40);
}
#pragma pack(pop)
