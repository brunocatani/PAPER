# Migrating a mod from PAPER 1.2 to PAPER 2.0

PAPER 1.2 published one table, `PaperProviderApiV1`, through the RPS SDK's
`PAPERApi.h` and the exports `PAPERAPI_GetProviderApi` and
`PAPERAPI_GetProviderDescriptorV1`. PAPER 2.0 publishes provider API **2.0**
from this repository's `include/PAPER/`, found through
`PAPERAPI_QueryInterfaceV1`. The old exports still exist only to refuse: each
returns null, and PAPER stops the game at the main menu with an error box
naming the mod that called it. A mod built against `PAPERApi.h` has to be
rebuilt with the changes on this page.

PAPER 2.0 runs on ROCK 1.0. A mod that uses both moves to ROCK's provider API
2 at the same time; see ROCK's `docs/api/Migration.md`. Coding agents can
follow ROCK's `skills/rock-1.0-migration/SKILL.md`, which covers ROCK, PAPER,
RPS UI Framework and RobCo PALM together.

## 1. Build

- Remove the RPS SDK from your build: `find_package(RPS_SDK)`, include paths
  ending in `SDK/PAPER/include`, and any copy of `PAPERApi.h`.
- Add this repository's `include/` directory through a CMake cache variable,
  for example `PAPER_INCLUDE_DIR`, and include `<PAPER/Client.h>`,
  `<PAPER/Provider.h>` and, if you need it, `<PAPER/PhysicalReload.h>`.
  Nothing is linked.
- The headers need C++20 and a 64-bit build.

## 2. Discovery and connection

| PAPER 1.2 | PAPER 2.0 |
| --- | --- |
| `PAPERAPI_GetProviderApi(PAPER_API_VERSION)`, `PAPERAPI_GetProviderDescriptorV1()` | `PAPERAPI_QueryInterfaceV1(id, exactMajor, minimumMinor, minimumTableBytes, descriptor)` (`paper::api::kQueryExportName`) |
| `getVersion()` | Removed. The interface version is in the descriptor; `getModVersion()` returns PAPER's version string |
| `registerConsumerV1`, `unregisterConsumerV1` | `registerConsumer`, `unregisterConsumer`, or `paper::api::Client` |

```cpp
#include <PAPER/Client.h>
#include <PAPER/Provider.h>

auto* module = GetModuleHandleW(L"PAPER.dll");
if (!module) return;                               // PAPER absent: run without it
const auto query = reinterpret_cast<paper::api::QueryInterfaceV1>(
    GetProcAddress(module, paper::api::kQueryExportName));
paper::api::Client paper;
paper.connect(query, "MyMod", capabilities);       // game thread, once PAPER is ready
```

`connect` registers your owner on the Provider interface with the capability
mask you ask for; `acquire(table)` returns any further interface. Request
exactly the versions your headers declare, as `Client` does. Never probe with
trial queries: every refused query stops the game. When `PAPER.dll` is not
loaded, turn your PAPER features off and keep running.

## 3. Names and records

- `V1` is gone from every function, record and enum, and the `Paper` prefix is
  gone from types: `PaperProviderApiV1` is `provider::Api`,
  `PaperResultV1` is `Status`, `PaperConsumerCapabilityV1` is
  `ConsumerCapability`, `PaperEventKindV1` is `EventKind`.
- Every record starts with `size` and `version` (`kRecordVersion`, 1). Calls
  refuse a wrong size or version. Value-initialize records and the defaults are
  right. Layouts changed; never reuse a binary built against `PAPERApi.h`.

## 4. The Provider table

These calls keep their meaning and lose `V1`:

| PAPER 1.2 | PAPER 2.0 |
| --- | --- |
| `getModVersion`, `isReady` | Same |
| `getRuntimeStateV1`, `getConfigStateV1` | `getRuntimeState`, `getConfigState`. `ConfigState` drops the development capture fields |
| `getCapturedTransformCountV1`, `copyCapturedTransformsV1` | `getCapturedTransformCount`, `copyCapturedTransforms` |
| `getNativeHandPoseV1` | `getNativeHandPose` |
| `getGrantedCapabilitiesV1` | `getGrantedCapabilities` |
| `setAnimationAuthorityV1`, `clearAnimationAuthorityV1` | `setAnimationAuthority`, `clearAnimationAuthority` |
| `registerEventCallbackV1`, `unregisterEventCallbackV1` | `registerEventCallback`, `unregisterEventCallback`. `Event` loses its `related*` fields |
| `getNativePoseFrameStateV1`, `getNativeHandSolutionV1` | `getNativePoseFrameState`, `getNativeHandSolution` |

These are removed with no replacement. They served PAPER's development capture
toolkit, which is gone since PAPER reads native animations directly:

- Reload observation: `getReloadObservationLimitsV1`,
  `getReloadCatalogStateV1`, `copyReloadCatalogNodesV1`,
  `copyReloadEvidenceV1`, `copyReloadEvidencePointsV1`,
  `getReloadFrameStateV1`, `copyReloadNodeObservationsV1`.
- Reload animation evidence: `getReloadAnimationLimitsV1`,
  `getReloadAnimationCatalogStateV1`, `getReloadAnimationLiveStateV1`,
  `copyReloadAnimationClipsV1`, `copyReloadAnimationTracksV1`,
  `copyReloadAnimationSamplesV1`, `copyReloadAnimationAnnotationsV1`,
  `copyReloadAnimationTriggersV1`, `copyReloadAnimationSkeletonV1`.
- Reload stages: `getReloadStageStateV1`, `copyReloadStagePartsV1`.
- Weapon motion and manipulation: `getWeaponMotionLimitsV1`,
  `getWeaponMotionCatalogStateV1`, `copyWeaponMotionPartsV1`,
  `copyWeaponMotionStagesV1`, `copyWeaponMotionStageKeysV1`,
  `copyWeaponMotionFollowersV1`, `copyWeaponMotionFollowerKeysV1`,
  `getWeaponMotionLearningStateV1`, `copyWeaponMotionRecordersV1`,
  `getWeaponManipulationFrameStateV1`, `getWeaponManipulationHandStateV1`,
  `getWeaponMotionStoreStateV1`.
- Development capture: `setDevelopmentCaptureV1`, `clearDevelopmentCaptureV1`,
  `getDevelopmentCaptureStateV1`.

## 5. Capabilities and events

`ConsumerCapability` keeps `RuntimeState` (bit 0), `CapturedTransforms` (1),
`NativeHandPose` (2), `AnimationAuthority` (3), `FrameCallbacks` (4) and
`NativePosePipeline` (10). Bits 5 to 9 and 11 to 14
(`ReloadObservations`, `ReloadEvidenceGeometry`, `ReloadAnimationEvidence`,
`ReloadAnimationTelemetry`, `ReloadStageIdentification`,
`WeaponMotionCatalog`, `WeaponMotionDiagnostics`,
`WeaponManipulationTelemetry`, `DevelopmentCaptureControl`) are retired and
never granted. Ask only for the bits you use; `All` covers the surviving ones.

`EventKind` keeps `FrameComplete` (1), `RuntimeReset` (2), `ConfigReloaded` (3)
and `PresentationComplete` (11). Kinds 4 to 10 (weapon motion and
manipulation) are retired.

## 6. New: PhysicalReload

`PhysicalReload` (interface 2) is new to mods coming from PAPER 1.2. It is for
a mod that drives a reload or manual cycle with the player's own hands: a
renewed lease takes the equipped weapon away from PAPER's native reload
animation. See [PhysicalReload](PhysicalReload.md).

## 7. Check the result

1. Build against `include/PAPER/` with no RPS SDK path left. Fix every compile
   error with the tables above.
2. Search your source for leftovers: `PAPERApi.h`, `PAPERAPI_GetProviderApi`,
   `PAPERAPI_GetProviderDescriptorV1`, `PAPER_API_VERSION`, `V1`, and the
   removed calls listed in section 4.
3. Start the game with PAPER 2.0 and ROCK 1.0. No error box appears at the
   main menu, `PAPER.log` names no refusal for your DLL, and your own log shows
   `connect` and every `acquire` returning `Ok`.
