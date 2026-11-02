#pragma once
#include <PAPER/Discovery.h>
#include <PAPER/Provider.h>
#include <span>

namespace paper::discovery {
    // Every interface takes owner tokens from the Provider major PAPER serves.
    template <class Table>
    constexpr api::InterfaceDescriptorV1 describe(const Table& table) noexcept
    {
        return { sizeof(api::InterfaceDescriptorV1), Table::interfaceId, Table::majorVersion, Table::minorVersion,
            sizeof(Table), api::provider::kMajor, api::provider::kMinor, 0, &table };
    }

    // Exact major, minimum minor and minimum table size. A known interface
    // asked for a major PAPER does not serve is refused with UnsupportedMajor
    // and no table; the caller logs that refusal.
    inline api::Status query(std::span<const api::InterfaceDescriptorV1> entries,
        api::InterfaceId id, std::uint32_t major, std::uint32_t minor,
        std::uint32_t bytes, const api::InterfaceDescriptorV1** output) noexcept
    {
        if (!output) return api::Status::InvalidArgument;
        *output = nullptr;
        if (!major || !bytes) return api::Status::InvalidArgument;
        bool knownId = false;
        for (const auto& descriptor : entries) {
            if (descriptor.interfaceId != id) continue;
            knownId = true;
            if (descriptor.major != major) continue;
            if (descriptor.minor < minor) return api::Status::UnsupportedMinor;
            if (descriptor.tableByteSize < bytes) return api::Status::TableTooSmall;
            if (!descriptor.table || descriptor.size != sizeof(api::InterfaceDescriptorV1)) return api::Status::InternalError;
            *output = &descriptor;
            return api::Status::Ok;
        }
        return knownId ? api::Status::UnsupportedMajor : api::Status::UnknownInterface;
    }
}
