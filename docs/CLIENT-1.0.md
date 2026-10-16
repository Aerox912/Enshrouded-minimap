# ESM Minimap 0.5.8: Enshrouded 1.0

This compatibility release targets Steam client build **25966634**, executable SHA-256
`4d6d43ea4a91e71c82d60b045dde932179c99e32e40657efe374688b2933589a`.
It requires the existing Shroudtopia native loader and proxy. EML alone cannot load this DLL.
The technical mod ID remains `minimap_mod`. Upstream credits and the MIT license are unchanged.

The source starts at `f93ab498bc6200cbb9ea6a57b29ff824c70e6535` and includes the frozen
Bright-map source delta whose prior DLL hash was
`27c9460312d2a225185bd1d4274f42cfcb37f85c0292f9b3dfc18809c21b63b0`.
This is a legacy Shroudtopia compatibility update, separate from the ESM-native port.

## Changes

- Select the current ABI by executable SHA-256, then independently verify named systems,
  complete instructions and feature-specific layouts before installing hooks.
- Update local camera, remote players, waypoint/ping, marker visibility, world/death marker,
  fog, weather, clock and Vulkan contracts for this client.
- Read pings from the native segmented arena, with its verified block capacity, count limit,
  cycle detection and guarded reads. Failed reads publish no partial markers.
- Install Vulkan callbacks only from a presenting context whose table/device pair and resolved
  functions agree. One pair owns the originals; additional devices and foreign slots are refused.
- Keep load readiness independent of Shroudtopia's host-owned `loaded` flag. Discard partial
  optional hook pairs, unwind failed required activation, and drain in-flight present callbacks
  before renderer teardown. Inactive present calls forward once without polling controls.
- Pin callback code for the process lifetime and retain original Vulkan functions even after
  logical unload or failed slot restoration. Already-selected callbacks can still forward.
  A process restart is required to replace this DLL or select another Vulkan device.
- Preserve Bright and explicit Original map appearance. No map texture or world extent was
  changed in this compatibility work.

## Verification and limits

The test programs compile the production readers and guards against a private mapped PE,
mocked Vulkan functions and synthetic records. They never execute game instructions.
Build all five test projects under `tests` with MSBuild, Release/x64/v143/C++17, and run them.
Pass the exact client executable to `client_1_0_test.exe`, with an older executable as optional
second argument for profile rejection. Build `minimap_mod.vcxproj` with the same toolset.
See the frozen candidate's verification logs for exact counts and hashes.

Offline tests do not establish graphics output, game stability, save compatibility, all 1.0
regions or multiplayer behavior. Testing must check movement/rotation, zoom/size/toggle,
clock/weather/fog, pins/pings/players/death markers, Bright/Original appearance and a
deactivate/reactivate cycle. Test newly added world regions explicitly; inherited static
map coverage is not evidence that those regions are covered. No dedicated-server DLL is supplied.
Close the game before installation or rollback; hot DLL unloading is outside this candidate's
acceptance scope.

The unknown-build path installs no hooks. The previously named legacy profile remains in
source, but this candidate's fresh acceptance evidence is for the pinned current client only.

## Observed local gameplay, 16 October 2026

The exact ce16e8a2 DLL passed user checks for local world rendering, movement,
camera turns, world reload, tracking, opening/closing the world map and F10 hide/show.
The supplied screenshot shows the colored map. This is bounded local 1.0 testing,
not acceptance of every setting, multiplayer, new regions or other display setups.
