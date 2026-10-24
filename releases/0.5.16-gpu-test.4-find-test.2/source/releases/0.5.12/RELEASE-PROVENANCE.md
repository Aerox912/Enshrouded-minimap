# Release provenance

- Public version: 0.5.12
- Accepted runtime input ZIP SHA-256: caab300c02c815fac6eb44839dbef9886c614c37793ab60f3ccb10440540ee3b
- Accepted source input ZIP SHA-256: ebad69b63f71a3c1ef6e73735a58b8738d88674805ac1b68c5924854202a47a2
- Accepted source inventory: SOURCE-INPUT-MANIFEST.json, version 0.5.12-local-r4, SHA-256 1a8d7a82ce75ad3c240a26395ee5d97b156b784a147f016754b97ff12c543707
- Accepted source tree: 57 files, SHA-256 2312a21aa78dbf79ae6a7d7a43b5ec1051e93dc3d5d652dc4ccddb90221ae45c; unchanged from accepted source snapshot
- Accepted runtime DLL SHA-256: eda8ad629d2c8613cb7e6f8076c2bb346983e81f380646768ff208ae592d9182; unchanged

The public wrapper overlays release metadata on the accepted r4 snapshots. The frozen BUILD.ps1 targets 0.5.12-local-r4; it was not run to emit these stable 0.5.12 ZIPs. The frozen source inventory is preserved as historical provenance. SOURCE-OVERLAY-MANIFEST.json records the two changed files that were inputs to that inventory, including their accepted and overlay hashes. No native code, shader, map, default-setting, or runtime DLL bytes changed.