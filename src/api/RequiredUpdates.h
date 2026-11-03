#pragma once

// Version mismatches stop the game at the main menu with an error box naming
// the mod to update (RPS/Runtime/RequiredUpdate.h), in both directions: a mod
// built for another PAPER API asking PAPER for an interface, and a ROCK that
// does not serve the API PAPER was built for. A missing ROCK is not a
// mismatch: PAPER then stays idle and the game keeps running.

#include <PAPER/Abi.h>
#include <ROCK/Abi.h>

#include <cstdint>

namespace paper::required_updates
{
    // Any thread. Every PAPERAPI_QueryInterfaceV1 refusal; callerAddress is
    // the export's return address, inside the calling mod.
    void noteDiscoveryRefusal(api::Status status, api::InterfaceId id, std::uint32_t requestedMajor,
        const void* callerAddress) noexcept;

    // Any thread. A call to a retired pre-2.0 PAPER export.
    void noteRetiredExport(const char* exportName, const void* callerAddress) noexcept;

    // Game thread. ROCK.dll is loaded but does not export ROCK's discovery
    // function, or refused PAPER's request with status.
    void noteRockMissingDiscovery() noexcept;
    void noteRockRefusal(rock::api::Status status, const char* interfaceName) noexcept;

    // Once, from the F4SE GameLoaded message.
    void watch() noexcept;
}
