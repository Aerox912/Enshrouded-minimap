# Live fog and weather (0.5.3)

The runtime options `show_fog_of_war`, `show_clock`, and `show_weather` all
default to `true`. Clock and weather are independent: the panel can show both,
either one alone, or neither. A single feature uses the compact bronze frame.
With weather visible, the clock contains only the time. One weather symbol combines daylight and
conditions: a sun or moon for clear skies, or that celestial symbol behind the
rain/snow/blizzard cloud. Both combined and weather-only panels use the native
daylight flag, even when time is hidden. If weather is disabled or unavailable,
the day/night icon appears beside the clock instead. If daylight data is unavailable, only
the condition is drawn; the renderer does not guess day or night.
The top-right map clears the quest/Journal area; the footer slightly overlaps
the lower ornament without covering terrain.

## Exploration source

On Steam client data revision 1076226, the named `fog_of_war` system (descriptor
RVA `0x1D48D00`, callback `0x2A3EC0`) checks ownership of the local player before
calling its discovery updater at `0x2A3F84` (target `0x320E60`). The wrapper
forwards the original object, position and XMM2 radius unchanged, then copies
the completed native grid at most twice per second. It never changes game data.

The object stores world width/height at +0, tile columns/rows at +8, data at
+0x18 and tile count at +0x20. Each tile contains 32x32 bytes. The constructor
at `0x320070` rounds world extents to tiles at 0.125 pixels per metre; the tile
accessor at `0x320460` uses `(tileY * columns + tileX) * 1024`. Discovery uses
`(worldHeight - worldZ) * 0.125`, with pixel centres offset by 0.5. Zero means
unexplored; 255 is fully explored. The minimap bilinearly samples those bytes
using the same world coordinates as its rotating terrain.

Header dimensions, allocation length, coordinate scale, native call, ownership
guard and object accesses are validated. Copied grids are capped at 16 MiB,
held as immutable owned snapshots and checked for session identity and two-second
freshness. The renderer takes one shared snapshot per draw, with no per-pixel
locks or game reads. World exit discards it. Missing, stale or unsupported data
keeps terrain covered while fog is enabled; disabling the option restores the
full terrain and its location icons. Terrain and location icons use one snapshot
per frame. Original location icons are filtered before the icon limit using their world positions
and a 50% reveal threshold at the fog's soft edge. Players, pings, custom pins, player-placed Flame Altars, waypoints,
tombstones and quest markers stay visible. Native quest identity is retained
even without an atlas entry or when a custom pin overlaps it.
The optional legacy static POI mask is not used as live exploration data.

## Weather source

The named `ambient_effects` system (descriptor `0x1D44550`, callback `0x29C830`)
guards the local player and calls its weather sampler at `0x29CBF8` (target
`0x9AE790`). Its completed four-float blend corresponds to the native WeatherState
enum: Clear, Rain, Snow and Blizzard. The highest weight determines the icon and
label. Weather capture is limited to ten samples per second. Missing, invalid or
stale samples hide weather alone; they never fabricate clear conditions.

Both wrappers retain an E8 CALL at the original site and use a near leaf relay
to the C++ wrapper. This preserves Windows x64 argument registers, shadow space,
stack alignment and return address. Native calls are made even when the display
options are disabled. Hooks and their relay allocations are removed on unload.

## Validation

Focused tests execute both real call relays against synthetic native functions,
checking arguments, floating-point radius, return value, original output and
restoration of the original call. Tests cover tile origins, row stride, tile
boundaries, vertical orientation, rectangular worlds, invalid headers and bytes,
world exit, tick wrap, stale data and independent configuration combinations.
Production terrain drawing is exercised at four headings and two zoom levels.
Executable gates are tested against the installed Steam image and mutations of
the relevant instructions. All 470 focused checks pass (17 tracking, 27 renderer, 58 live-marker and
368 map-feature checks). Gameplay screenshots supplied on 4 October 2026 show
unexplored regions, clear daylight, rain with its combined icon, and clear night
with a crescent. Snow, blizzard and all independent visibility combinations are
covered by automated checks; those states were not separately shown in gameplay.

Custom pins on an existing marker keep that marker's original icon and add a
transparent red diamond outline. Pins on empty terrain remain red flags. Both
remain visible through fog, and removing a pin removes its outline.
