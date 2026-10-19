# Sparkweave Minimap 0.5.12

Community-maintained Enshrouded minimap by Aerox912, based on the original work
of elxokker (xoker). This is an independent community mod, not an official Keen
Games product. See [credits](CREDITS.md) and [license](LICENSE).

North-up is the default orientation; rotating remains optional. Terrain uses
GPU rendering with a CPU fallback. Death markers retain their red skull, and
the minimap hides during controller menus. No measured FPS gain is claimed.
The required Shroudtopia/WINMM loader pair is not bundled.

The root native source, tests, shaders and assets are the exact 57 files from
the accepted 0.5.12 source archive. [The retained snapshot](releases/0.5.12/)
records the original package, provenance and acceptance. Its historical BUILD.ps1
is preserved; use [the current build instructions](BUILD-TEST.md) for this tree.
The new pipeline produces candidate archives from a fresh build. Those archives
are not replacements for the accepted published bytes until independently reviewed.

F10 toggles the minimap, F9/F11 change its size, and +/- change zoom. Existing
mod identifier `minimap_mod` and settings remain compatible.

This integration adds no installation, game launch, release tag or publication.
