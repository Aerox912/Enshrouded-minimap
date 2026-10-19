# ESM Minimap color appearance (local candidate)

Nick selected the bright concept as a color reference on 15 October 2026 and
explicitly requested preservation of the accurate map, not use of the illustration
as terrain. This supersedes the rejected base-height renderer.

Set `mods.minimap_mod.map_style` in `shroudtopia.json` to `"original"` or `"bright"`.
Bright is the default. An explicitly saved Original selection is preserved.
The existing settings poll applies the selection live.
This adds a mod configuration setting; it does not change the separate Mod Manager UI.

Both appearances use the released source RGBA map. Bright changes RGB values at
the same texel only. Its weighted byte-space RGB brightness follows a monotonic
curve of the original; this is not a physical luminance measurement. Alpha and
pixel positions remain unchanged. Eight-bit rounding can merge small brightness
differences, and replacing chroma can reduce original color-only boundaries.
It does not guarantee preservation of every original visual detail.
A 32 x 32 RGB color-reference field
is area-averaged from the selected concept and smoothly sampled. This is
illustrative color placement, not verified game biome metadata or a precise
reproduction of the Deluxe print map. No new contours, lava channels, land/ocean
masks or terrain geometry come from that reference. Markers, fog, frame and clock
remain separate layers. Existing `map_light` still applies to both appearances.

The derived color cache is built once when terrain loads, under the existing map
mutex. Switching appearances selects a cache. Missing, malformed or unreadable
palette data keeps Original usable. Optional-cache allocation exceptions are
contained so Original still loads. Invalid settings also select Original.

## Sources and reproducibility

- Original map artwork and terrain: Keen Games; original minimap: elxokker.
- Color reference: Nick's selected AI-generated HD-inspired concept, used only
  for its coarse colors. Reference and palette SHA-256 values are recorded in
  `assets/embervale_bright_palette.json`.
- `tools/extract-color-palette.py` extracts the palette from that square source
  image using Pillow area averages. It refuses to overwrite outputs.
- `tools/preview-map-style.cpp` uses the same `src/map_appearance.h` function as
  the mod. Pass original RGBA, palette RGB and a new output RGBA filename. It
  neither opens a game nor writes source assets.

## Acceptance boundary

The source RGBA files remain byte-identical to v0.5.7. This color change does not
establish updated northern region coverage, restore missing native layers from
base-height data, or establish compatibility with client build 25936191 / data
revision 1265863. Those remain separate work. The ESM port checkout is unchanged.

Offline checks cover live configuration selection, original fallback, palette
validation, exact source preservation, alpha, color orientation and the weighted
brightness curve. Live landmark/marker alignment, current-game loader compatibility and
visual acceptance remain pending. No installation or release is included.
