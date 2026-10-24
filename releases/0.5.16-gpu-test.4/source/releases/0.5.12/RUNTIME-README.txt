# Sparkweave Minimap 0.5.12

Sparkweave Minimap is an independently maintained community fork of the Enshrouded Minimap. It keeps the existing `minimap_mod` loader ID and settings for compatibility.

## Features

- North-up map orientation by default; set `mods.minimap_mod.map_orientation` to `rotating` for heading-up.
- Death markers retain the established red skull appearance.
- Terrain rendering uses the GPU when available and falls back to the CPU path.
- The minimap hides during unsupported menus and restores during gameplay.

The release retains the existing map sizes and settings. GPU use is an implementation detail; no FPS or frame-time improvement is claimed. Compatibility depends on the maintained Shroudtopia and WINMM loader pair, which is not included.

## Install

Extract this archive into the Enshrouded game directory, keeping the `mods/minimap_mod` folder structure. Back up an existing `mods/minimap_mod` folder before replacing it. Configure the mod through the existing Shroudtopia configuration; do not replace that configuration with a fragment.

## Credits and license

The original minimap was created by elxokker (xoker). This community-maintained fork is by Aerox912 and preserves upstream work, credits, and the MIT license in `mods/minimap_mod/CREDITS.md` and `mods/minimap_mod/LICENSE`. Sparkweave is an independent community brand and is not affiliated with Keen Games.