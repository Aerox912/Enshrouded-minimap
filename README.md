# Shroudweave Minimap 0.5.13

An in-game minimap maintained by Aerox912, based on [elxokker's original Minimap](https://github.com/elxokker/Enshrouded-minimap). Credits, licenses and asset provenance remain included. Technical ID: `minimap_mod`.

This release adds normal-startup compatibility with unchanged [original Creative Mode, Nexus 146, version 2.1.4](https://www.nexusmods.com/enshrouded/mods/146). Creative is a separate download and is not included.

## Release changes

- North-up by default, with optional rotation.
- Fixed death markers, now shown in red.
- Minimap hides while controller-opened menus are visible.
- GPU rendering path introduced in the code; optimization and runtime validation are ongoing. Active GPU terrain rendering and measured performance gains are not established for this accepted runtime payload.
## Compatibility

The matching private candidate passed local single-player testing with original Creative 2.1.4: world loading, normal map colours, Creative menu/controller operation, map recovery after closing menus, north-up tracking, and restart. Its DLL SHA-256 is `c1d733819d863e788133bff6259af710d8459498cfc3dc2d6ceed419df423e40`. The local release build retains exact source, DLL and archive hashes; these observations do not accept different gameplay behavior automatically.

That test used Creative's original `dinput8.dll` with EML through `dbghelp.dll`. XHL plugins, Free Climbing, ModLoader Proxy, SF Flight, multiplayer, concurrent hot reload, HDR and measured GPU/FPS performance were not accepted by this test. An unknown or changed Creative hook profile hides the minimap instead of using an unverified hook.

North-up remains the default. Existing appearance, positions and configuration are preserved. F10 toggles the map, + and - control zoom, and F9/F11 adjust widget size. Existing configured bindings take precedence.

## Installation

Requires Windows x64 Enshrouded, the compatible Shroudtopia/EML loader setup and the Microsoft Visual C++ x64 runtime. This is a client mod; it needs no server component.

1. Close Enshrouded and the Mod Manager, and retain a backup of the current Minimap folder and configuration.
2. Obtain the versioned `Shroudweave-Minimap-0.5.13.zip` from this repository's release and verify its published SHA-256.
3. Extract its `mods/minimap_mod` folder into the game directory. Keep existing `shroudtopia.json` settings; do not replace loaders or Creative files from unrelated packages.
4. Start the game normally. When using original Creative, keep its separately installed, unchanged 2.1.4 files and the accepted loader arrangement. This archive does not resolve competing root `dinput8.dll` owners.

The internal diagnostic revision remains `0.5.13-public-creative-compat-r1` to preserve the reviewed C++ source. Public package version is 0.5.13. The release build receipt identifies the exact source tree, compiler and artifact hashes. Nick waived GitHub Actions provenance for this release; the current source is still recorded in this GitHub repository.

See [release notes](docs/releases/v0.5.13.md), `CREDITS.md`, `LICENSE` and packaged asset provenance. This repository is an independent community fork.
