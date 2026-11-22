#!/usr/bin/env python3
"""Compact rig -> full archetype JSON -> validated -> rendered preview. Built so small local models succeed.

  python3 tools/rig_build.py staging/mage.rig.json            # build, check, render out/rig/<name>.png
  python3 tools/rig_build.py staging/mage.rig.json --team red # preview another team colour
  python3 tools/rig_build.py staging/mage.rig.json --accept   # also add it to registry/ (runs the lint gate)

The rig is ~60 lines of JSON (see rigs/*.rig.json and docs/RIG.md). This tool writes the 1000-line archetype
(parts, layers, anchors, animation frames), builds an example asset, runs ArcForge's own validator and tests,
and prints at most 12 short diagnostics. Style: shaded_15 (ramps T=team, S=steel, K=skin)."""
import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A

LAYERS = ["back", "body", "armor", "held", "foreground", "fx"]
RAMP = {"T": "$team_ramp", "S": "$steel_ramp", "K": "$skin_ramp"}
ANIM_KEYS = ("idle", "walk", "attack", "hurt", "death")


class RigError(Exception):
    pass


def fl(v):
    return math.floor(v + 0.5)


def fill(code, flat, where):
    if code == "outline":
        return code
    r, _, t = str(code).partition(".")
    if r not in RAMP:
        raise RigError(f"{where}: fill '{code}' must start with T, S, K or be 'outline' (e.g. 'S', 'T.base', 'S.high')")
    if flat and not t:
        t = "base"
    if t and t not in ("deep", "shadow", "base", "light", "high"):
        raise RigError(f"{where}: tone '{t}' must be deep, shadow, base, light or high")
    return f"{RAMP[r]}.{t or '*'}"


def need(sp, n, where, usage):
    nums = [v for v in sp[2:] if not isinstance(v, (dict, list, str))]
    if len(sp) < n:
        raise RigError(f"{where}: needs {usage}")


def shape(sp, layer, where):
    if not isinstance(sp, list) or len(sp) < 3:
        raise RigError(f"{where}: a shape is a list like ['capsule','S',x1,y1,x2,y2,w]")
    opts = sp[-1] if isinstance(sp[-1], dict) else {}
    body = sp[:-1] if opts else sp
    k = body[0]
    lay = opts.get("layer", layer)
    if lay not in LAYERS:
        raise RigError(f"{where}: layer '{lay}' must be one of {LAYERS}")
    if k == "capsule":
        if len(body) != 7:
            raise RigError(f"{where}: capsule = ['capsule', fill, x1, y1, x2, y2, width]  (got {len(body) - 2} numbers)")
        _, f, x1, y1, x2, y2, w = body
        d = {"shape": "capsule", "layer": lay, "x1": fl(x1), "y1": fl(y1), "x2": fl(x2), "y2": fl(y2), "w": max(1, fl(w)),
             "fill": fill(f, False, where)}
        if opts.get("flat"):
            d["caps"] = "flat"
        return d
    if k == "ellipse":
        if len(body) != 6:
            raise RigError(f"{where}: ellipse = ['ellipse', fill, cx, cy, rx, ry]  (centre and radii)")
        _, f, cx, cy, rx, ry = body
        return {"shape": "ellipse", "layer": lay, "x": fl(cx - rx), "y": fl(cy - ry), "w": max(1, fl(2 * rx)),
                "h": max(1, fl(2 * ry)), "fill": fill(f, False, where)}
    if k == "rect":
        if len(body) != 6:
            raise RigError(f"{where}: rect = ['rect', fill, x, y, w, h]  (flat colour: use 'S.base' style fills)")
        _, f, x, y, w, h = body
        return {"shape": "rect", "layer": lay, "x": fl(x), "y": fl(y), "w": fl(w), "h": fl(h), "fill": fill(f, True, where)}
    if k == "poly":
        if len(body) != 3 or not isinstance(body[2], list):
            raise RigError(f"{where}: poly = ['poly', fill, [[x,y],[x,y],[x,y],...]]  (flat colour)")
        return {"shape": "poly", "layer": lay, "points": [[fl(p[0]), fl(p[1])] for p in body[2]], "fill": fill(body[1], True, where)}
    raise RigError(f"{where}: unknown shape '{k}'; use capsule, ellipse, rect or poly")


def offs(parts, dy, dx=0):
    return {p: [dx, dy] for p in parts}


def anim(name, a, parts, origin):
    pr = a.get("preset")
    body = a.get("body", [])
    for p in body + a.get("parts", []) + a.get("legs", []) + a.get("keep", []) + ([a["part"]] if "part" in a else []):
        if p not in parts:
            raise RigError(f"animation {name}: part '{p}' is not defined in parts (have: {sorted(parts)})")
    if pr == "bob":
        pts, amt = a["parts"], a.get("amount", 1)
        frames = [{"offsets": offs(pts, d * amt)} for d in (0, 0, 1, 1)]
        return {"fps": a.get("fps", 4), "loop": True, "frames": frames}
    if pr == "walk":
        L, R = a["legs"]
        lift = a.get("lift", 4)
        pat = [(L, 0), (L, 1), (None, 0), (R, 0), (R, 1), (None, 0)]
        frames = []
        for leg, dy in pat:
            o = offs(body, dy)
            if leg:
                o[leg] = [0, -lift]
            frames.append({"offsets": o})
        return {"fps": a.get("fps", 10), "loop": True, "frames": frames}
    if pr == "swing":
        deg, piv = a["degrees"], a["pivot"]
        bob = a.get("bob", [0] * len(deg))
        if len(bob) != len(deg):
            raise RigError(f"animation {name}: 'bob' needs {len(deg)} numbers like 'degrees'")
        frames = [{"offsets": offs(body, bob[i]), "rotate": {a["part"]: [deg[i], piv[0], piv[1]]}} for i in range(len(deg))]
        out = {"fps": a.get("fps", 12), "loop": False, "phases": ["windup", "strike", "recover"], "frames": frames}
        if a.get("hit_frames"):
            out["hit_frames"] = a["hit_frames"]
        return out
    if pr == "hurt":
        frames = [{"offsets": offs(a["parts"], dy, dx)} for dx, dy in ((-3, -1), (-2, 0), (0, 0))]
        return {"fps": a.get("fps", 8), "loop": False, "frames": frames}
    if pr == "death":
        # collapse: lean back about the feet and sink; parts in "keep" (e.g. the feet) rotate but do not sink
        piv = a.get("pivot", origin)
        ang, drop, keep = a.get("angle", 25), a.get("drop", 10), set(a.get("keep", []))
        frames = []
        for fr, fd in zip((0, .2, .45, .7, .9, 1), (0, .15, .4, .7, .9, 1)):
            o = {q: [0, 0 if q in keep else fl(drop * fd)] for q in a["parts"]}
            frames.append({"offsets": o, "rotate": {q: [fl(ang * fr), piv[0], piv[1]] for q in a["parts"]}})
        return {"fps": a.get("fps", 8), "loop": False, "frames": frames}
    if "frames" in a:
        return {"fps": a.get("fps", 8), "loop": a.get("loop", False), "frames": a["frames"]}
    raise RigError(f"animation {name}: give 'preset' (bob, walk, swing, hurt, death) or raw 'frames'")


def expand(rig):
    for k in ("name", "size", "parts", "animations"):
        if k not in rig:
            raise RigError(f"rig is missing '{k}'")
    W, H = rig["size"]
    origin = rig.get("origin", [W // 2, H - 3])
    parts = {"root": {"shapes": []}}
    for pid, pdef in rig["parts"].items():
        if "shapes" not in pdef or not pdef["shapes"]:
            raise RigError(f"part '{pid}' needs a non-empty 'shapes' list")
        layer = pdef.get("layer", "body")
        parts[pid] = {"shapes": [shape(sp, layer, f"part {pid} shape #{i + 1}") for i, sp in enumerate(pdef["shapes"])]}
    anchors = {"origin": {"part": "root", "at": origin}}
    for an, (pt, x, y) in rig.get("anchors", {}).items():
        anchors[an] = {"part": pt, "at": [x, y]}
    anims = {n: anim(n, a, parts, origin) for n, a in rig["animations"].items()}
    arch = {"archetype": rig["name"], "version": "1.0.0", "description": rig.get("description", f"{rig['name']} (built from a rig)"),
            "view": rig.get("view", "front"), "canvas": {"default": [W, H], "supported": [[W, H]]},
            "params": {"team_ramp": {"type": "ramp", "default": "blue", "allow": ["blue", "red", "green", "gold"]},
                       "skin_ramp": {"type": "ramp", "default": "skin", "allow": ["skin"]},
                       "steel_ramp": {"type": "ramp", "default": "steel", "allow": ["steel"]}},
            "layers": LAYERS, "parts": parts, "anchors": anchors, "animations": anims}
    return arch


def example_asset(arch, style, team):
    W, H = arch["canvas"]["default"]
    an = {}
    for t in arch["animations"]:
        k = next((k for k in ANIM_KEYS if t == k or t.startswith(k + "_")), None)
        if k and k not in an:
            an[k] = {"template": t}
    return {"arcforge": "2.0", "asset": {"id": f"rig.{arch['archetype']}", "type": "character", "version": "1.0.0"},
            "target": {"profile": "arcade1-draft", "canvas": [W, H]}, "style_pack": style,
            "archetype": {"name": arch["archetype"], "params": {"team_ramp": team}}, "features": [], "animations": an,
            "tests": [{"assert": "dimensions", "expected": [W, H]}, {"assert": "palette_size", "max": 15},
                      {"assert": "bbox_margin", "min": 0}]}


def show(diags, limit=12):
    for d in diags[:limit]:
        extra = ""
        for k in ("expected", "suggestions", "fix"):
            if d.get(k):
                extra += f" {k}={json.dumps(d[k])[:120]}"
        print(f"  {d['severity'].upper()} {d['code']} {d['path']}: {d['message'][:150]}{extra}")
    if len(diags) > limit:
        print(f"  ... {len(diags) - limit} more")


def sheet(renders, W, H, path, scale=3):
    rows = [("base", renders["base"])] + [(n, f) for n, f in renders.items() if n != "base"]
    cols = max(len(f) for _, f in rows)
    buf = bytearray(W * cols * H * len(rows) * 4)
    pw = W * cols
    for r, (_, fs) in enumerate(rows):
        for c, f in enumerate(fs):
            for y in range(H):
                dst = ((r * H + y) * pw + c * W) * 4
                buf[dst:dst + W * 4] = f["rgba"][y * W * 4:(y + 1) * W * 4]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(A.png_bytes(pw, H * len(rows), bytes(buf), scale=scale, bg=(52, 56, 72)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rig")
    ap.add_argument("--team", default="blue")
    ap.add_argument("--style", default="shaded_15")
    ap.add_argument("--accept", action="store_true")
    a = ap.parse_args()
    try:
        rig = json.loads(Path(a.rig).read_text())
    except Exception as e:
        sys.exit(f"RIG JSON error: {e}")
    try:
        arch = expand(rig)
    except (RigError, KeyError, TypeError, ValueError) as e:
        sys.exit(f"RIG ERROR: {e}")
    name = arch["archetype"]
    st = ROOT / "staging"
    st.mkdir(exist_ok=True)
    apath = st / f"{name}.archetype.json"
    apath.write_text(json.dumps(arch, indent=2) + "\n")
    sd = A.schema_diags(arch, "archetype.schema.json")
    if sd:
        print("ARCHETYPE SCHEMA ERRORS (the rig expanded to something invalid):")
        show(sd)
        sys.exit(1)
    reg = A.load_registry()
    reg["archetypes"][name] = arch
    asset = example_asset(arch, a.style, a.team)
    diags, ir = A.compile_asset(asset, reg, ROOT)
    if ir is None:
        print("COMPILE ERRORS:")
        show([d for d in diags if d["severity"] == "error"] or diags)
        sys.exit(1)
    renders = A.render_all(ir)
    results, d2 = A.run_tests(ir, renders)
    errs = [d for d in diags + d2 if d["severity"] == "error"]
    warns = [d for d in diags + d2 if d["severity"] != "error"]
    out = ROOT / "out" / "rig" / f"{name}.png"
    sheet(renders, ir["W"], ir["H"], out)
    frames = sum(len(f) for f in renders.values())
    if errs:
        print(f"FAILED ({len(errs)} errors); preview still written: {out}")
        show(errs)
        sys.exit(1)
    print(f"OK {name}: {ir['W']}x{ir['H']}, {frames} frames, tests {sum(r['status'] == 'PASS' for r in results)}/{len(results)} pass")
    if warns:
        print("warnings:")
        show(warns, 5)
    print(f"PREVIEW: {out}   (rows: base, then each animation; open it to judge the look)")
    if a.accept:
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "lib_add.py"), str(apath)], cwd=ROOT)
        sys.exit(r.returncode)


if __name__ == "__main__":
    main()
