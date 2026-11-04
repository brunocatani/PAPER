# Provider (interface 1, major 2)

Header: `include/PAPER/Provider.h`. PAPER's main interface: consumer
registration, runtime and configuration state, captured native transforms
and hand poses, native animation authority, frame events, reload observation
and animation evidence, reload stages, the native pose pipeline, weapon
motion and development capture. Records are value copies; no engine pointer
crosses the boundary.

Interface `Provider`, major 2, minor 0. Every other PAPER interface takes the
owner token this interface issues.

## Discovery and connection

Add PAPER's `include/` directory to your build (for example through a
`PAPER_INCLUDE_DIR` CMake cache variable) and include `<PAPER/...>`. Never
copy the headers or link PAPER.

```cpp
#include <PAPER/Client.h>
#include <PAPER/PhysicalReload.h>

const auto query = reinterpret_cast<paper::api::QueryInterfaceV1>(
    GetProcAddress(GetModuleHandleW(L"PAPER.dll"), paper::api::kQueryExportName));
paper::api::Client paper;
paper.connect(query, "MyMod", capabilities);   // game thread, once PAPER is ready
const paper::api::physical_reload::Api* reload{};
paper.acquire(reload);                          // any further interface
```

`PAPERAPI_QueryInterfaceV1(id, exactMajor, minimumMinor, minimumTableBytes,
descriptor)` runs on any thread and returns a descriptor whose table stays
valid for the process lifetime. It serves only major 2: any other major
returns `UnsupportedMajor` with no table. `UnsupportedMinor` and
`TableTooSmall` mean the installed PAPER is older than your headers. `Client`
(header-only, not part of the DLL ABI) wraps the query, registration and
`unregisterConsumer`.

The pre-2.0 exports `PAPERAPI_GetProviderApi`,
`PAPERAPI_GetProviderDescriptorV1` and `PAPERAPI_QueryPhysicalReloadV1/V2/V3`
still exist only to refuse outdated consumers: each returns null. A mod built
against them must be rebuilt against these headers.

A version mismatch stops the game. After any version refusal or a call to a
pre-2.0 export, PAPER logs the calling DLL and, when the main menu opens (or
at once, if it is already open), shows an error box naming that DLL and what
to update; the game closes when the box is closed. Request exactly the
versions your headers declare, as `Client` does; never probe for versions with
trial queries. When `PAPER.dll` is not loaded, turn your PAPER features off and
keep running.

Every record starts with `size` and `version` (`kRecordVersion`, 1); calls
reject a wrong size or version. All calls return `Status` unless noted.

## Registration and capabilities

- `registerConsumer(registration, handle)`: game thread, after `isReady`.
  One owner per non-empty `modName` (`OwnerConflict` otherwise), at most
  `kMaxConsumers`. `requestedCapabilities` is a `ConsumerCapability` mask;
  PAPER grants the bits it knows and returns them in the handle.
- `unregisterConsumer(owner)`: game thread. Ends the owner's authority
  request, development-capture lease, event callbacks and physical-reload
  lease.
- `getGrantedCapabilities(owner)` returns the granted mask (0 for an unknown
  owner).

Each group below names the capability it needs. A call without it returns
`PermissionDenied` (or `false`/0 for the non-`Status` getters).

## Runtime state (game thread)

- `getModVersion()` (any thread): PAPER's version string, for example
  `"1.2.0"`.
- `isReady()` (any thread).
- `getRuntimeState(owner, state)` and `getConfigState(owner, state)`
  (`RuntimeState`, return `bool`).
- `getCapturedTransformCount(owner)` and `copyCapturedTransforms(owner,
  output, max)` (`CapturedTransforms`): the native arm, hand and weapon
  transforms PAPER captured this frame, at most `kMaxCapturedTransforms`.
- `getNativeHandPose(owner, hand, pose)` (`NativeHandPose`, returns `bool`):
  the native hand in weapon space with finger locals.

## Native animation authority (`AnimationAuthority`)

`setAnimationAuthority(owner, request)` asks PAPER to apply native reload
animation to arms, hands and/or weapon (`NativeAnimationAuthorityFlag`).
`leaseFrames` 0 lasts until cleared; 1 to `kMaxAuthorityLeaseFrames` expire
after that many PAPER frames. `clearAnimationAuthority(owner)` ends it.

## Events (`FrameCallbacks`)

`registerEventCallback(owner, callback, userData, token)` (at most
`kMaxCallbacks`) and `unregisterEventCallback(owner, token)`. Callbacks run on
the game thread inside PAPER's frame; the `Event` is borrowed for the call.
`FrameComplete` is the control and lease boundary; `PresentationComplete`
reports the final native-pose readback and does not advance leases. Motion
events (`WeaponManipulation*`, `WeaponMotion*`) fill the `related*` fields.

## Reload observation (`ReloadObservations`; points need `ReloadEvidenceGeometry`)

Descriptive source data for the equipped weapon: PAPER infers no motion,
attachment or stage from it.

- `getReloadObservationLimits`, `getReloadCatalogState`,
  `copyReloadCatalogNodes(owner, catalogSequence, first, output, max,
  copied)`, `copyReloadEvidence(...)`, `copyReloadEvidencePoints(owner,
  catalogSequence, evidenceId, first, output, max, copied)`.
- `getReloadFrameState` and `copyReloadNodeObservations(owner,
  snapshotSequence, ...)`: per-frame native-graph and post-ROCK transforms.

Copy calls take the sequence from the matching state record and return
`StaleSnapshot` once it is superseded.

## Reload animation evidence (`ReloadAnimationEvidence` or `ReloadAnimationTelemetry`)

`getReloadAnimationLimits`, `getReloadAnimationCatalogState`,
`getReloadAnimationLiveState`, and the copy calls for clips, tracks, samples,
annotations, triggers and the skeleton. `ReloadAnimationTelemetry` covers
passive graph bindings, live activity, tracks and raw markers and never starts
the exact off-screen clip preharvest; `ReloadAnimationEvidence` adds it.

## Reload stages (`ReloadStageIdentification`)

`getReloadStageState` and `copyReloadStageParts`: PAPER's stage
identification (rest, fire, magazine in/out, slide and bolt travel) with the
per-part evidence behind it.

## Native pose pipeline (`NativePosePipeline`)

`getNativePoseFrameState` and `getNativeHandSolution(owner, hand, output)`:
PAPER's native-animation decision and ROCK's final presented pose for the
same completed frame. Observational only; they grant no pose or IK authority.

## Weapon motion

- `WeaponMotionCatalog`: `getWeaponMotionLimits`,
  `getWeaponMotionCatalogState`, `copyWeaponMotionParts`, `copyWeaponMotionStages`,
  `copyWeaponMotionStageKeys`, `copyWeaponMotionFollowers`,
  `copyWeaponMotionFollowerKeys`. Exact stages come from authored animation;
  learned stages only from native-graph observations.
- `WeaponMotionDiagnostics`: `getWeaponMotionLearningState`,
  `copyWeaponMotionRecorders`, `getWeaponMotionStoreState`.
- `WeaponManipulationTelemetry`: `getWeaponManipulationFrameState` and
  `getWeaponManipulationHandState(owner, hand, output)`: progress of a hand
  moving a mapped weapon part along its stage.

## Development capture (`DevelopmentCaptureControl`)

`setDevelopmentCapture(owner, request)` requests capture scopes
(`DevelopmentCaptureScope`) for 1 to `kMaxDevelopmentCaptureLeaseFrames`
frames; renew it while capture is wanted. PAPER grants only the scopes its
INI `DevelopmentCapture` settings allow and reports the rest as denied.
`clearDevelopmentCapture(owner)` and `getDevelopmentCaptureState(owner,
state)`. Off by default.
