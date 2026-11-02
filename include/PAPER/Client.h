#pragma once
#include "Discovery.h"
#include "Provider.h"
#include <cstring>

namespace paper::api
{
    // Consumer-side helper only; it is not part of the DLL ABI. Pass the
    // kQueryExportName export resolved from PAPER.dll.
    class Client
    {
        QueryInterfaceV1 _query{};
        const provider::Api* _provider{};
        OwnerToken _owner{};
        std::uint32_t _granted{};

    public:
        Client() = default;
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        // Game thread, once PAPER is ready (NotReady before). capabilities is a
        // ConsumerCapability mask; PAPER grants only the bits it knows. One
        // owner per mod name; a name already registered gets OwnerConflict.
        Status connect(QueryInterfaceV1 query, const char* name, std::uint32_t capabilities) noexcept
        {
            if (_owner) return Status::OwnerConflict;
            if (!query || !name) return Status::InvalidArgument;
            ConsumerRegistration registration{};
            std::size_t length = 0;
            while (length < sizeof(registration.modName) && name[length]) ++length;
            if (!length || length == sizeof(registration.modName)) return Status::InvalidArgument;
            std::memcpy(registration.modName, name, length);
            registration.requestedCapabilities = capabilities;
            const InterfaceDescriptorV1* descriptor{};
            auto status = query(provider::kInterfaceId, provider::kMajor, provider::kMinor, sizeof(provider::Api), &descriptor);
            if (status != Status::Ok) return status;
            if (!descriptor || descriptor->size != sizeof(InterfaceDescriptorV1) ||
                descriptor->interfaceId != provider::kInterfaceId || descriptor->major != provider::kMajor ||
                descriptor->tableByteSize < sizeof(provider::Api) || !descriptor->table) return Status::InvalidSize;
            const auto* api = static_cast<const provider::Api*>(descriptor->table);
            ConsumerHandle handle{};
            status = api->registerConsumer(&registration, &handle);
            if (status != Status::Ok) return status;
            _query = query;
            _provider = api;
            _owner = handle.ownerToken;
            _granted = handle.grantedCapabilities;
            return Status::Ok;
        }

        // Any further interface, for example physical_reload::Api. Its calls
        // take owner().
        template <class Table>
        Status acquire(const Table*& output) const noexcept
        {
            output = nullptr;
            if (!_owner || !_query) return Status::UnknownOwner;
            const InterfaceDescriptorV1* descriptor{};
            const auto status = _query(Table::interfaceId, Table::majorVersion, Table::minorVersion, sizeof(Table), &descriptor);
            if (status != Status::Ok) return status;
            if (!descriptor || descriptor->size != sizeof(InterfaceDescriptorV1) ||
                descriptor->interfaceId != Table::interfaceId || descriptor->major != Table::majorVersion ||
                descriptor->minor < Table::minorVersion || descriptor->tableByteSize < sizeof(Table) ||
                descriptor->requiredProviderMajor != provider::kMajor ||
                descriptor->requiredProviderMinor > provider::kMinor || !descriptor->table) return Status::InvalidSize;
            output = static_cast<const Table*>(descriptor->table);
            return Status::Ok;
        }

        // Game thread. Ends every lease, authority request and callback the
        // owner holds.
        Status close() noexcept
        {
            if (!_owner) return Status::Ok;
            const auto status = _provider->unregisterConsumer(_owner);
            if (status == Status::Ok || status == Status::UnknownOwner) {
                _owner = 0;
                _granted = 0;
                _provider = nullptr;
                _query = nullptr;
            }
            return status;
        }

        [[nodiscard]] OwnerToken owner() const noexcept { return _owner; }
        [[nodiscard]] std::uint32_t grantedCapabilities() const noexcept { return _granted; }
        [[nodiscard]] const provider::Api* providerApi() const noexcept { return _provider; }
    };
}
