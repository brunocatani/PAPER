# Changelog

## v2.0.0 (unreleased)

### Requirements

- PAPER now runs on ROCK 1.0 and binds ROCK's provider API 2.0 (Core, Hands, Grab, Weapon, WeaponParts and Animation). ROCK 0.92 and earlier are no longer supported; with them PAPER logs why and animates nothing.
- Diagnostic drawing goes through RPS UI Framework, which is optional and needed only for that drawing.

### Changed

- Weapons ROCK seats from a FRIK user weapon offset keep the game's own reload and cycling.
- PAPER takes the hands only for a native reload or cycle action; firing alone no longer does.
- Two copies of the same weapon equipped in both hands are now recognized as dual wielding.

### Added

- Mods that drive a reload or manual cycle with the player's own hands can take the weapon away from PAPER's animation through a renewed lease, including taking over early and owning the bolt cycle, chambering, casing ejection and their sounds. PAPER keeps that ownership after firing.
- Release packaging: the `custom-release` preset builds a `.7z` with `PAPER.dll`, its PDB and the license.

### Fixed

- Missing native node remaps of weapon attachments are repaired.
- Guarded native memory reads no longer query page protection first, which lowers their per-frame cost.
- The `RELOAD-PERF` log line no longer repeats every second while game frames take longer than 20 ms.
- Log messages and INI comments describe the current ROCK connection, RPS UI drawing and capture activation.

### For mod authors

- PAPER's provider API is now 2.0, published in this repository's `include/PAPER/` with one page per interface in `docs/api/`: `Provider` (49 calls) and `PhysicalReload` (3 calls), discovered through `PAPERAPI_QueryInterfaceV1`. `paper::api::Client` registers an owner and acquires each interface.
- Mods built against the pre-2.0 API (`PAPERApi.h`, the RPS SDK `RPS::PAPER` target, `PAPERAPI_QueryPhysicalReloadV1/V2/V3`) are refused with a logged reason and must be rebuilt against the new headers.
- Clip telemetry records bounded animation mapping evidence for development diagnostics.
