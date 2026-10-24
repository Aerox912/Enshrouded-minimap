# ESM Minimap 0.5.10-local-r2: Enshrouded 1.0

This public preview targets Steam client build **25988036**, executable SHA-256
`82e5020a8736348cd0374f004b2f5e5ef456b5bd27aa5420df34c7dd241cfeeb`. It requires
the maintained Shroudtopia native loader and proxy. EML alone cannot load this DLL.
The technical mod ID remains `minimap_mod`; upstream credits and the MIT license are retained.

## Published artifacts

- Preview: [ESM-Minimap-0.5.10-local-r2-preview.zip](https://github.com/Aerox912/Enshrouded-minimap/releases/download/v0.5.10-local-r2/ESM-Minimap-0.5.10-local-r2-preview.zip)
- Source snapshot: [ESM-Minimap-0.5.10-local-r2-source.zip](https://github.com/Aerox912/Enshrouded-minimap/releases/download/v0.5.10-local-r2/ESM-Minimap-0.5.10-local-r2-source.zip)
- Source baseline: public `main` commit `5241d3830f9b1034614837fc08bbed8709e0ac6a`
- Source ZIP SHA-256: `50f2148bf24cc785a59261f50cdd57909dadc40edb220ebb89c58301530d1a82`
- Preview DLL SHA-256: `9b2f5646a5332798cf8d20098d73b41fcc3341822008ac9a962a23f2ecfb0b73`

## r2 source changes

- Update the client profile and named-instruction guards for build 25988036.
- Select the bundled 2048 x 2048 map by default while preserving the Original map option.
- Require Vulkan image-view create/destroy functions in the ready predicate and unwind partially created renderer resources on failures.
- Add client-profile, frame-failure, HDR and patch-profile test projects.

## Test and compatibility boundary

`ci/build.ps1` builds and runs the mod and eight test projects with MSVC v143. The test projects exercise production readers and guards using private mapped images, synthetic data or mocked Vulkan calls; they do not execute game instructions or establish in-game rendering.

The accepted live scope recorded with the r2 source is native SDR local alignment, hide/show placement and normal restart persistence. HDR/scRGB/PQ remains experimental and unverified in game. This is a client mod requiring Shroudtopia/WINMM, not an EML-native mod or a server component. Do not infer multiplayer, every-setting, every-display or all-region acceptance from the bounded local scope.

For installation, close Enshrouded and replace only `mods/minimap_mod` from the preview ZIP. Keep the existing `shroudtopia.json` and back up the prior mod folder before updating.
