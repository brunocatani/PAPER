# ROCK Reanimate

ROCK Reanimate is a Fallout 4 VR F4SE addon that coordinates Bethesda's native weapon animation with ROCK's controller-driven VR pose. Its current focus is restoring native reload and manual-cycle arm, hand, finger, and weapon motion without creating a second, competing animation pipeline.

The project is pre-release (`0.1.0`). The native-animation controls still use `TestEnabled` names because the feature surface has not reached a stable release contract.

## What it does

- Captures selected output from the native first-person animation graph at ROCK's coordinated animation phases.
- Applies full or partial reload authority around ROCK's own pose update.
- Preserves controller ownership of the visible weapon in partial-authority mode while applying substantial native hand and IK motion.
- Handles eligible manual-cycle weapon animations, including explicitly classified revolver behavior.
- Publishes runtime state, captured transforms, native hand poses, events, and leased animation authority through the public Reanimate V1 API.
- Can publish opt-in native/ROCK/applied-pose diagnostics through ROCK's stereo debug renderer.

## Dependencies

### Runtime

| Dependency | Requirement |
| --- | --- |
| Fallout 4 VR | Runtime `1.2.72` or newer. Flat Fallout 4 is rejected. |
| F4SEVR | A loader build compatible with the installed Fallout 4 VR runtime. |
| ROCK | A current ROCK build exposing the complete V1 animation-phase, equipped-grip, hand-visual-authority, native-animation-authority/runtime, presented-hand-frame, and debug-overlay surfaces. |

Reanimate does not load hFRIK directly; skeleton readiness and final presented-hand data arrive through ROCK. It is also independent of PAPER Redux. If ROCK is unavailable or lacks a required capability, Reanimate remains fail-closed and retries when a new game session becomes ready.

### Build

- Windows x64, Visual Studio 2022 with the v143 C++ toolset, and C++23 support.
- CMake 4.2 or newer.
- [vcpkg](https://github.com/microsoft/vcpkg) using the baseline pinned in `vcpkg.json`.
- [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR).
- PowerShell 7 (`pwsh`) for the source-boundary test.

The ROCK V1 consumer header required by this addon is pinned in `src/api/ROCKProviderApi.h`; building Reanimate does not require a separate ROCK source checkout.

## Installation and configuration

Install `ROCK_Reanimate.dll` under `Data/F4SE/Plugins`. The active configuration path is:

```text
Documents/My Games/Fallout4VR/ROCK_Reanimate_Config/ROCK_Reanimate.ini
```

Copy `data/config/ROCK_Reanimate.ini` there to customize the defaults. The file controls the master switch, full/partial native reload authority, log level, and optional debug visualization. Configuration is reloaded on game/session lifecycle events.

## Building and testing

Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json`, replace the placeholder paths, and keep the resulting local preset untracked.

```powershell
cmake --preset custom-fast
cmake --build build-fast --config Release --target ROCK_Reanimate -- /m:1 /p:CL_MPCount=2

cmake --preset custom-tests
cmake --build build-tests --config Release --target ROCKReanimatePolicyTests -- /m:1 /p:CL_MPCount=2
ctest --test-dir build-tests -C Release --output-on-failure -j 4
```

The plugin output is `build-fast/Release/ROCK_Reanimate.dll`.

## Consumer SDK

`SDK/ROCK_Reanimate/include/ROCKReanimateApi.h` is the public, versioned V1 consumer header. Consumers load the exported provider table, register with only the capabilities they need, and must obey the header's game-thread and callback-lifetime rules.
