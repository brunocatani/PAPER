# PhysicalReload (interface 2, major 2)

Header: `include/PAPER/PhysicalReload.h`. For a mod that drives a reload or
manual cycle with the player's own hands: it takes the equipped weapon away
from PAPER's native reload animation for a short lease that it renews every
frame.

Interface `PhysicalReload`, major 2, minor 0, discovered through
`PAPERAPI_QueryInterfaceV1` (see [Provider](Provider.md)); `Client::acquire`
does it for you. It replaces the former `PAPERAPI_QueryPhysicalReloadV1`,
`V2` and `V3` exports, which now only refuse (see [Provider](Provider.md)). Its semantics
are those of the former V3 early takeover; the V1 and V2 admission rules are
gone. Every call runs on the game thread and takes a Provider owner token.

## Calls

- `acquire(owner, request)` needs the `AnimationAuthority` capability. It
  binds the exact native equipped item (form, equipped data and instance).
  `observedFrame` is the ROCK frame your hands acted on and must be within one
  frame of PAPER's (`StaleSnapshot` otherwise). The lease ends after
  `observedFrame + leaseFrames` (1 to `kMaxLeaseFrames`), when the equipped
  item is replaced, on a world reset or when the owner is revoked. No PAPER
  animation snapshot or weapon-part readiness is required. Returns
  `OwnerConflict` while another owner holds a live lease or any consumer holds
  Provider animation authority, and `NotReady` before PAPER is ready.
  Repeating the call renews your lease.
- `clear(owner)` ends your lease; `OwnerConflict` while another owner's lease
  is live.
- `read(owner, state)` needs `RuntimeState`. `active` 0 means no live lease
  and every other field is zero.

## Flags

`Flag::ManualCycle`: you own bolt cycling, chambering, casing ejection and
their sounds; PAPER suppresses the native automatic cycle for the lease.
Unknown flag bits return `InvalidArgument`.

## Example

`tests/ConsumerExample.cpp` connects, acquires the interface and renews the
lease with the current ROCK frame.
