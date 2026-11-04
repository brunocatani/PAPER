# AGENTS.md - PAPER

Orientation for any agent working on PAPER or building a mod on top of it.

PAPER is a Fallout 4 VR F4SE plugin (`PAPER.dll`) that restores reload and weapon-cycling animations: it plays the weapon's native animation and moves the player's arms, hands and fingers with it while ROCK keeps the weapon following the controller. It runs inside ROCK's animation phases through ROCK's provider API 2.0, publishes its own provider API, and links CommonLibF4VR directly. It targets Fallout 4 VR 1.2.72.0 only.

## Building a mod on PAPER

- PAPER publishes provider API 2.0: 2 interfaces, `Provider` and `PhysicalReload`, whose headers are in `include/PAPER/`. They include only the C++ standard library; nothing links against PAPER.
- Add PAPER's `include/` directory through a CMake cache variable such as `PAPER_INCLUDE_DIR`, include `<PAPER/...>`, resolve `PAPERAPI_QueryInterfaceV1` from the loaded `PAPER.dll`, and connect with `paper::api::Client`. Never include headers from `src/` or copy PAPER's headers into the consumer.
- Read `docs/api/Provider.md` first, then `docs/api/PhysicalReload.md` when the mod drives reloads with the player's hands. `tests/ConsumerExample.cpp` is the minimal consumer.
- When PAPER is absent, the consumer turns its PAPER features off and keeps running. A version mismatch never runs silently: PAPER names a consumer built for another PAPER API in an error box at the main menu and closes the game when the box is closed. Request exactly the versions your headers declare (`paper::api::Client` does); never probe for optional versions with trial queries.

## Repository map

| Path | Contents |
| --- | --- |
| `src/PaperMain.cpp` | F4SE Query/Load, version guard, ROCK connection, frame and session lifecycle |
| `src/animation/` | Native animation authority and the hand pose pipeline |
| `src/reload_control/` | Manual-reload-only control and the physical-reload lease |
| `src/reload_observation/`, `src/reload_stages/`, `src/animation_evidence/` | Reload observation and weapon classification, stage identification, clip telemetry and exact preharvest |
| `src/weapon_motion/` | Weapon-part motion paths, learning and the motion cache |
| `src/attachment_bindings/` | Weapon attachment node remapping |
| `src/compat/` | Merged Root Behavior reserve-reload recovery and the Tactical Reload bridge |
| `src/api/` | Provider API: discovery (`InterfaceNegotiation.h`), the interface tables (`PAPERProvider.cpp`) and the ROCK client |
| `src/debug/` | Diagnostic drawing through RPS UI Framework |
| `src/native/`, `src/support/` | Native offsets, guarded native memory reads, logging and runtime helpers |
| `src/PaperConfig*.cpp` | Compiled defaults and INI loading |
| `include/PAPER/` | Public provider API headers, the only copy |
| `docs/api/` | One page per provider interface |
| `data/config/PAPER_example.ini` | Example INI, a reference only |
| `tests/` | Tests and the consumer example |
| `cmake/` | Version resource and release packaging |

## Build and test

Requirements: Windows x64, Visual Studio 2022 (v143), CMake 4.2 or newer, vcpkg with `VCPKG_ROOT` set, and checkouts of CommonLibF4VR, ROCK (its `include/` headers), RPS UI Framework (its `SDK/include/` headers) and RPS Framework (its `include/` headers). PAPER uses C++23.

Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and set `COMMON_LIB_F4VR_PATH` and `COPY_PLUGIN_BASE_PATH` (the mod folder the build copies `PAPER.dll` and `PAPER.pdb` into, under `F4SE/Plugins`); set `ROCK_INCLUDE_DIR`, `RPSUI_INCLUDE_DIR` and `RPS_FRAMEWORK_INCLUDE_DIR` when the checkouts are not where `CMakeLists.txt` looks by default. Before building, check that the presets and the generated cache point at this checkout and PAPER's own mod folder, not an older copy. Then:

```powershell
cmake --preset custom-fast
cmake --build build-fast --config Release --target PAPER
```

Tests live in `build-fast` but outside the default build. Every test target name contains `Tests`. Build and run them with `cmake --build build-fast --config Release --target build_tests`, then `ctest --test-dir build-fast -C Release --output-on-failure`. `PAPERPublicHeaderTests` compiles every public header on its own plus the consumer example; `PAPERDiscoveryTests` checks discovery and refused majors; `PAPERObservationApiTests` checks the ABI layout of the published records and tables.

`custom-release` builds into `build-release` and writes a release's two files to `build-release/package` (`cmake/package.cmake`): a `.zip` holding only the DLL, which installs as it is, and `README.md` beside it. The zip refuses any other file; the PDB, the license, headers and every other development file stay out of it.

## Runtime files

- Plugin: `Data/F4SE/Plugins/PAPER.dll`, registered with F4SE and with ROCK as `PAPER`. Log: `PAPER.log` in `Documents/My Games/Fallout4VR/F4SE/`.
- Settings: `PAPER.ini` in `Documents/My Games/Fallout4VR/Mods_Config/PAPER/`, with the optional compiled motion cache in `MotionCache/v1/` beside it. Runtime code resolves the Documents known folder.
- PAPER writes no co-save data.

## What PAPER does not own

- **Weapon placement, hands and grips** belong to ROCK. PAPER reads ROCK's presented hands and grip baselines and requests native-animation and hand-visual authority through ROCK's Animation interface; ROCK keeps the weapon following the controller.
- **Ammunition, firing and the reload clock** belong to the game. PAPER coordinates the animation around them.
- **Physical reloads** belong to the consumer holding a PhysicalReload lease: while the lease is live PAPER leaves that weapon's reload, and with `ManualCycle` its bolt cycle, to the consumer.
- **World drawing** belongs to RPS UI Framework. PAPER's diagnostic drawing goes through its world drawing; PAPER has no renderer.

## Invariants

### Animation

- PAPER takes hand authority only for a native reload or cycle action; `WeaponFire` alone is telemetry.
- PAPER stands down while ROCK latches a hand to a fixed surface (climbing), and leaves reloads and cycles native for a weapon ROCK seats from a FRIK user weapon offset.
- A physical-reload lease and Provider animation authority exclude each other: the lease is refused while any consumer holds authority.
- Native-pose and weapon-motion records are observations. They grant no pose, IK or scene-node control.
- Merged Root Behavior compatibility recovers only a missing player reserve reload (`WPNReloadReserve`) through the normal reload (`WPNReload`) of the same loaded subgraph. Native reserve bindings stay, trigger timing and ammo accounting stay native, and no clip or asset is changed. Its hook installs only after the resolver and lookup bytes are verified.

### Provider API

- Public headers include only the C++ standard library and other `include/PAPER/` headers: never CommonLibF4VR, ROCK, engine or F4SE types.
- Every entry point validates the caller's record `size` and `version` before reading it. Records use fixed-width types.
- `PAPERAPI_QueryInterfaceV1` serves only the current majors. The pre-2.0 exports (`PAPERAPI_GetProviderApi`, `PAPERAPI_GetProviderDescriptorV1`, `PAPERAPI_QueryPhysicalReloadV1/V2/V3`) only refuse; their names, like retired interface IDs, slots, enum values and flag bits, are never reused.
- Every version refusal and every call to a pre-2.0 export is logged with the calling DLL and stops the game at the main menu; so does a loaded ROCK that does not serve the ROCK API PAPER was built for (`src/api/RequiredUpdates.cpp`). A missing ROCK leaves PAPER idle and the game running.
- A released interface version is frozen. A compatible addition goes at the end of a table; an incompatible change needs a new major.
- A header change updates its `docs/api/` page in the same commit and passes the public header, discovery and observation API tests.

### Configuration

- `src/PaperConfigDefaults.cpp` holds every option's compiled default. A missing `PAPER.ini` is created from those defaults; loading never rewrites an existing file, and keys it lacks use their defaults. The file reloads while the game runs; cache size limits apply at the next session.
- `data/config/PAPER_example.ini` documents every option. It is a reference only: never read, embedded, packaged, deployed or used to create runtime files. INIs never ship in the mod package.
- Development capture and the motion cache are off by default. `[DevelopmentCapture]` is a hard ceiling: an API request never exceeds it, API demand wakes nothing unless `bAllowApiActivation` is set, and passive telemetry never starts exact harvesting, motion-cache writes or background work. `ReadOnly` cache access never creates, touches, prunes or writes files.
