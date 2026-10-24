# ESM Minimap map appearance

The 0.5.10-local-r2 preview uses the bundled 2048 x 2048 Embervale map by default. Set `mods.minimap_mod.map_style` in `shroudtopia.json` to `"original"` to use the original map. Existing `"bright"` settings remain accepted as an alias for the default map. Changes apply while the mod is active.

Both textures share the same world bounds. The default texture is loaded from `assets/embervale_realmap_default_2048.rgba`; if it is missing or malformed, the renderer falls back to the original map. The original texture and saved map preference are preserved.

The appearance choice is independent of fog, markers, icons, terrain lighting, frame and clock. `map_light` continues to control terrain lighting for either texture.

## Source and acceptance

The public source snapshot and asset provenance are recorded in the [r2 compatibility report](CLIENT-1.0.md) and `assets/realmap-provenance.json`. Original map artwork and terrain belong to Keen Games; the original minimap is by elxokker. The MIT license and upstream credits remain included.

The bounded r2 live scope covers native SDR local alignment, hide/show placement and normal restart persistence. HDR/scRGB/PQ output remains experimental and unverified in game. Offline image tests do not establish additional game regions, multiplayer behavior or other display setups.
