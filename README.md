# PAPER

PAPER brings weapon reload and cycling animations to Fallout 4 VR. It works with ROCK to animate your arms, hands, and fingers around the weapon, with the default mode keeping the weapon under controller control.

Part of the RPS Suite, PAPER supports animated reloads, bolt and lever actions, and revolver hammer cocking where supported by the weapon's animations. Custom animations for vanilla weapons are recommended; results depend on the animation's compatibility with VR.

## Requirements and installation

- Fallout 4 VR and F4SEVR.
- ROCK and its requirements, including FRIK v78.2 or higher.

Install through your mod manager and launch the game through F4SEVR. The plugin belongs at `Data/F4SE/Plugins/PAPER.dll`.

## Settings

The active configuration is in your Documents folder:

```text
My Games/Fallout4VR/Mods_Config/PAPER/PAPER.ini
```

PAPER creates the file if it is missing and preserves an existing file. Settings hot reload automatically and can also be edited through RobCo PALM.

See the [configuration reference](data/config/PAPER.ini) for available settings. Development capture and motion caching are off by default. Developer cache size limits take effect at the next game session.

## For developers

PAPER exposes reload stages, animation observations, and weapon motion through the [RPS SDK](https://github.com/brunocatani/RPS_SDK), which includes integration documentation and examples.

<details>
<summary>Building from source</summary>

Requires Windows x64, Visual Studio 2022 with the v143 C++ toolset, CMake 4.2 or newer, vcpkg, and CommonLibF4VR. The project uses C++23.

Copy [CMakeUserPresets.json.template](CMakeUserPresets.json.template) to `CMakeUserPresets.json` and set your dependency paths and deployment destination. The `custom-fast` preset copies the plugin to the mod folder you configure.

```powershell
cmake --preset custom-fast -B build-fast
cmake --build build-fast --config Release --target PAPER -- /m:1 /p:CL_MPCount=2

cmake --preset custom-tests -B build-tests
cmake --build build-tests --config Release --target PAPERPolicyTests PAPERObservationApiTests PAPERConfigFileTests PAPERWeaponMotionCacheTests -- /m:1 /p:CL_MPCount=2
ctest --test-dir build-tests -C Release --output-on-failure -j 4
```

</details>

## Source and license

[Source code](https://github.com/brunocatani/PAPER) · [GNU GPL v3 only (GPL-3.0-only)](LICENSE).

Built with the assistance of AI.
