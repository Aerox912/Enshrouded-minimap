ESM Minimap 0.5.8

Close Enshrouded. Install the matching Shroudtopia loader/proxy from Optional Files.
Back up existing shroudtopia.dll and winmm.dll, then extract both beside enshrouded.exe.
Keep shroudtopia.json and other mods. Resolve any other winmm.dll owner before replacing it.
Extract the Minimap ZIP into the game folder: mods/minimap_mod/minimap_mod.dll.
Keep only one minimap_mod folder.

In shroudtopia.json, merge into the existing mods object:
"minimap_mod": { "active": true, "map_style": "bright", "position": "top-right" }
Use map_style "original" for original colors. Do not overwrite other mod settings.

F10: hide/show. Plus/minus: zoom. F9/F11: smaller/larger.
See README.md for settings, credits and tested scope.
To roll back: close the game and restore the backed-up mod and loader files.
