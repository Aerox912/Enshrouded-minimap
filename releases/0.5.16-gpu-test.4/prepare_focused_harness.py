from __future__ import annotations

import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
request = json.loads((root / "BUILD-REQUEST.json").read_text(encoding="utf-8-sig"))
pins = request["focusedHarness"]
harness_root = root / "tests/focused-default"
provenance = harness_root / "provenance"
original_cpp = provenance / "top-right-default-test.cpp"
original_project = provenance / "top-right-default-test.vcxproj"
fixture = provenance / "position-config.json"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


cpp_raw = original_cpp.read_bytes()
project_raw = original_project.read_bytes()
fixture_raw = fixture.read_bytes()
for name, data, expected in (
    ("original harness", cpp_raw, pins["originalHarnessSha256"]),
    ("original project", project_raw, pins["originalProjectSha256"]),
    ("position fixture", fixture_raw, pins["fixtureSha256"]),
):
    if digest(data) != expected:
        raise SystemExit(f"frozen focused harness {name} hash mismatch")

old_include = b'#include "E:\\Repos\\Enshrouded-minimap\\scratch\\minimap-top-right-default-20261024\\source\\src\\dllmain.cpp"'
new_include = b'#include "../../source/src/dllmain.cpp"'
if cpp_raw.count(old_include) != 1:
    raise SystemExit("expected one absolute dllmain.cpp include in frozen harness")
adapted_cpp = cpp_raw.replace(old_include, new_include, 1)

old_root = b"E:\\Repos\\Enshrouded-minimap\\scratch\\minimap-top-right-default-20261024\\source"
if project_raw.count(old_root) != 3:
    raise SystemExit("expected three frozen project source-root paths")
adapted_project = project_raw.replace(old_root, b"../../source")

(harness_root / "top-right-default-test.cpp").write_bytes(adapted_cpp)
(harness_root / "top-right-default-test.vcxproj").write_bytes(adapted_project)
manifest = {
    "status": "FROZEN_FOCUSED_HARNESS_PREPARED",
    "originalHarnessSha256": digest(cpp_raw),
    "originalProjectSha256": digest(project_raw),
    "fixtureSha256": digest(fixture_raw),
    "adaptedHarnessSha256": digest(adapted_cpp),
    "adaptedProjectSha256": digest(adapted_project),
    "cppIncludeReplacements": 1,
    "projectSourceRootReplacements": 3,
    "assertionsChanged": False,
    "sourceManifestSha256": request["sourceManifestSha256"],
}
manifest_path = root / "dist/receipts/focused-default-harness-input.json"
manifest_path.parent.mkdir(parents=True, exist_ok=True)
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(json.dumps(manifest, sort_keys=True))
