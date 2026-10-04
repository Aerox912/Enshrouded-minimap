# Enshrouded Minimap - Community Edition

Community-maintained fork of **elxokker's Enshrouded Minimap**, using Shroudtopia.
Version **0.5.0** includes the camera/graphics fixes from upstream
[PR #5](https://github.com/elxokker/Enshrouded-minimap/pull/5) and the player,
ping, waypoint and world-map additions from
[PR #6](https://github.com/elxokker/Enshrouded-minimap/pull/6).

Original creator: [elxokker (xoker)](https://github.com/elxokker/Enshrouded-minimap).
Community maintenance: [Aerox912](https://github.com/Aerox912/Enshrouded-minimap).
This is an independent fork, not an official upstream release. See
[credits and license](CREDITS.md) and the
[original Nexus mod](https://www.nexusmods.com/enshrouded/mods/97).

This is not an external Windows overlay. The mod hooks into the game client and
draws the minimap directly inside the Enshrouded frame.

## What It Does

- Shows a circular minimap on the right side of the screen.
- Defaults to the bottom-right corner and supports top-right, middle-right,
  and bottom-right placement.
- Uses a premium compass-style frame asset.
- Renders the real Embervale map at minimap scale.
- Shows the player's position and facing direction.
- Uses real map marker icons extracted from the game's map UI.
- Shows nearby points of interest that are visible or detected by the map.
- Shows other players as green arrows, pings as green diamonds, and your
  selected waypoint as a hollow yellow outline.
- Mirrors world-map locations, altars, NPC markers and custom pins.
- Uses original location icons by default, with optional gold frames,
  adjustable terrain lighting and heading smoothing.
- Uses fog-of-war and POI data as a fallback when the game does not expose all
  live markers.
- Renders inside the game's Vulkan frame without a separate overlay window.

## Controls

- `+` zooms in.
- `-` zooms out.
- `F10` toggles the minimap on/off without leaving the game.

The numpad `+`, `-`, and `*` keys also work.

## Download

Download the latest zip from the release page:

[Community Edition v0.5.0](https://github.com/Aerox912/Enshrouded-minimap/releases/tag/v0.5.0)

Release asset:

`enshrouded-minimap-community-v0.5.0.zip`

Requires Windows x64 Enshrouded,
[Shroudtopia 0.1.1](https://www.nexusmods.com/enshrouded/mods/43), and the
[Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe).
Visual Studio is not needed to use the mod. Install the minimap on each player's
client; no server component is required.

## Installing the Zip

1. Close Enshrouded.
2. Install Shroudtopia if needed and download `enshrouded-minimap-community-v0.5.0.zip`.
3. Back up any existing `mods/minimap_mod` folder outside the `mods` directory.
4. Extract the ZIP into your Enshrouded game directory. The resulting mod folder is:

```text
C:\Program Files (x86)\Steam\steamapps\common\Enshrouded\mods\minimap_mod
```

The final folder should look like this:

```text
Enshrouded
+-- mods
    +-- minimap_mod
        +-- minimap_mod.dll
        +-- mod.json
        +-- embervale_minimap_frame.rgba
        +-- embervale_minimap_icons.bin
        +-- embervale_player_arrow.rgba
        +-- embervale_realmap_1280.rgba
        +-- ...
```

5. Start Enshrouded with Shroudtopia.

If you already had an older version installed, replace the whole `minimap_mod`
folder with the new one. Keep only one copy inside `mods`. The ZIP does not
replace `shroudtopia.json`, so your existing preferences remain in place.

To uninstall, close the game and remove `mods/minimap_mod`. Leave Shroudtopia
installed if other mods need it. To roll back, restore your backed-up mod folder.

## Position Config

The minimap position is controlled from Shroudtopia's config file:

```text
C:\Program Files (x86)\Steam\steamapps\common\Enshrouded\shroudtopia.json
```

Set `mods.minimap_mod.position` to one of these values:

- `top-right`
- `middle-right`
- `bottom-right`

Example:

```json
{
  "mods": {
    "minimap_mod": {
      "active": true,
      "position": "bottom-right",
      "toggle_key": "F10",
      "render_camera_fallback": true,
      "debug_logging": false,
      "show_other_players": true,
      "show_pings": true,
      "show_waypoints": true,
      "show_world_markers": true,
      "map_light": 55,
      "icon_style": "original",
      "heading_smoothing_ms": 55,
      "map_sample_step": 2,
      "max_icons": 64
    }
  }
}
```

The mod also reads `mods.minimap_mod.toggle_key` every second while active.
Recommended value: `F10`. Supported readable values include `F1`-`F24`,
`insert`, `delete`, `home`, `end`, `pageup`, `pagedown`, `backspace`, and
`numpad-*`.

`render_camera_fallback` is enabled by default. It lets the minimap recover its
player position from render data when a game update stops the normal UI position
feed from firing before the minimap draws.

The mod reads these values directly and refreshes them every second while active.
If the minimap is not loaded yet, start or restart the game after changing it.

## Live markers and world-map display

This release includes client-side markers for Steam client data revision
1076226, developed on `feature/multiplayer-player-markers`.
Other-player markers, pings and waypoints were confirmed working in user
gameplay testing on 2026-10-03.
The combined world-map display and graphics build was confirmed working in
user gameplay testing on 2026-10-04, with original location icons selected.
Both upstream PRs are ready for review following that confirmation; individual
extended-session and display-change results were not separately recorded.

- Other players use the local player's arrow artwork tinted green, sampled up
  to ten times per second. Arrows follow their facing direction relative to the map.
  Only players whose transforms are available on your client can be shown;
  full-world coverage is not guaranteed. Missing transforms remove the marker,
  and an interrupted feed expires after two seconds.
- Your pings and other players' pings appear as green diamonds with a bright
  border and a dark downward arrow. A new ping replaces that sender's old ping;
  pings expire eight seconds after their last event.
- Active waypoints appear as hollow yellow diamonds. Their centres remain
  transparent so the map and any POI icon underneath stay visible. Moving or
  clearing a waypoint replaces or removes its marker.

Markers rotate and zoom with the map, and positions beyond the minimap edge
are clamped to its rim. They have separate visibility settings:
`show_other_players`, `show_pings`, and `show_waypoints` (all default to `true`).
These settings refresh every second. Live markers do not consume the POI
`max_icons` allowance. No server component is added by this feature.

See [live marker validation](docs/live-markers.md) for layout evidence and the
in-game regression checklist.

The minimap also mirrors world-map POIs, altars, NPC markers and custom pins,
adds parchment lighting and optional gold-framed icons, supports zoom up to +7, and
offers configurable rotation smoothing. Ordinary distant POIs no longer crowd
the map edge. These additions retain the green player/ping markers and the
hollow yellow selected-waypoint outline. Location icons use the original atlas
style by default; set `icon_style` to `"world-map"` to enable gold frames.

See [display settings and PR #4 feature coverage](docs/pr4-feature-parity.md)
for `map_light`, `icon_style`, `heading_smoothing_ms`, `show_world_markers`,
compatibility boundaries and test instructions.

## EML Compatibility Notes

This minimap is a native Shroudtopia mod. It is not an EML-native Lua mod and
EML does not load `minimap_mod.dll` by itself.

The release includes a no-op `src/mod.lua` and EML-neutral manifest fields so
EML setups that scan every folder under `mods/` do not treat `minimap_mod` as an
incomplete Lua package.

For mixed EML + Shroudtopia setups:

- Keep Shroudtopia installed with `winmm.dll` and `shroudtopia.dll`.
- Keep this mod at `mods/minimap_mod`.
- If EML with `dbghelp.dll` crashes on client startup, try EML's `dinput8.dll`
  proxy instead of `dbghelp.dll`, as recommended by EML's own troubleshooting.
- Do not expect an EML-only setup to load the minimap DLL. Shroudtopia is still
  required.

## Building From Source

Requirements:

- Windows.
- Visual Studio with MSBuild and the C++ toolset.
- Shroudtopia installed in the game folder to load the mod.

From a Visual Studio 2022 x64 Native Tools prompt, build into a separate output
directory:

```powershell
MSBuild minimap_mod.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:OutDir=E:\Build\Enshrouded-minimap\bin\ /p:IntDir=E:\Build\Enshrouded-minimap\obj\
```

Package a committed build for manual installation:

```powershell
.\package-release.ps1 -DllPath E:\Build\Enshrouded-minimap\bin\minimap_mod.dll -OutputDirectory E:\Build\Enshrouded-minimap\release
```

Use a new output directory for each package. The package includes runtime assets,
license, credits, source commit metadata and SHA-256 checksums. Follow the manual
installation steps above; building and packaging do not modify the game.

## Repository Layout

- `src/` - mod source code.
- `include/` - minimal Shroudtopia headers required to build.
- `assets/` - runtime assets copied next to the DLL.
- `build-release.ps1` - builds `Release|x64`.
- `install.ps1` - installs the mod and backs up the previous install.
- `package-release.ps1` - packages a built DLL with runtime assets and credits.
- `docs/releases/` - release notes and installation details.

## Version

Current community mod version: `0.5.0`.

The mod resolves hook signatures near the known Enshrouded client addresses at
load time, which makes small game updates less likely to break the minimap.
Large game updates can still require structure offsets to be revalidated.
