# Minimap 0.5.16 GPU test.4 candidate

This is the exact frozen source and package candidate selected for the ESM Manager 4.5.3 Regular and Admin builds. The source files are held under `source/` to preserve the current fork main code without replacing its newer 0.5.15 history. `SOURCE-MANIFEST.json` and `next-source.patch` bind the 224 reviewed inputs and three-file top-right default change.

The main repository workflow builds `minimap_mod.dll` from the frozen source with the reviewed Windows toolset. Historical `/Zi` and `/DEBUG` output embeds timestamp, PDB GUID and absolute PDB path, so byte-identical DLL reproduction is not expected. CI compares x64 architecture, `CreateModInstance` export and dependencies with the historical DLL, then replaces only that DLL in a new candidate ZIP. It also compiles and runs the frozen top-right/north-up native harness; its only changes are relative paths to this frozen source tree. CI records the old archive and DLL as immutable inputs, and the new DLL, ZIP, source and toolchain as outputs. This is build evidence only: no GitHub Release or Nexus publication occurs here. Runtime gameplay acceptance remains pending.

Original fork: https://github.com/Aerox912/Enshrouded-minimap

- Source manifest: `282b8c3611b17f1b807afa948c9419d60f053db118ca31c7fd22705927ad2c05`
- Historical runtime DLL: `38876e8bf9ae41df6b112927c15eb328b7f8239b3911f7969a3e72cf4c96d8f1`
- Historical input archive: `b115ef7482b6c929efda5b8bb52b527f173e24ea932d71cb24952e06a215d025`
- Focused settings contract: top-right position, north-up orientation, map sample step 2, offset 0. Static source and package check; no game runtime.
