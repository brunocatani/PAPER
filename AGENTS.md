# AGENTS.md — PAPER

PAPER (`PAPER.dll`) owns native reload and manual-cycle animation coordination through ROCK's animation phases. Suite ownership comes from the ROCK suite workspace instructions.

## Identity and paths

- Remote: `origin` at `https://github.com/brunocatani/PAPER.git`.
- Deploys `PAPER.dll` and `PAPER.pdb` only to `F4SE\Plugins` under the mod folder set by `COPY_PLUGIN_BASE_PATH` in `CMakeUserPresets.json`.
- Config: `Documents\My Games\Fallout4VR\Mods_Config\PAPER\PAPER.ini`, with the optional compiled motion cache in `MotionCache\v1\` beside it.
- Log: `Documents\My Games\Fallout4VR\F4SE\PAPER.log` (with rotations).

## Build

Before building, verify that `CMakeUserPresets.json` and the generated cache point at this checkout and at PAPER's own mod folder.

```powershell
cmake --preset custom-fast
cmake --build build-fast --config Release --target PAPER
```

Tests build in `build-fast` through the `build_tests` target. PAPER has no project release preset; do not invent one.

## Architecture

- PAPER consumes ROCK's provider interfaces from ROCK's own `include/`; ROCK owns those versions.
- PAPER publishes its provider API (interfaces `Provider` and `PhysicalReload`, major 2) from `include/PAPER/`, compiled by PAPER itself, with one page per interface in `docs/api/`, standalone-header and discovery tests in `tests/`, and one consumer example (`tests/ConsumerExample.cpp`). `PAPERAPI_QueryInterfaceV1` serves only major 2; the retired pre-2.0 exports only refuse with a logged reason.
- PAPER links `libraries_and_tools/CommonLibF4VR` directly and does not consume F4VR-CommonFramework.
- Keep PAPER behaviour separate from ROCK unless the task explicitly changes a shared contract.
