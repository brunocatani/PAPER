# Changelog

## 2.0.0

### Requirements

- PAPER now runs on ROCK 1.0.0 and binds ROCK's provider API 2.0 (Core, Hands, Grab, Weapon, WeaponParts and Animation). ROCK 0.92 and earlier are no longer supported; with them PAPER logs why and animates nothing.
- Diagnostic drawing goes through RPS UI Framework, which is optional and needed only for that drawing.

### Changed

- Settings move to `PAPER.toml`, with each option's help above it, and are kept organized and up to date at every start. The first start carries every option you changed in `PAPER.ini` into `PAPER.toml` and deletes the INI; nothing to do by hand. Keys lose their type letters (`bChamberRound` is now `ChamberRound`).
- PAPER has no on/off switch: installed, it runs. The hand-worked bolt has its own section, Bolt Appreciation (`[BoltAppreciation]`); the slide held open on empty and the round in the chamber are with the reload options. Each option has a title and a plain explanation.
- Weapons ROCK seats from a FRIK user weapon offset keep the game's own reload and cycling.
- PAPER takes the hands only for a native reload or cycle action; firing alone no longer does.
- Two copies of the same weapon equipped in both hands are now recognized as dual wielding.
- During a cycle the left hand follows the animation only when the clip works the weapon with it; otherwise it stays where ROCK puts it. A reload always plays both hands.
- `[Reload] ManualReloadOnly` and `ChamberRound` are on by default. An upgraded `PAPER.ini` that left them off, their old default, takes the new default.

### Added

- An empty gun's bolt or slide stays open, in either hand, until a reload starts or rounds arrive, with parts that move with it such as a tilting barrel (`[Reload] SlideOpenOnEmpty`, `SlideCloseSeconds`), with `ManualReloadOnly`. PAPER reads the open position off screen from the gun's own animations the first time it is equipped each session. Bolt, lever, pump and double-barrel guns and revolvers never hold.
- `[Reload] HandBlendSeconds` blends the left hand onto a reload, pump stroke or cycle animation and back; 0 moves it instantly.
- `[Reload] ChamberRound` keeps a round in the chamber of every gun that fires cartridges, in either hand: it stays through a reload on top of the full magazine, except on revolvers, double-barrels and miniguns, shows on the ammo counter, and each gun keeps its rounds when unequipped and in saves.
- `[BoltAppreciation] BoltAppreciation` lets you work the bolt of a bolt-action rifle by hand after each shot, and leave it open, with `BoltPullDistance`. The hand the rifle's cycle animation uses takes the bolt from the moment the rifle is drawn. The bolt plays the rifle's own clip sounds as it moves, throws spent cases out of the visible ejection port as world debris, and throws a live round you cycle out as a real round you can pick up.
- With `ManualReloadOnly`, each trigger pull on an empty gun plays one dry-fire click instead of starting a reload.
- Mods that drive a reload or manual cycle with the player's own hands can take the weapon away from PAPER's animation through a renewed lease, including taking over early and owning the bolt cycle, chambering, casing ejection and their sounds. PAPER keeps that ownership after firing.

### Removed

- The development capture and weapon motion cache settings (`[DevelopmentCapture]`, `[WeaponMotionCache]`) and the work behind them. They were off by default; `PAPER.toml` keeps old values as comments that never apply.

### Fixed

- A gun put away empty is drawn with its bolt or slide already open. It used to show closed for about a third of a second after every equip and then open again.
- Missing native node remaps of weapon attachments are repaired.
- PAPER's hooks chain behind another plugin's patch of the same game code instead of refusing to install.
- The game closes normally on exit; PAPER no longer raises R6025 during shutdown.
- `ManualReloadOnly` keeps working when another plugin routes the game's action-dispatcher jump.
- A pump shotgun that shares a double-barrel's fire sound keyword is no longer taken for a double-barrel, and two revolvers with unusual keywords are now recognized as revolvers.
- Guarded native memory reads no longer query page protection first, which lowers their per-frame cost.
- The `RELOAD-PERF` log line no longer repeats every second while game frames take longer than 20 ms.
- Log messages and settings help describe the current ROCK connection and RPS UI drawing.

### For mod authors

- PAPER's provider API is now 2.0, published in this repository's `include/PAPER/` with one page per interface in `docs/api/`: `Provider` (16 calls) and `PhysicalReload` (3 calls), discovered through `PAPERAPI_QueryInterfaceV1`. `paper::api::Client` registers an owner and acquires each interface.
- Mods built against the pre-2.0 API (`PAPERApi.h`, `PAPERAPI_QueryPhysicalReloadV1/V2/V3`) are refused with a logged reason and must be rebuilt against the new headers.
