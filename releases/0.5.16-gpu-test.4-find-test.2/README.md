# Shroudweave Minimap 0.5.16-gpu-test.4-find-test.2

Private Manager 4.5.4 build candidate. Windows native fix accepts the reviewed Creative-over-Find 1.1.2 hook chain while retaining existing guards. Find and Creative binaries remain unchanged.

## Frozen inputs

- Base commit: `b876e501bd6ae84f956deca32cfabd7b61d2ce70`.
- Accepted 224-file source manifest: `8798613c6d87c0df89ba9c73a6afd5c76486fdb6311af4aeb5b031f10665dccd`.
- Exact source patch: `9cc34e91d5d1f7ef685b33dd3aaab88f9eed7b4afa5a09ae74e81ee543d1bfb5`.
- Independent source/archive review: `1067f73d95def8ee7c569b5233da89b362ab2e165242e88e67219fbde4b759e5`.
- Candidate archive changes only `mods/minimap_mod/mod.json` version to `0.5.16-gpu-test.4-find-test.2`; CI rebuilds the Minimap DLL from frozen source and verifies every other archive member.

## Acceptance limits

Offline source, fixtures, and archive review cover only the observed Creative-over-Find order and pinned public binaries. Live Find trampoline bytes, gameplay/HUD behavior, and FPS improvement remain unverified. This pipeline does not publish a GitHub Release, install the mod, or change game/server state.
