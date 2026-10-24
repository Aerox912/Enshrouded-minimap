# Sparkweave Minimap 0.5.12

![Sparkweave logo](branding/assets/sparkweave-logo-dark-v1.png)

![Sparkweave mod-family banner](branding/assets/sparkweave-mods-banner-dark-v1.png)

Stable public package metadata for Sparkweave Minimap 0.5.12, prepared from the accepted r4 runtime and source snapshots. The `minimap_mod` identifier and existing settings are preserved. No runtime rebuild or source-code change was made for this packaging step.

North-up is the default map orientation; `rotating` selects heading-up. Terrain rendering uses the GPU when available with a CPU fallback. Death markers retain the red skull appearance. No FPS or frame-time improvement is claimed. The required Shroudtopia and WINMM loader pair is not bundled.

The `package/` directory contains the standalone runtime payload. The `source/` directory contains the matching native source, tests, shaders, and assets. `CHANGELOG.md` lists the public-facing release notes; `RELEASE-PROVENANCE.md` records the exact accepted inputs and unchanged payload hashes.

The original minimap was created by elxokker (xoker). This community-maintained fork is by Aerox912. Original credits, the MIT license, and third-party attribution are preserved in the package.