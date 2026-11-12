#!/usr/bin/env python3
"""Proof of concept: one size-parametric archetype rendered at several hero heights.
Reuses the reference tool's expression evaluator and rasterizer; nothing here is part of the
supported v2.0 surface. Run: python3 spec-next/prototype/size_sweep.py [--out DIR]"""
import json, sys, argparse
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A

PROTO = json.loads((Path(__file__).parent / "humanoid_scalable.proto.json").read_text())
STYLE = A.load_registry()["styles"]["arcade_16"]
SIZES = [(14, 16), (22, 24), (30, 32), (46, 48)]          # (hero height, square cell)


def metrics(h, W, H):
    env = {"W": W, "H": H, "height": h}
    for name, expr in PROTO["metrics"]:
        env[name] = A.to_int(A.ev(expr, env))
    return env


def resolve_params(spec, env):
    out = {}
    for k, v in spec.items():
        if isinstance(v, dict) and v.get("type") == "percent":
            px = (env[v["of"]] * v["default"] + 50) // 100          # integer round-half-up
            out[k] = max(v.get("min_px", 0), px)
        else:
            out[k] = v
    return out


def build(h, W, H, with_gear=True):
    env = metrics(h, W, H)
    layers = PROTO["layers"]
    anchors = {n: [A.to_int(A.ev(e, env)) for e in xy] for n, xy in PROTO["anchors"].items()}
    ds, seq = [], 0

    def add(d):
        nonlocal seq
        d.update({"part": "root", "seq": seq, "src": ""}); seq += 1; ds.append(d)

    for sh in PROTO["body"]:
        d = A.build_shape(sh, env, None, layers.index(sh["layer"]), STYLE["ramps"])
        if d: add(d)
    if with_gear:
        for rname, r in PROTO["recipes"].items():
            renv = dict(env); renv.update(resolve_params(r["params"], env))
            for k, e in r.get("locals", []):
                renv[k] = A.to_int(A.ev(e, renv))
            for sh in r["geometry"]:
                anc = anchors[sh.get("anchor", r["anchor"])]
                d = A.build_shape(sh, renv, anc, layers.index(sh["layer"]), STYLE["ramps"])
                if d: add(d)
    return {"W": W, "H": H, "drawables": ds, "style": STYLE, "arch": {"layers": layers}}, env


def colour(v):
    # The grid value layout is an internal detail of the renderer (seq is painter
    # order, not pixel colour), so symmetry compares colour only.
    return (v[-2], v[-1])   # (ramp, tone); works for old (li, ramp, tone) and new (li, seq, part, ramp, tone)


def analyse(h, W, H):
    ir, env = build(h, W, H, with_gear=False)
    grid, _ = A.render_frame(ir, {})
    cx = env["cx"]
    asym = sum(1 for (x, y), c in grid.items()
               if grid.get((2 * cx - x, y)) is None or colour(grid[(2 * cx - x, y)]) != colour(c))
    full, _ = build(h, W, H)
    g2, clipped = A.render_frame(full, {})
    return {"height": h, "cell": [W, H], "body_asymmetric_px": asym, "clipped_px": clipped,
            "opaque_px": len(g2), "u": env["u"], "head_h": env["head_h"], "torso_w": env["torso_w"]}, full, g2


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--out", default="spec-next/prototype/out"); a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    scale, pad = 8, 2
    rows, report = [], []
    for gear in (False, True):
        cells = []
        for h, c in SIZES:
            ir, env = build(h, c, c, gear)
            grid, _ = A.render_frame(ir, {})
            cells.append((c, A.to_rgba(ir, grid)))
        rows.append(cells)
    for h, c in SIZES:
        r, _, _ = analyse(h, c, c); report.append(r)
    totw = sum(c for c, _ in rows[0]) + pad * (len(SIZES) + 1)
    toth = 48 * 2 + pad * 3
    buf = bytearray(totw * toth * 4)
    for ri, cells in enumerate(rows):
        x0 = pad
        for c, rgba in cells:
            y0 = pad + ri * (48 + pad) + (48 - c)               # bottom-aligned on the foot line
            for y in range(c):
                dst = ((y0 + y) * totw + x0) * 4
                buf[dst:dst + c * 4] = rgba[y * c * 4:(y + 1) * c * 4]
            x0 += c + pad
    (out / "size_sweep.png").write_bytes(A.png_bytes(totw, toth, bytes(buf), scale=scale, bg=(52, 56, 72)))
    (out / "size_sweep.report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    bad = [r for r in report if r["body_asymmetric_px"] or r["clipped_px"]]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
