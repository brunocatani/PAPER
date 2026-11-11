# AGENTS.md - PAPER

Orientation for any agent working on PAPER or building a mod on top of it.

PAPER is a Fallout 4 VR F4SE plugin (`PAPER.dll`) that restores reload and weapon-cycling animations: it plays the weapon's native animation and moves the player's arms, hands and fingers with it while ROCK keeps the weapon following the controller. It runs inside ROCK's animation phases through ROCK's provider API 2.0, publishes its own provider API, and links CommonLibF4VR directly. It targets Fallout 4 VR 1.2.72.0 only.

## Building a mod on PAPER

- PAPER publishes provider API 2.0: 2 interfaces, `Provider` and `PhysicalReload`, whose headers are in `include/PAPER/`. They include only the C++ standard library; nothing links against PAPER.
- Add PAPER's `include/` directory through a CMake cache variable such as `PAPER_INCLUDE_DIR`, include `<PAPER/...>`, resolve `PAPERAPI_QueryInterfaceV1` from the loaded `PAPER.dll`, and connect with `paper::api::Client`. Never include headers from `src/` or copy PAPER's headers into the consumer.
- Read `docs/api/Provider.md` first, then `docs/api/PhysicalReload.md` when the mod drives reloads with the player's hands. `tests/ConsumerExample.cpp` is the minimal consumer.
- Updating a mod built against PAPER 1.2 (`PAPERApi.h`): `docs/api/Migration.md` maps every old call to its replacement. ROCK's `skills/rock-1.0-migration/SKILL.md` covers PAPER, ROCK, RPS UI Framework and RobCo PALM together.
- When PAPER is absent, the consumer turns its PAPER features off and keeps running. A version mismatch never runs silently: PAPER names a consumer built for another PAPER API in an error box at the main menu and closes the game when the box is closed. Request exactly the versions your headers declare (`paper::api::Client` does); never probe for optional versions with trial queries.

## Repository map

| Path | Contents |
| --- | --- |
| `src/PaperMain.cpp` | F4SE Query/Load, version guard, ROCK connection, frame and session lifecycle |
| `src/animation/` | Native animation authority and the hand pose pipeline |
| `src/reload_control/` | Manual-reload-only control, the empty-gun bolt hold, the chamber, the hand-worked bolt and the physical-reload lease |
| `src/animation_evidence/` | The off-screen clip reader (`exact_clip_preharvest::stepPoses`) and the hand-action trace (`ClipTelemetry.cpp`) |
| `src/weapon_classification/` | Weapon family classification from ROCK's classification, the weapon form and part evidence |
| `src/attachment_bindings/` | Weapon attachment node remapping |
| `src/compat/` | Merged Root Behavior reserve-reload recovery and the Tactical Reload bridge |
| `src/api/` | Provider API: discovery (`InterfaceNegotiation.h`), the interface tables (`PAPERProvider.cpp`) and the ROCK client |
| `src/debug/` | Diagnostic drawing through RPS UI Framework |
| `src/native/`, `src/support/` | Native offsets, guarded native memory reads, logging and runtime helpers |
| `src/PaperSettings.cpp`, `src/PaperConfig.cpp` | Settings catalog (tables, keys, help, compiled defaults, old INI keys) on RPS Framework's settings store, and loading with hot reload |
| `include/PAPER/` | Public provider API headers, the only copy |
| `docs/api/` | One page per provider interface, and `Migration.md` for mods built against PAPER 1.2 |
| `data/config/PAPER_example.toml` | Example settings file, a reference only |
| `tests/` | Tests and the consumer example |
| `cmake/` | Version resource and release packaging |

## Build and test

Requirements: Windows x64, Visual Studio 2022 (v143), CMake 4.2 or newer, vcpkg with `VCPKG_ROOT` set, and checkouts of CommonLibF4VR, ROCK and RPS UI Framework (their `include/` headers) and RPS Framework (built with its `custom-fast` preset, which stages the package PAPER links). PAPER uses C++23.

Copy `CMakeUserPresets.json.template` to `CMakeUserPresets.json` and set `COMMON_LIB_F4VR_PATH` and `COPY_PLUGIN_BASE_PATH` (the mod folder the build copies `PAPER.dll` and `PAPER.pdb` into, under `F4SE/Plugins`); set `ROCK_INCLUDE_DIR`, `RPSUI_INCLUDE_DIR` and `RPSFramework_DIR` (the staged package's `lib/cmake/RPSFramework`) when the checkouts are not where `CMakeLists.txt` looks by default. Then:

```powershell
cmake --preset custom-fast
cmake --build build-fast --config Release --target PAPER
```

Tests live in `build-fast` but outside the default build. Every test target name contains `Tests`. Build and run them with `cmake --build build-fast --config Release --target build_tests`, then `ctest --test-dir build-fast -C Release --output-on-failure`. `PAPERPublicHeaderTests` compiles every public header on its own plus the consumer example; `PAPERDiscoveryTests` checks discovery and refused majors; `PAPERObservationApiTests` checks the ABI layout of the published records and tables.

## Runtime files

- Plugin: `Data/F4SE/Plugins/PAPER.dll`, registered with F4SE and with ROCK as `PAPER`. Log: `PAPER.log` in `Documents/My Games/Fallout4VR/F4SE/`.
- Settings: `PAPER.toml` in `Documents/My Games/Fallout4VR/Mods_Config/PAPER/`. Runtime code resolves the Documents known folder.
- Co-save: unique ID `PAPR`, record `BOLT` version 1, written by `src/reload_control/ManualBolt.cpp`: the loaded rounds and chamber of each gun it tracks, by weapon, owner and ExtraUniqueID. A layout change needs a new record version.

## What PAPER does not own

- **Weapon placement, hands and grips** belong to ROCK. PAPER reads ROCK's presented hands and grip baselines and requests native-animation and hand-visual authority through ROCK's Animation interface; ROCK keeps the weapon following the controller.
- **Ammunition, firing and the reload clock** belong to the game. PAPER coordinates the animation around them; only `[BoltAppreciation] ChamberRound` and `BoltAppreciation` set a gun's loaded count, and always within what the inventory holds.
- **Physical reloads** belong to the consumer holding a PhysicalReload lease: while the lease is live PAPER leaves that weapon's reload, and with `ManualCycle` its bolt cycle, to the consumer.
- **World drawing** belongs to RPS UI Framework. PAPER's diagnostic drawing goes through its world drawing; PAPER has no renderer.

## Invariants

### Animation

- PAPER takes hand authority only for a native reload or cycle action; `WeaponFire` alone is telemetry.
- A reload always plays both hands. During a cycle the left hand is driven only when the clip works the weapon with it, meaning it moves against both the gun and the body; otherwise it stays where ROCK puts it. A pump stroke reaches PAPER as a reload start, so it plays as a reload.
- `[Reload] HandBlendSeconds` blends the left hand between ROCK's pose and the animation in both directions; 0 moves it instantly.
- `[BoltAppreciation] BoltHoldOpen` leaves an empty gun's action open on both weapon slots until a reload starts or rounds arrive. PAPER decides and ROCK applies it through WeaponParts drives under a two-frame lease, so the pose ends by itself if PAPER stops. The pose is the weapon's own, read off screen from its clips the first time a form is equipped in the process (`exact_clip_preharvest::stepPoses`). The idle clip gives the closed pose, never the live bones, which sit wherever they were left until the graph runs that weapon's subgraph. The reloads come next: the `WeaponBolt` bone and the `WeaponExtra` bones that keep still beside it while a clip parks it open. With no parked bolt, the pose is the farthest frame of any fire clip, or otherwise of a reload's stroke; other clips are read only when none of those moves the bolt. A dwell is what leaves magazine props out and takes a barrel's tilt or a bolt catch in, so never select parts by name or evidence. Only manual actions are excluded by family, because classification flags are noisy (a `Round` part marks any gun with a visible bullet as loose-round fed); a gun none of whose clips open its bolt simply has no pose. A part's name never makes an automatic gun a manual action. A gun put away empty is drawn with its action open: a form whose parts decided its family before holds on that family from the frame ROCK keys the weapon, until this weapon's own parts arrive and decide again; the hold continues through ROCK's re-key when the colliders arrive; and the first frame that knows both the pose and the rounds sets the hold outright, so the action never opens or closes in the hand because of an equip.
- `[Reload] ManualReloadOnly` hooks the game's automatic reload call sites and asks ROCK to dry-fire an empty second weapon, whose reload ROCK starts itself. The check of the action-dispatcher thunk stops at its jump's opcode, because another plugin may route that jump. In place of the reload an empty trigger pull would have started, both weapons play one `WPNRifleFireDry` click; a charging-reload gun never reaches that path and has no click.
- `[BoltAppreciation] ChamberRound` keeps a chamber and the rounds of every gun that fires cartridges, on both weapon slots (`src/reload_control/ManualBolt.cpp`). A gun qualifies when the ammo on the equipped weapon names a casing model, the game's own test for throwing a case out, and the weapon has the `WeaponTypeBallistic` keyword; both are read from the equipped weapon because weapon mods swap ammo and keywords. Neither signal alone is enough: the keyword is also on launchers and special guns, and some of those have casings. No keyword names a mechanism, so a round stays chambered through a reload unless the family read without part evidence is a revolver, a double-barrel or a minigun; part names are noise for this.
- `[BoltAppreciation] BoltAppreciation` lets a hand work the bolt of a bolt-action rifle on slot 0 (`src/reload_control/ManualBolt.cpp`), for the rifles whose cycle PAPER already animates. A shotgun with the game's bolt-action flag is a pump and is never one of these rifles. The hand stands down while PAPER does not animate the cycle; the rounds go on being counted. Its only data is the bolt hold's clip read, which also samples the arms for this (`src/reload_control/BoltStroke.cpp`): the hand that rides the bolt in the cycle clip is the working hand, and the grab point sits between its thumb and index finger pads. When that is the firing hand, the other hand carries the gun through ROCK's part carry first. The working hand holds that grab point in the clip's wrist and fingers from the moment the weapon is drawn, with no collider needed. The other hand takes the bolt's collider with ROCK's dynamic grip and finger posing once the colliders exist; a grab point keeps its parts from ordinary grabs only for the hands in its mask. The bolt has two resting positions: moving the holding hand's controller past half of `BoltPullDistance` along the bolt's line commits the stroke, which plays at the clip's speed, and letting go never moves it. The sounds are the clip's own `SoundPlay` annotations, played as the bolt passes the point of the stroke they mark in the clip, with the game's generic bolt sounds only for a clip that marks none. The bolt counts as back at three quarters of the stroke and home at one quarter; the trigger waits for it fully home. The pull is read from the controller because the hand is locked to the bolt while it holds it. `BoltHandFromAnimation` off publishes no grab point, so both hands use the dynamic grip. A rifle with no readable pose or no hand on the bolt in its clip keeps its native cycle while its rounds and chamber are still tracked, as does one with no bolt collider when `BoltHandFromAnimation` is off. Loaded rounds are counted in the inventory, so setting the native count creates no ammunition: a round chambered at a reload stays as one extra within the reserve. What the bolt throws out leaves through the game's eject function once ROCK has placed the weapon for the frame (`src/reload_control/ChamberEjection.cpp`), from the weapon's `P-Casing` point on the visible first-person weapon: a spent case is the game's casing built as ordinary world debris, because first-person debris stays out of the world scene for its first half second, and a live round is a real one-round ammo reference that leaves the inventory. ROCK's casing grace covers only the game's casings, so a live round takes ROCK's ejection clearance to get past the weapon and hands. ROCK's chamber marker is a lease PAPER renews, so it disappears with PAPER. These rifles keep a chamber whatever `ChamberRound` says.
- PAPER stands down while ROCK latches a hand to a fixed surface (climbing), and leaves reloads and cycles native for a weapon ROCK seats from a FRIK user weapon offset. Standing down gives up hand authority only: the reload compatibility features under `src/compat/` keep serving those native reloads.
- A physical-reload lease and Provider animation authority exclude each other: the lease is refused while any consumer holds authority.
- Native-pose records are observations. They grant no pose, IK or scene-node control.
- Merged Root Behavior compatibility recovers only a missing player reserve reload (`WPNReloadReserve`) through the normal reload (`WPNReload`) of the same loaded subgraph. Native reserve bindings stay, trigger timing and ammo accounting stay native, and no clip or asset is changed. Its hook installs only after the resolver and lookup bytes are verified.

### Provider API

- Public headers include only the C++ standard library and other `include/PAPER/` headers: never CommonLibF4VR, ROCK, engine or F4SE types.
- Every entry point validates the caller's record `size` and `version` before reading it. Records use fixed-width types.
- `PAPERAPI_QueryInterfaceV1` serves only the current majors. The pre-2.0 exports (`PAPERAPI_GetProviderApi`, `PAPERAPI_GetProviderDescriptorV1`, `PAPERAPI_QueryPhysicalReloadV1/V2/V3`) only refuse.
- Every version refusal and every call to a pre-2.0 export is logged with the calling DLL and stops the game at the main menu; so does a loaded ROCK that does not serve the ROCK API PAPER was built for (`src/api/RequiredUpdates.cpp`). A missing ROCK leaves PAPER idle and the game running.

### Configuration

- `src/PaperSettings.cpp` declares every option once (table, key, help, compiled default from `PaperConfig`) for RPS Framework's settings store (`RPS::Settings`). A missing `PAPER.toml` is created from those defaults; each start organizes the file (every option once under its table with its help as `##` lines, player notes on single-`#` lines kept, unread entries moved to a commented block) and writes it only when that text differs. The file reloads while the game runs.
- At startup only, when `PAPER.toml` is missing, `PAPER.ini` of earlier versions is migrated once through the old-key table in `src/PaperSettings.cpp` (values equal to their old default are left out) and deleted after `PAPER.toml` reads back; a leftover `PAPER.ini` beside an existing `PAPER.toml` is deleted unread. Never rename a key or move it to another table without an old-key row.
- `data/config/PAPER_example.toml` is what PAPER writes for an all-default file. Regenerate it with `PAPERSettingsTests --write-examples data/config` after any catalog change; the test fails when it differs. PAPER never reads it.
- The bolt hold's clip read runs only while `BoltHoldOpen` or `BoltAppreciation` is on. It keeps the poses it needs and stores no tracks, clips or files.
