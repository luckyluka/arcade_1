#!/usr/bin/env python3
"""Recolour an existing style pack into palette variants. Ramp NAMES stay the same, so every recipe keeps working.
usage: python3 tools/gen_ramps.py <template style name> [--keep skin,other] [--out staging]
Writes staging/<template>_<variant>.style.json; then run tools/lib_add.py on the ones you like."""
import argparse
import colorsys
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# variant: (hue shift degrees, saturation factor, value factor)
VARIANTS = {"dusk": (-30, 0.9, 0.9), "ember": (20, 1.1, 1.0), "mint": (110, 0.9, 1.0), "ocean": (-90, 1.0, 0.95),
            "toxic": (70, 1.2, 0.95), "ash": (0, 0.35, 0.95), "candy": (-140, 0.85, 1.1), "night": (-60, 0.8, 0.7)}


def shift(rgb, dh, fs, fv):
    h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in rgb))
    h = (h + dh / 360) % 1
    s, v = min(1, s * fs), min(1, v * fv)
    return [round(c * 255) for c in colorsys.hsv_to_rgb(h, s, v)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("template")
    ap.add_argument("--keep", default="skin", help="comma list: ramp names containing these stay unchanged")
    ap.add_argument("--out", default="staging")
    a = ap.parse_args()
    keep = [k for k in a.keep.split(",") if k]
    doc = json.loads((ROOT / "registry" / "styles" / f"{a.template}.style.json").read_text())
    outline = doc.get("outline", {}).get("ramp")
    out = ROOT / a.out
    out.mkdir(parents=True, exist_ok=True)
    for vname, (dh, fs, fv) in VARIANTS.items():
        new = json.loads(json.dumps(doc))
        new["style_pack"] = f"{a.template}_{vname}"
        if "description" in new:
            new["description"] = f"{new['description']} ({vname} palette variant)"
        for rname, tones in new["ramps"].items():
            if rname == outline or any(k in rname for k in keep):
                continue
            for tone, rgb in tones.items():
                if isinstance(rgb, list) and len(rgb) == 3:
                    tones[tone] = shift(rgb, dh, fs, fv)
        p = out / f"{new['style_pack']}.style.json"
        p.write_text(json.dumps(new, indent=2) + "\n")
        print("wrote", p)


if __name__ == "__main__":
    main()
