# MM-TOP-RIGHT-DEFAULT-20261024

Local source-only change, verified on 24 October 2026. Next-build source: `source/` in this directory.

- All 224 files match `result-manifest.json`, SHA256 `282b8c3611b17f1b807afa948c9419d60f053db118ca31c7fd22705927ad2c05`.
- Compared with the frozen predecessor, the complete runtime source differs only in initial placement and the missing-position setting default, both now top-right. Configuration precedence, explicit positions, aliases and parser fallback behavior are unchanged.
- The parsed descriptor differs only in default position. Sample step 2, north-up, offset 0 and all other settings are unchanged. README defaults/examples match top-right.
- `next-source.patch` SHA256: `fe7d4009b4df7796b488ec3dbb41a6b9612b8374e78a9713c85aff268367555e`. Only `src/dllmain.cpp`, `assets/mod.json` and `README.md` differ in the source manifest; all other source/assets/guards are preserved.
- Canonical development defaults are also top-right. Frozen predecessor manifest remains `0c27f8d19bdfc2f30afe3ffa4a78d5b3169b61b71360cc6a7c6553503745fe6e`.

No new native build, DLL/ZIP, runtime test, install, live setting change, commit, push or publication. Existing accepted test.3 artifacts remain the prior candidate. A later authorized build must use this source and assign its own immutable artifact version and receipts.
