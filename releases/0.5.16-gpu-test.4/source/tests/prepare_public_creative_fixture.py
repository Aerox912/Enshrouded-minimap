"""Builder-only test data preparation. Reads ZIP bytes; never loads its DLL.

The output is excluded from every distributable. No third-party binary/template
is embedded in the test source or shipping source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ZIP_SHA = "7da33ed560300f1cbe9cc4dd43aaf97ae247b0a7576669f05b10b9b18d00ca61"
DLL_SHA = "dbc26a853675f741d6cd0d4676c8f0424c916d9a18c7ed2c262d38716d98b136"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zip", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    assert hashlib.sha256(args.zip.read_bytes()).hexdigest() == ZIP_SHA, "ZIP identity mismatch"
    assert not args.out.exists(), "test-data directory must be new"
    with zipfile.ZipFile(args.zip) as archive:
        data = archive.read("creative/creative.dll")
    assert len(data) == 5558784 and hashlib.sha256(data).hexdigest() == DLL_SHA, "DLL identity mismatch"
    args.out.mkdir(parents=True)
    good = args.out / "public-module-data.bin"
    wrong = args.out / "wrong-module-data.bin"
    good.write_bytes(data)
    changed = bytearray(data)
    changed[-1] ^= 1
    wrong.write_bytes(changed)
    rows = [{"path": p.name, "bytes": p.stat().st_size,
             "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in (good, wrong)]
    (args.out / "fixture-receipt.json").write_text(json.dumps({"zip_sha256": ZIP_SHA,
        "scope": "inert test data only; do not redistribute", "files": rows}, indent=2) + "\n")
    print("prepared 2 inert fixture files; no DLL loading or execution")


if __name__ == "__main__":
    main()
