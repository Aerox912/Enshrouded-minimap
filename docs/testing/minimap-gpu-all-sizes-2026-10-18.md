# Minimap 0.5.12-local-r1 candidate evidence (2026-10-18)

This record binds the repository source candidate to the sealed local build and offline acceptance artifacts. It is not a release or live-game acceptance report.

- Source input manifest SHA-256: `654672163ab87783682c600b708bab128108b1e08f7adc4b835d678b0dc711ee`.
- Runtime DLL SHA-256: `bad979ce590bbb3eb8941a894f41abd964ef097a2b09e14aa791f9784b195cd4` (399,360 bytes).
- Sealed build receipt SHA-256: `b98ee71500f208e5166b2cc617302e1e7aec9c354e5a33eb21117e6b5bc88486`.
- Sealed runtime ZIP SHA-256: `03d0dd38da7117e88cf5eb14387d8722583473c5ee5e5191e320a9a2d9f4c650`.
- Sealed source ZIP SHA-256: `5469a399714ef2a6ddfbdca6410bab865b89e1e4b014f5878d7855ebfd8e519b`.
- Local build: 12/12 projects succeeded (runtime plus 11 native tests); 2,777 PASS markers, zero FAIL markers, empty test stderr. Four MSB8028 shared-intermediate-directory warnings were recorded.
- Independent offline acceptance: 168 GPU full-frame cases, 56 production widget cases, 44 CPU fallback frame comparisons, 38 literal placement checks, and fog init/unchanged/dirty/reset/stale scenarios. Coverage includes 50–200% sizes, sample steps 1–4, north-up/rotating orientations, SDR/scRGB/PQ, and offline Vulkan readback on an RTX 5080 Laptop GPU.
- Accepted test records: `E:\Build\minimap-gpu-all-sizes-20261018\TEST-RESULTS.md` and `E:\Build\minimap-gpu-all-sizes-20261018\independent\final_acceptance_evidence.json`.

`SOURCE-INPUT-MANIFEST.json` and `BUILD-MANIFEST.json` preserve the source and build identities. The repository copy updates only map ownership metadata after Nick confirmed ownership and authorized inclusion; the supplied map bytes remain identical to the tested candidate. The runtime DLL was not copied from the candidate snapshot because that snapshot contains a stale DLL; no binary is committed here.

## Limits

Live game/install acceptance was not performed. No physical HDR monitor or display chain was tested. No FPS or performance-improvement claim is made. The GPU performance verifier was absent from the accepted build receipt. The GitHub CI suite runs fixture-free native checks; the sealed local receipt additionally records fixture-backed client/profile tests.
