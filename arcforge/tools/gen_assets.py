#!/usr/bin/env python3
"""Seeded random ArcForge asset generator + contact sheet.
usage: python3 tools/gen_assets.py --n 200 --seed 1 [--archetype NAME] [--out out/gen]"""
import argparse
import json
import random
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import arcforge_ref as A


def sample(specs, style, rng):
    out = {}
    for k, s in sorted(specs.items()):
        t = s["type"]
        if t == "int":
            out[k] = rng.randint(s["min"], s["max"])
        elif t == "bool":
            out[k] = rng.random() < 0.5
        elif t == "enum":
            out[k] = rng.choice(s["values"])
        elif t == "ramp":
            allowed = s.get("allow") or [r for r in sorted(style["ramps"]) if r != style["outline"]["ramp"]]
            out[k] = rng.choice([r for r in allowed if r in style["ramps"]])
    return out


ANIMS = ("idle", "walk", "attack", "hurt", "death")


def pick_anims(arch):
    """Animation keys must be the short names; the archetype's template (e.g. idle_breathe) goes in 'template'."""
    out = {}
    for t in arch.get("animations", {}):
        k = next((k for k in ANIMS if t == k or t.startswith(k + "_")), None)
        if k and k not in out:
            out[k] = {"template": t}
    return out


def make_doc(reg, rng, aname, i, seed):
    arch = reg["archetypes"][aname]
    style_name = rng.choice(sorted(reg["styles"]))
    style = reg["styles"][style_name]
    size_specs = arch.get("size") or {}
    archetype, size_val = {"name": aname}, None
    if size_specs:
        primary = next(iter(size_specs))
        s = size_specs[primary]
        size_val = rng.randint(s["min"], s["max"])
        archetype["size"] = {primary: size_val}
        canvas = {"mode": "auto", "margin": 1, "multiple_of": 1, "align": "pivot_bottom_center"}
        version = "2.1"
    else:
        canvas, version = arch["canvas"]["default"], "2.0"
    archetype["params"] = sample(arch["params"], style, rng)
    cands = [r for _, r in sorted(reg["recipes"].items())
             if aname in r["applies_to"] and all(a in arch["anchors"] for a in r["requires_anchors"])]
    rng.shuffle(cands)
    feats, used = [], set()
    for r in cands:
        if r["slot"] in used and not r.get("stackable"):
            continue
        sp = r.get("sizes")
        if sp and size_val is not None and not sp["min"] <= size_val <= sp["max"]:
            continue
        if isinstance(r["geometry"], dict) and aname not in r["geometry"]:
            continue
        if rng.random() < 0.6:
            used.add(r["slot"])
            feats.append({"recipe": r["recipe"], "params": sample(r["parameters"], style, rng)})
    return {
        "arcforge": version,
        "asset": {"id": f"gen.{aname}.s{seed}n{i}", "type": "character", "version": "1.0.0"},
        "target": {"profile": "arcade1-draft", "canvas": canvas, "clip_policy": "error"},
        "style_pack": style_name,
        "archetype": archetype,
        "features": feats,
        "animations": pick_anims(arch),
        "tests": [{"assert": "feature_present", "recipe": f["recipe"]} for f in feats],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--archetype")
    ap.add_argument("--out", default="out/gen")
    a = ap.parse_args()
    reg = A.load_registry()
    names = [a.archetype] if a.archetype else sorted(reg["archetypes"])
    rng = random.Random(a.seed)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    seen, kept, rejected = set(), [], Counter()
    for i in range(a.n):
        doc = make_doc(reg, rng, rng.choice(names), i, a.seed)
        diags, ir = A.compile_asset(doc, reg, A.ROOT)
        if ir is None:
            rejected.update(d["code"] for d in diags if d["severity"] == "error")
            continue
        renders = A.render_all(ir)
        _, d2 = A.run_tests(ir, renders)
        if d2:
            rejected.update(d["code"] for d in d2)
            continue
        base = renders["base"][0]
        if not 0.15 <= len(base["grid"]) / (ir["W"] * ir["H"]) <= 0.7:
            rejected["SILHOUETTE"] += 1
            continue
        h = json.dumps(A.frame_hashes(renders), sort_keys=True)
        if h in seen:
            rejected["DUPLICATE"] += 1
            continue
        seen.add(h)
        (out / f"{doc['asset']['id']}.json").write_text(json.dumps(doc, indent=2) + "\n")
        kept.append((ir, base))
    cols = 12
    if kept:
        cells = {(k // cols, k % cols): (ir["W"], ir["H"], b["rgba"]) for k, (ir, b) in enumerate(kept)}
        A.write_contact_sheet(out / "sheet.png", cells, (len(kept) + cols - 1) // cols, min(cols, len(kept)))
    print(f"kept {len(kept)} / {a.n}")
    for code, n in rejected.most_common():
        print(f"  rejected {n:4d}  {code}")


if __name__ == "__main__":
    main()
