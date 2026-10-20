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

Tests use `custom-tests` / `build-tests`. PAPER has no project release preset; do not invent one.

## Architecture

- PAPER consumes ROCK's provider interfaces; ROCK owns those versions.
- PAPER's own consumer API currently lives in `src/api/` and `SDK/`, mirrored in RPS_SDK.
- PAPER links `libraries_and_tools/CommonLibF4VR` directly and does not consume F4VR-CommonFramework.
- Keep PAPER behaviour separate from PAPER_Toolkit and ROCK unless the task explicitly changes a shared contract.

## Pending API rework

When work on PAPER's API starts, rework it the way ROCK's 1.0 reset did (master `AGENTS.md`, section 15):

- Move the public headers into `include/PAPER/`, self-contained, with `kMajor`/`kMinor` in each interface header and one page per interface in `docs/api/`. Drop every SDK copy and bind other providers from their own `include/` directories.
- Squash every earlier version and minor of each interface into exactly 2.0. Every request for a major-1 version fails closed: discovery returns a logged unsupported-version status and never a table.
- Whatever is not carried into the squashed version is deleted with its code, tests and docs. No code or behaviour is kept to serve an older version: it is either in the new version or dead.
- `PhysicalReload`'s separate V1, V2 and V3 exports become one interface.
- Update `providers/PAPER.toml` in the API release registry (headers, `headers_ready = true`) and run `check.py`.
