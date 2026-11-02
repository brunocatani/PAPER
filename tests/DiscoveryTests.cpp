#include "api/InterfaceNegotiation.h"
#include <PAPER/PhysicalReload.h>
#include <array>
#include <cassert>
#include <iostream>

// Discovery negotiation over PAPER's real interface tables: exact major,
// minimum minor and minimum table size; every pre-2.0 major is refused.
int main() {
    using namespace paper;
    using api::Status;
    const api::provider::Api provider{};
    const api::physical_reload::Api reload{};
    const std::array entries{ discovery::describe(provider), discovery::describe(reload) };
    const api::InterfaceDescriptorV1* out{};

    assert(discovery::query(entries,api::InterfaceId::Provider,2,0,sizeof(api::provider::Api),&out)==Status::Ok);
    assert(out==&entries[0]&&out->table==&provider&&out->tableByteSize==sizeof(api::provider::Api));
    assert(discovery::query(entries,api::InterfaceId::PhysicalReload,2,0,sizeof(api::physical_reload::Api),&out)==Status::Ok);
    assert(out->requiredProviderMajor==api::provider::kMajor&&out->requiredProviderMinor==api::provider::kMinor);

    // Only major 2 is served. Pre-2.0 PhysicalReload consumers called the
    // retired per-version exports, never this query.
    for (const std::uint32_t major : {1u,3u}) {
        out=&entries[0];
        assert(discovery::query(entries,api::InterfaceId::PhysicalReload,major,0,8,&out)==Status::UnsupportedMajor&&!out);
    }
    assert(discovery::query(entries,api::InterfaceId::Provider,1,0,8,&out)==Status::UnsupportedMajor&&!out);
    assert(discovery::query(entries,api::InterfaceId::Provider,3,0,8,&out)==Status::UnsupportedMajor&&!out);

    // An older (smaller) table request is served; a newer minor or larger table is not.
    assert(discovery::query(entries,api::InterfaceId::Provider,2,0,8,&out)==Status::Ok&&out);
    assert(discovery::query(entries,api::InterfaceId::Provider,2,1,8,&out)==Status::UnsupportedMinor&&!out);
    assert(discovery::query(entries,api::InterfaceId::Provider,2,0,sizeof(api::provider::Api)+8,&out)==Status::TableTooSmall&&!out);

    assert(discovery::query(entries,static_cast<api::InterfaceId>(99),2,0,8,&out)==Status::UnknownInterface&&!out);
    assert(discovery::query(entries,api::InterfaceId::Provider,0,0,8,&out)==Status::InvalidArgument);
    assert(discovery::query(entries,api::InterfaceId::Provider,2,0,0,&out)==Status::InvalidArgument);
    assert(discovery::query(entries,api::InterfaceId::Provider,2,0,8,nullptr)==Status::InvalidArgument);
    std::cout<<"PASS PAPER discovery: exact major, minimum minor, table size, pre-2.0 refusal\n";
}
