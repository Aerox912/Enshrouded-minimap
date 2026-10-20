Shroudweave Minimap 0.5.13

Client-only Minimap maintained by Aerox912, based on elxokker's original.
Credits, license and asset provenance: mods/minimap_mod.

Release changes: north-up by default with optional rotation; fixed death markers
shown in red; minimap hides for controller-opened menus. A GPU rendering path is
introduced in code, with optimization/validation ongoing. Active GPU terrain
rendering or measured performance gains are not established for this payload.
Original Creative Mode Nexus146 v2.1.4 is separate and is not included.
The matching private predecessor passed local single-player normal-startup,
Creative menu/controller, normal colours, north-up tracking and restart checks.
Release artifacts retain exact source, binary and archive integrity checks.
XHL/Free Climbing, ModLoader Proxy, SF Flight, multiplayer, concurrent hot reload,
HDR and measured GPU/FPS performance are not accepted by those observations.

Close game and Manager; back up the existing Minimap folder/configuration.
Extract mods/minimap_mod into the game directory. Keep existing shroudtopia.json,
loaders and original Creative files. This package does not resolve competing
root dinput8.dll owners. Requires the compatible Shroudtopia/EML setup and VC++
x64 runtime. F10 toggles, +/- zoom, F9/F11 resize; retain configured bindings.
North-up, appearance and existing settings are preserved.

Public package version: 0.5.13. Internal diagnostic revision remains
0.5.13-public-creative-compat-r1 to preserve accepted C++ source bytes.
Source tree, compiler and artifact hashes are in the local build receipt.
