"""Extract a coarse color reference; never output or replace terrain artwork."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("reference", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
if args.output.exists() or args.output.with_suffix(".json").exists():
    raise SystemExit("Output already exists")
raw = args.reference.read_bytes()
with Image.open(args.reference) as reference:
    if reference.width != reference.height:
        raise SystemExit("Expected the selected square reference")
    # Area-averaged color samples only: no reference geometry is exported.
    palette = reference.convert("RGB").resize((32, 32), Image.Resampling.BOX).tobytes()
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("xb") as stream:
    stream.write(palette)
args.output.with_suffix(".json").write_text(json.dumps({
    "purpose": "Bright color reference for ESM Minimap; not terrain or biome data",
    "source": "User-selected AI color concept; HD-map-inspired",
    "referenceSha256": hashlib.sha256(raw).hexdigest(),
    "paletteSha256": hashlib.sha256(palette).hexdigest(),
    "format": "RGB8", "width": 32, "height": 32,
    "sampling": "area-average; normalized coordinates; no terrain resampling",
    "geographySource": "Original minimap RGBA only",
    "newRegionCoverage": False,
    "runtimeValidated": False
}, indent=2) + "\n", encoding="utf-8")
