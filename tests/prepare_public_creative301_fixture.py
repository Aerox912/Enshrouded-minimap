"""Prepare inert test bytes from the pinned Creative 3.0.1 module.

This reads the supplied DLL as data. It never loads or executes the module.
The output belongs only to the private test-data directory and is not shipped.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile


DLL_SIZE = 6_122_496
DLL_SHA256 = "d769e38379d5224ef6d4dffa478b99af4ff3fe5a14733cd12849a08c7435eefe"
FIND_ZIP_SHA256 = "2639ef5bbcdba5512c02fd65271f27eb39f23020a98327764e7bd33a314a5a1a"
FIND_MEMBER = "greeneye/find/find.dll"
FIND_SIZE = 472_576
FIND_SHA256 = "82ba715080195ac8aeceac80f341cb4466991278e21b8a24ebb836a3f2be18c5"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", required=True, type=Path)
    parser.add_argument("--find-zip", type=Path,
                        help="optional pinned Find 1.1.2 archive for Creative-over-Find tests")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    data = args.dll.read_bytes()
    assert len(data) == DLL_SIZE, "Creative 3.0.1 DLL size mismatch"
    assert sha256(data) == DLL_SHA256, "Creative 3.0.1 DLL identity mismatch"

    find_archive_sha256 = None
    find_data = None
    if args.find_zip:
        archive_bytes = args.find_zip.read_bytes()
        find_archive_sha256 = sha256(archive_bytes)
        assert find_archive_sha256 == FIND_ZIP_SHA256, "Find 1.1.2 archive identity mismatch"
        with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
            find_data = archive.read(FIND_MEMBER)
        assert len(find_data) == FIND_SIZE, "Find 1.1.2 DLL size mismatch"
        assert sha256(find_data) == FIND_SHA256, "Find 1.1.2 DLL identity mismatch"

    assert not args.out.exists(), "test-data directory must be new"

    args.out.mkdir(parents=True)
    good = args.out / "public-module-data.bin"
    wrong = args.out / "wrong-module-data.bin"
    good.write_bytes(data)
    changed = bytearray(data)
    changed[-1] ^= 1
    wrong.write_bytes(changed)
    outputs = [good, wrong]
    if find_data is not None:
        find_path = args.out / "find112-module-data.bin"
        find_path.write_bytes(find_data)
        outputs.append(find_path)

    files = [
        {"path": path.name, "bytes": path.stat().st_size,
         "sha256": sha256(path.read_bytes())}
        for path in outputs
    ]
    receipt = {
        "source_dll_bytes": DLL_SIZE,
        "source_dll_sha256": DLL_SHA256,
        "scope": "inert test data only; do not redistribute",
        "files": files,
    }
    if find_archive_sha256 is not None:
        receipt["find_source_archive_sha256"] = find_archive_sha256
    (args.out / "fixture-receipt.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )
    print("prepared inert Creative 3.0.1 fixture bytes; no DLL loading or execution")


if __name__ == "__main__":
    main()
