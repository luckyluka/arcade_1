#!/usr/bin/env python3
"""ArcForge reference implementation (spike).

Purpose: make the spec executable. This is the oracle that the future C
compiler (ROADMAP M4) must match byte-for-byte (see tests/golden).
Dependencies: Python 3.9+, jsonschema.

Commands:
  validate ASSET            schema + registry validation, structured diagnostics (JSON)
  render   ASSET [--out D]  deterministic render: frames, spritesheet, atlas, preview
  test     ASSET            validate + render + asset assertions
  golden   ASSET [--update] compare (or write) per-frame pixel hashes
  lint                      validate the registry itself and render every recipe example
  sweep   ARCHETYPE         size-sweep an archetype file: sizes x recipes x parameter
                            extremes; contact sheet + JSON table in out/sweep/
  list     [archetypes|recipes|styles]
  describe KIND NAME        print one registry entry (JSON)
"""
import argparse
import ast
import difflib
import hashlib
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REG = ROOT / "registry"
SCHEMA_DIR = ROOT / "schema"
TONES = ("shadow", "base", "light")


def load_json(p):
    return json.loads(Path(p).read_text())


def load_registry():
    reg = {"archetypes": {}, "recipes": {}, "styles": {}}
    for p in sorted(REG.glob("archetypes/*.archetype.json")):
        d = load_json(p)
        reg["archetypes"][d["archetype"]] = d
    for p in sorted(REG.glob("recipes/**/*.recipe.json")):
        d = load_json(p)
        reg["recipes"][d["recipe"]] = d
    for p in sorted(REG.glob("styles/*.style.json")):
        d = load_json(p)
        reg["styles"][d["style_pack"]] = d
    return reg


# --------------------------------------------------------------------------
# Diagnostics
# --------------------------------------------------------------------------
def D(code, path, message, observed=None, expected=None, suggestions=None, fix=None, severity="error"):
    d = {"code": code, "severity": severity, "path": path, "message": message}
    if observed is not None:
        d["observed"] = observed
    if expected is not None:
        d["expected"] = expected
    if suggestions:
        d["suggestions"] = suggestions
    if fix:
        d["fix"] = fix
    return d


def close(name, options, n=3, cutoff=0.6):
    return difflib.get_close_matches(str(name), list(options), n=n, cutoff=cutoff)


def ptr(*parts):
    return "".join("/" + str(p).replace("~", "~0").replace("/", "~1") for p in parts)


# --------------------------------------------------------------------------
# Expression language: total, side-effect free, no loops.
#   numbers, 'strings', true/false, $name, + - * /, comparisons, && || !,
#   floor ceil round min max abs clamp
# --------------------------------------------------------------------------
class ExprError(Exception):
    pass


FUNCS = {
    "floor": math.floor, "ceil": math.ceil, "round": lambda v: math.floor(v + 0.5),
    "min": min, "max": max, "abs": abs, "clamp": lambda v, a, b: max(a, min(b, v)),
    # 2.1: parity helpers, floor semantics for negatives (odd/even = smallest
    # value of that parity that is >= x), half = floor(x/2).
    "odd": lambda v: _parity(math.floor(v), 1),
    "even": lambda v: _parity(math.floor(v), 0),
    "half": lambda v: math.floor(v / 2),
}


def _parity(v, want):
    return v if v % 2 == want else v + 1
CONSTS = {"true": True, "false": False}


def _prep(src):
    src = re.sub(r"\$([A-Za-z_][A-Za-z0-9_]*)", r"__\1", src)
    src = src.replace("&&", " and ").replace("||", " or ")
    return re.sub(r"!(?!=)", " not ", src)


def ev(expr, env):
    if isinstance(expr, bool) or isinstance(expr, (int, float)):
        return expr
    try:
        tree = ast.parse(_prep(expr), mode="eval")
    except SyntaxError:
        raise ExprError(f"syntax error in expression {expr!r}")
    try:
        return _ev(tree.body, env)
    except TypeError:
        raise ExprError(f"type error in expression {expr!r}")


def _ev(n, env):
    if isinstance(n, ast.Constant) and isinstance(n.value, (int, float, str, bool)):
        return n.value
    if isinstance(n, ast.Name):
        if n.id.startswith("__"):
            k = n.id[2:]
            if k not in env:
                raise ExprError(f"unknown variable ${k}")
            return env[k]
        if n.id in CONSTS:
            return CONSTS[n.id]
        raise ExprError(f"unknown identifier {n.id!r}")
    if isinstance(n, ast.UnaryOp):
        v = _ev(n.operand, env)
        if isinstance(n.op, ast.USub):
            return -v
        if isinstance(n.op, ast.UAdd):
            return +v
        if isinstance(n.op, ast.Not):
            return not v
    if isinstance(n, ast.BinOp):
        a, b = _ev(n.left, env), _ev(n.right, env)
        if isinstance(n.op, ast.Add):
            return a + b
        if isinstance(n.op, ast.Sub):
            return a - b
        if isinstance(n.op, ast.Mult):
            return a * b
        if isinstance(n.op, ast.Div):
            if b == 0:
                raise ExprError("division by zero")
            return a / b
        if isinstance(n.op, ast.FloorDiv):
            if b == 0:
                raise ExprError("floor division by zero")
            return a // b
        if isinstance(n.op, ast.Mod):
            if b == 0:
                raise ExprError("modulo by zero")
            return a % b
    if isinstance(n, ast.BoolOp):
        vals = [_ev(v, env) for v in n.values]
        return all(vals) if isinstance(n.op, ast.And) else any(vals)
    if isinstance(n, ast.Compare):
        left = _ev(n.left, env)
        for op, comp in zip(n.ops, n.comparators):
            right = _ev(comp, env)
            ok = {ast.Eq: left == right, ast.NotEq: left != right, ast.Lt: left < right,
                  ast.LtE: left <= right, ast.Gt: left > right, ast.GtE: left >= right}.get(type(op))
            if ok is None:
                raise ExprError("unsupported comparison")
            if not ok:
                return False
            left = right
        return True
    if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id in FUNCS and not n.keywords:
        args = [_ev(a, env) for a in n.args]
        try:
            return FUNCS[n.func.id](*args)
        except TypeError:
            raise ExprError(f"wrong number or type of arguments to {n.func.id}()")
    raise ExprError("unsupported expression")


def to_int(v):
    if isinstance(v, bool):
        raise ExprError("boolean used as a number")
    return math.floor(v)  # geometry is implicitly floored


# --------------------------------------------------------------------------
# Fixed-point geometry (1/64 px). All raster tests are integer-only so a C
# port is bit-identical. Trig is an integer-degree table rounded to 1e-6.
# --------------------------------------------------------------------------
COS = {d: int(round(math.cos(math.radians(d)) * 1e6)) for d in range(-180, 181)}
SIN = {d: int(round(math.sin(math.radians(d)) * 1e6)) for d in range(-180, 181)}


def rot_fixed(x, y, deg):
    c, s = COS[deg], SIN[deg]
    return (((x * c - y * s) * 64 + 500000) // 1000000, ((x * s + y * c) * 64 + 500000) // 1000000)


def point_in_poly(pts, cx, cy):
    inside = False
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        if (y1 > cy) != (y2 > cy):
            dy = y2 - y1
            lhs = (cx - x1) * dy
            rhs = (cy - y1) * (x2 - x1)
            if (dy > 0 and lhs < rhs) or (dy < 0 and lhs > rhs):
                inside = not inside
    return inside


# --------------------------------------------------------------------------
# .px fragments
# --------------------------------------------------------------------------
class PxError(Exception):
    def __init__(self, line, msg):
        super().__init__(msg)
        self.line = line
        self.msg = msg


def parse_px(text):
    out = {"name": None, "size": None, "anchors": {}, "legend": {}, "rows": []}
    block = None
    seen_header = False
    for ln, raw in enumerate(text.splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if not raw.startswith((" ", "\t")):
            block = None
            parts = raw.split()
            kw = parts[0]
            if kw == "px":
                if parts[1:] != ["1"]:
                    raise PxError(ln, "unsupported px version (expected 'px 1')")
                seen_header = True
            elif not seen_header:
                raise PxError(ln, "first statement must be 'px 1'")
            elif kw == "name" and len(parts) == 2:
                out["name"] = parts[1]
            elif kw == "size" and len(parts) == 3 and parts[1].isdigit() and parts[2].isdigit():
                out["size"] = (int(parts[1]), int(parts[2]))
            elif kw == "anchor" and len(parts) == 4 and parts[2].lstrip("-").isdigit() and parts[3].lstrip("-").isdigit():
                out["anchors"][parts[1]] = (int(parts[2]), int(parts[3]))
            elif kw in ("legend", "rows") and len(parts) == 1:
                block = kw
            else:
                raise PxError(ln, f"unknown or malformed statement {raw.strip()!r}")
        else:
            body = raw.strip()
            if block == "legend":
                p = body.split()
                if len(p) != 2 or len(p[0]) != 1:
                    raise PxError(ln, "legend line must be '<char> <value>'")
                out["legend"][p[0]] = p[1]
            elif block == "rows":
                out["rows"].append((ln, body))
            else:
                raise PxError(ln, "indented line outside a block")
    if not seen_header:
        raise PxError(1, "missing 'px 1' header")
    if out["size"] is None:
        raise PxError(1, "missing 'size W H'")
    w, h = out["size"]
    if len(out["rows"]) != h:
        raise PxError(out["rows"][0][0] if out["rows"] else 1, f"expected {h} rows, found {len(out['rows'])}")
    for ln, row in out["rows"]:
        if len(row) != w:
            raise PxError(ln, f"row has {len(row)} pixels, expected {w}")
        for ch in row:
            if ch not in out["legend"]:
                raise PxError(ln, f"pixel {ch!r} not in legend")
    if "attach" not in out["anchors"]:
        raise PxError(1, "missing 'anchor attach X Y'")
    ax, ay = out["anchors"]["attach"]
    if not (0 <= ax < w and 0 <= ay < h):
        raise PxError(1, "anchor attach lies outside the fragment")
    return out


# --------------------------------------------------------------------------
# Validation + compilation to IR
# --------------------------------------------------------------------------
def schema_validator(name):
    from jsonschema import Draft202012Validator
    return Draft202012Validator(load_json(SCHEMA_DIR / name))


def schema_diags(doc, schema_name, base_path=""):
    diags = []
    for e in sorted(schema_validator(schema_name).iter_errors(doc), key=lambda e: list(map(str, e.absolute_path))):
        p = base_path + ptr(*e.absolute_path)
        if e.validator == "additionalProperties" and "were unexpected" in e.message or "was unexpected" in e.message:
            for extra in re.findall(r"'([^']+)'", e.message):
                diags.append(D("E_UNKNOWN_FIELD", p + ptr(extra),
                               f"unknown field '{extra}'. Free text is metadata, never executable visual intent; "
                               "use archetype/features/params or a custom px_fragment",
                               suggestions=["features", "custom", "metadata"]))
        else:
            diags.append(D("E_SCHEMA", p or "/", e.message))
    return diags


def check_params(specs, given, path, style, diags, label, env=None):
    out = {}
    for k in given:
        if k not in specs:
            diags.append(D("E_UNKNOWN_PARAM", f"{path}/{k}", f"{label} has no parameter '{k}'",
                           expected=sorted(specs), suggestions=close(k, specs)))
    for k, spec in specs.items():
        v = given.get(k, spec["default"])
        t = spec["type"]
        pp = f"{path}/{k}"
        if t == "percent":
            # px = max(min_px, (dim * pct + 50) // 100), integer only. `of` must
            # name an integer dimension of the archetype (size parameter or
            # metric); without it the parameter is unavailable on this
            # archetype: supplying it is an error, omitting it leaves it
            # unbound so fixed-art archetypes keep compiling.
            dim = env.get(spec.get("of")) if isinstance(env, dict) else None
            available = isinstance(dim, int) and not isinstance(dim, bool)
            if not available:
                if k in given:
                    diags.append(D("E_UNKNOWN_SIZE_PARAM", pp,
                                   f"parameter '{k}' is a percent of '{spec.get('of')}', which this archetype does not provide",
                                   observed=given[k], expected=f"archetype dimension '{spec.get('of')}'",
                                   suggestions=(close(spec.get("of"), sorted(env)) if env else None)))
                continue
            if isinstance(v, bool) or not isinstance(v, int):
                diags.append(D("E_PARAM_TYPE", pp, f"'{k}' must be an integer percent", observed=v, expected="integer"))
                continue
            lo, hi = spec.get("min", 0), spec.get("max", 100)
            if v < lo or v > hi:
                c = max(lo, min(hi, v))
                diags.append(D("E_PARAM_RANGE", pp, f"'{k}' out of range", observed=v,
                               expected=f"{lo}..{hi}",
                               fix=[{"op": "replace", "path": pp, "value": c}]))
                continue
            out[k] = max(spec.get("min_px", 0), (dim * v + 50) // 100)
            continue
        if t == "int":
            if isinstance(v, bool) or not isinstance(v, int):
                diags.append(D("E_PARAM_TYPE", pp, f"'{k}' must be an integer", observed=v, expected="integer"))
                continue
            if v < spec["min"] or v > spec["max"]:
                c = max(spec["min"], min(spec["max"], v))
                diags.append(D("E_PARAM_RANGE", pp, f"'{k}' out of range", observed=v,
                               expected=f"{spec['min']}..{spec['max']}",
                               fix=[{"op": "replace", "path": pp, "value": c}]))
                continue
        elif t == "bool":
            if not isinstance(v, bool):
                diags.append(D("E_PARAM_TYPE", pp, f"'{k}' must be true or false", observed=v, expected="boolean"))
                continue
        elif t == "enum":
            if v not in spec["values"]:
                diags.append(D("E_PARAM_ENUM", pp, f"'{k}' is not an allowed value", observed=v,
                               expected=spec["values"], suggestions=close(v, spec["values"])))
                continue
        elif t == "ramp":
            allowed = spec.get("allow") or [r for r in style["ramps"] if r != style["outline"]["ramp"]]
            if v not in allowed or v not in style["ramps"]:
                diags.append(D("E_PARAM_ENUM", pp, f"'{k}' is not an available colour ramp", observed=v,
                               expected=allowed, suggestions=close(v, allowed)))
                continue
        out[k] = v
    return out


def resolve_fill(fill, env, ramps):
    if fill == "outline":
        return ("outline", "base")
    m = re.fullmatch(r"(\$?[A-Za-z_][A-Za-z0-9_]*)\.(shadow|base|light)", fill)
    if not m:
        raise ExprError(f"bad fill {fill!r}")
    ramp = m.group(1)
    if ramp.startswith("$"):
        if ramp[1:] not in env:
            raise ExprError(f"unknown variable {ramp}")
        ramp = env[ramp[1:]]
    if ramp not in ramps or m.group(2) not in ramps[ramp]:
        raise ExprError(f"ramp/tone '{ramp}.{m.group(2)}' does not exist in the style pack")
    return (ramp, m.group(2))


def build_shape(sh, env, anchor_at, layer_idx, ramps):
    """Evaluate one shape; returns a drawable dict (without part/seq) or None."""
    if "when" in sh and not ev(sh["when"], env):
        return None
    fill = resolve_fill(sh["fill"], env, ramps)
    ax, ay = anchor_at if anchor_at else (0, 0)
    deg = to_int(ev(sh["rotate"], env)) if "rotate" in sh else 0
    if not -180 <= deg <= 180:
        raise ExprError("rotate must be within -180..180 degrees")
    kind = sh["shape"]
    if kind == "ellipse":
        if deg:
            raise ExprError("ellipse cannot be rotated")
        x, y, w, h = (to_int(ev(sh[k], env)) for k in "xywh")
        if w <= 0 or h <= 0:
            return None
        return {"kind": "ellipse", "box": (ax + x, ay + y, w, h), "fill": fill, "li": layer_idx}
    if kind == "rect":
        x, y, w, h = (to_int(ev(sh[k], env)) for k in "xywh")
        if w <= 0 or h <= 0:
            return None
        pts = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    else:
        pts = [(to_int(ev(px, env)), to_int(ev(py, env))) for px, py in sh["points"]]
    fixed = []
    for px, py in pts:
        fx, fy = rot_fixed(px, py, deg)
        fixed.append((ax * 64 + fx, ay * 64 + fy))
    return {"kind": "poly", "pts": fixed, "fill": fill, "li": layer_idx}


def compile_asset(asset, reg, base_dir):
    """Validate and compile. Returns (diagnostics, ir or None)."""
    diags = schema_diags(asset, "asset.schema.json")
    if diags:
        return diags, None

    style = reg["styles"].get(asset["style_pack"])
    if not style:
        diags.append(D("E_UNKNOWN_STYLE_PACK", "/style_pack", f"no style pack '{asset['style_pack']}'",
                       expected=sorted(reg["styles"]), suggestions=close(asset["style_pack"], reg["styles"])))
    arch = reg["archetypes"].get(asset["archetype"]["name"])
    if not arch:
        diags.append(D("E_UNKNOWN_ARCHETYPE", "/archetype/name", f"no archetype '{asset['archetype']['name']}'",
                       expected=sorted(reg["archetypes"]), suggestions=close(asset["archetype"]["name"], reg["archetypes"])))
    if diags:
        return diags, None

    # ---- cell: fixed [w,h] or 2.1 auto {mode,...} ------------------------
    canvas = asset["target"]["canvas"]
    if isinstance(canvas, dict):
        # Auto: the real cell is derived after rendering on an unbounded plane.
        # Metrics/anchors see the provisional cell = archetype canvas.default.
        auto = {"margin": canvas.get("margin", 1),
                "multiple_of": canvas.get("multiple_of", 1),
                "align": canvas.get("align", "pivot_bottom_center")}
        W, H = arch["canvas"]["default"]
    else:
        auto = None
        W, H = canvas
    # ---- size parameters (2.1) ------------------------------------------
    size_specs = arch.get("size") or {}
    size_env = {}
    given_size = asset["archetype"].get("size", {})
    for k in given_size:
        if k not in size_specs:
            diags.append(D("E_UNKNOWN_SIZE_PARAM", ptr("archetype", "size", k),
                           f"archetype '{arch['archetype']}' has no size parameter '{k}'",
                           observed=k, expected=sorted(size_specs), suggestions=close(k, size_specs),
                           fix=[{"op": "remove", "path": ptr("archetype", "size", k)}]))
    for k, spec in size_specs.items():
        v = given_size.get(k, spec["default"])
        if v < spec["min"] or v > spec["max"]:
            c = max(spec["min"], min(spec["max"], v))
            diags.append(D("E_SIZE_RANGE", ptr("archetype", "size", k),
                           f"size parameter '{k}' out of range", observed=v,
                           expected=f"{spec['min']}..{spec['max']}",
                           fix=[{"op": "replace", "path": ptr("archetype", "size", k), "value": c}]))
            v = c
        size_env[k] = v
    # A sized archetype expresses its cell contract through min_cell(size);
    # the fixed `supported` list applies only to unsized, fixed-canvas assets.
    if auto is None and not size_specs and [W, H] not in arch["canvas"]["supported"]:
        diags.append(D("E_CANVAS_UNSUPPORTED", "/target/canvas", "archetype does not support this canvas size",
                       observed=[W, H], expected=arch["canvas"]["supported"],
                       fix=[{"op": "replace", "path": "/target/canvas", "value": arch["canvas"]["default"]}]))

    # ---- expression environment: cell, size, class, proportions ----------
    menv = {"W": W, "H": H}
    menv.update(size_env)
    classes = arch.get("classes", [])
    cls_name, cls_idx = "default", 0
    if size_specs and classes:
        primary = next(iter(size_specs))          # document order = primary size parameter
        sv = size_env[primary]
        cands = [c for c in classes if sv <= c["max"]]
        pick = min(cands, key=lambda c: c["max"]) if cands else max(classes, key=lambda c: c["max"])
        cls_name, cls_idx = pick["name"], classes.index(pick)
    menv["class"], menv["class_idx"] = cls_name, cls_idx
    row = arch.get("proportions", {}).get(cls_name)
    if isinstance(row, dict):
        menv.update(row)

    params = check_params(arch["params"], asset["archetype"].get("params", {}), "/archetype/params", style, diags,
                          f"archetype '{arch['archetype']}'", env=menv)
    env = dict(menv)
    env.update(params)
    # ---- metrics: ordered list, every value floored to an integer --------
    for mi, mdef in enumerate(arch.get("metrics", [])):
        mname, mexpr = mdef[0], mdef[1]
        try:
            env[mname] = to_int(ev(mexpr, env))
        except (ExprError, ValueError) as e:
            diags.append(D("E_ARCHETYPE_EXPR", ptr("archetype", "metrics", mi),
                           f"metric '{mname}' failed to evaluate: {e}"))
            env[mname] = 0
    # ---- min_cell(size): evaluated whenever declared; a *fixed* canvas must
    # contain the body at this size (auto cells are built to satisfy it later)
    mc = None
    if size_specs and arch.get("min_cell"):
        try:
            mc = [to_int(ev(v, env)) for v in arch["min_cell"]]
        except (ExprError, ValueError) as e:
            diags.append(D("E_ARCHETYPE_EXPR", ptr("archetype", "min_cell"),
                           f"min_cell failed to evaluate: {e}"))
        else:
            if auto is None and (W < mc[0] or H < mc[1]):
                fix = ([{"op": "replace", "path": "/target/canvas", "value": mc}]
                       if all(1 <= x <= 256 for x in mc) else None)
                diags.append(D("E_CELL_TOO_SMALL", "/target/canvas",
                               f"canvas {W}x{H} is smaller than the archetype minimum {mc[0]}x{mc[1]} at this size",
                               observed=[W, H], expected=mc, fix=fix))
    layers = arch["layers"]
    ramps = style["ramps"]
    drawables = []
    seq = 0
    anchors = {}
    for aname, adef in arch["anchors"].items():
        try:
            at = [to_int(ev(v, env)) for v in adef["at"]]
        except (ExprError, ValueError) as e:
            diags.append(D("E_ARCHETYPE_EXPR", ptr("archetype", "anchors", aname),
                           f"anchor '{aname}' failed to evaluate: {e}"))
            at = [0, 0]
        anchors[aname] = {"part": adef["part"], "at": at}
    parts = list(arch["parts"])

    def add(d, part, src):
        nonlocal seq
        d.update({"part": part, "seq": seq, "src": src})
        seq += 1
        drawables.append(d)

    # archetype body
    try:
        for part, pdef in arch["parts"].items():
            for sh in pdef["shapes"]:
                d = build_shape(sh, env, None, layers.index(sh["layer"]), ramps)
                if d:
                    add(d, part, f"archetype:{part}")
    except (ExprError, ValueError) as e:
        diags.append(D("E_ARCHETYPE_EXPR", "/archetype", f"archetype geometry failed: {e}"))

    # features
    used_slots = {}
    feature_names = []
    hitboxes = []
    for i, f in enumerate(asset.get("features", [])):
        fp = ptr("features", i)
        name, given = (f, {}) if isinstance(f, str) else (f["recipe"], f.get("params", {}))
        rp = fp if isinstance(f, str) else fp + "/recipe"
        rec = reg["recipes"].get(name)
        if not rec:
            sug = close(name, reg["recipes"])
            diags.append(D("E_UNKNOWN_RECIPE", rp, f"NO RECIPE EXISTS named '{name}'",
                           suggestions=sug or ["use a custom px_fragment"],
                           expected=sorted(reg["recipes"]),
                           fix=([{"op": "replace", "path": rp, "value": sug[0]}] if len(sug) == 1 or (sug and difflib.SequenceMatcher(None, name, sug[0]).ratio() > 0.85) else None)))
            continue
        if arch["archetype"] not in rec["applies_to"]:
            diags.append(D("E_RECIPE_NOT_APPLICABLE", rp, f"recipe '{name}' does not apply to archetype '{arch['archetype']}'",
                           expected=rec["applies_to"]))
            continue
        missing = [a for a in rec["requires_anchors"] if a not in anchors]
        if missing:
            diags.append(D("E_MISSING_ANCHOR", rp, f"archetype lacks anchors required by '{name}'", expected=missing))
            continue
        slot = rec["slot"]
        if slot in used_slots and not rec.get("stackable"):
            diags.append(D("E_SLOT_CONFLICT", rp, f"slot '{slot}' already used by feature #{used_slots[slot][0]} ('{used_slots[slot][1]}')",
                           observed=name, expected="one recipe per slot",
                           fix=[{"op": "remove", "path": fp}]))
            continue
        used_slots[slot] = (i, name)
        # a recipe may declare the primary size-parameter range it supports
        if rec.get("sizes") and size_specs:
            primary = next(iter(size_specs))
            sv = size_env[primary]
            lo, hi = rec["sizes"]["min"], rec["sizes"]["max"]
            if sv < lo or sv > hi:
                diags.append(D("E_RECIPE_SIZE_RANGE", fp,
                               f"recipe '{name}' supports {primary} {lo}..{hi}; this asset uses {sv}",
                               observed=sv, expected=f"{lo}..{hi}",
                               fix=[{"op": "replace", "path": ptr("archetype", "size", primary),
                                     "value": max(lo, min(hi, sv))}]))
                continue
        feature_names.append(name)
        pdiags_before = len(diags)
        params = check_params(rec["parameters"], given, fp + "/params", style, diags, f"recipe '{name}'", env=env)
        if len(diags) > pdiags_before:
            continue
        renv = dict(env)
        clash = set(params) & set(env)
        if clash:
            diags.append(D("E_PARAM_SHADOW", fp, f"recipe parameters shadow archetype names: {sorted(clash)}"))
            continue
        renv.update(params)
        ldiags_before = len(diags)
        for lname, lexpr in rec.get("locals", []):
            try:
                renv[lname] = to_int(ev(lexpr, renv))
            except (ExprError, ValueError) as e:
                diags.append(D("E_RECIPE_EXPR", fp, f"recipe '{name}' local '{lname}' failed to evaluate: {e}"))
        if len(diags) > ldiags_before:
            continue
        try:
            for sh in rec["geometry"]:
                if sh["layer"] not in layers:
                    raise ExprError(f"unknown layer '{sh['layer']}'")
                if sh["anchor"] not in anchors:
                    raise ExprError(f"unknown anchor '{sh['anchor']}'")
                a = anchors[sh["anchor"]]
                d = build_shape(sh, renv, a["at"], layers.index(sh["layer"]), ramps)
                if d:
                    add(d, a["part"], f"feature:{name}")
            for hb in rec.get("hitboxes", []):
                a = anchors[hb["anchor"]]
                x, y, w, h = (to_int(ev(v, renv)) for v in hb["rect"])
                hitboxes.append({"id": hb["id"], "rect": [a["at"][0] + x, a["at"][1] + y, w, h],
                                 "part": a["part"], "active_in": hb["active_in"]})
        except ExprError as e:
            diags.append(D("E_RECIPE_EXPR", fp, f"recipe '{name}' failed to evaluate: {e}"))

    # custom px fragments
    for i, c in enumerate(asset.get("custom", [])):
        cp = ptr("custom", i)
        if c["anchor"] not in anchors:
            diags.append(D("E_MISSING_ANCHOR", cp + "/anchor", f"archetype has no anchor '{c['anchor']}'",
                           expected=sorted(anchors), suggestions=close(c["anchor"], anchors)))
            continue
        if c["layer"] not in layers:
            diags.append(D("E_UNKNOWN_LAYER", cp + "/layer", f"archetype has no layer '{c['layer']}'",
                           expected=layers, suggestions=close(c["layer"], layers)))
            continue
        fpath = Path(base_dir) / c["file"]
        if not fpath.exists():
            diags.append(D("E_PX_NOT_FOUND", cp + "/file", f"fragment file not found: {c['file']}"))
            continue
        try:
            px = parse_px(fpath.read_text())
        except PxError as e:
            diags.append(D("E_PX_PARSE", cp + "/file", f"{c['file']} line {e.line}: {e.msg}"))
            continue
        legend = {}
        bad = False
        for ch, val in px["legend"].items():
            if val == "transparent":
                continue
            try:
                legend[ch] = resolve_fill(val, {}, ramps)
            except ExprError as e:
                diags.append(D("E_PX_LEGEND", cp + "/file", f"{c['file']}: legend '{ch}': {e}"))
                bad = True
        if bad:
            continue
        a = anchors[c["anchor"]]
        pxs = []
        fax, fay = px["anchors"]["attach"]
        for r, (_, row) in enumerate(px["rows"]):
            for col, ch in enumerate(row):
                if ch in legend:
                    pxs.append((col - fax, r - fay, legend[ch]))
        add({"kind": "px", "at": tuple(a["at"]), "px": pxs, "li": layers.index(c["layer"])}, a["part"], f"custom:{c['id']}")

    # animations
    anims = {}
    for name, a in asset.get("animations", {}).items():
        tpl = arch["animations"].get(a["template"])
        if not tpl:
            diags.append(D("E_UNKNOWN_ANIMATION_TEMPLATE", ptr("animations", name, "template"),
                           f"archetype has no animation template '{a['template']}'",
                           expected=sorted(arch["animations"]), suggestions=close(a["template"], arch["animations"])))
            continue
        anims[name] = tpl

    if any(d["severity"] == "error" for d in diags):
        return diags, None
    return diags, {
        "asset": asset, "arch": arch, "style": style, "W": W, "H": H, "drawables": drawables,
        "anchors": anchors, "parts": parts, "features": feature_names, "animations": anims, "hitboxes": hitboxes,
        "auto": auto, "min_cell": mc, "clip_policy": asset["target"].get("clip_policy", "error"),
        "env": env,
    }


# --------------------------------------------------------------------------
# Rasterizer
# --------------------------------------------------------------------------
def part_offset(offsets, part):
    rx, ry = offsets.get("root", (0, 0))
    px, py = offsets.get(part, (0, 0)) if part != "root" else (0, 0)
    return rx + px, ry + py


def render_frame(ir, offsets, clip=True):
    """Rasterize one frame. clip=False renders on an unbounded plane (auto
    canvas pass 1): nothing is dropped, `clipped` stays 0."""
    W, H = ir["W"], ir["H"]
    grid = {}   # (x, y) -> (li, seq, part, ramp, tone): seq orders pixels within
                # a layer and part identity feeds the inner-outline pass;
                # outer-outline pixels carry part None
    clipped = 0

    def put(x, y, li, seq, part, fill):
        nonlocal clipped
        if clip and not (0 <= x < W and 0 <= y < H):
            clipped += 1
        else:
            grid[(x, y)] = (li, seq, part, fill[0], fill[1])

    for d in sorted(ir["drawables"], key=lambda d: (d["li"], d["seq"])):
        ox, oy = part_offset(offsets, d["part"])
        if d["kind"] == "px":
            for dx, dy, fill in d["px"]:
                put(d["at"][0] + ox + dx, d["at"][1] + oy + dy, d["li"], d["seq"], d["part"], fill)
        elif d["kind"] == "ellipse":
            x, y, w, h = d["box"]
            x, y = x + ox, y + oy
            for py in range(y, y + h):
                for px in range(x, x + w):
                    dx = (2 * px + 1) - (2 * x + w)
                    dy = (2 * py + 1) - (2 * y + h)
                    if dx * dx * h * h + dy * dy * w * w <= w * w * h * h:
                        put(px, py, d["li"], d["seq"], d["part"], d["fill"])
        else:
            pts = [(fx + ox * 64, fy + oy * 64) for fx, fy in d["pts"]]
            x0 = min(p[0] for p in pts) // 64
            x1 = max(p[0] for p in pts) // 64 + 1
            y0 = min(p[1] for p in pts) // 64
            y1 = max(p[1] for p in pts) // 64 + 1
            for py in range(y0, y1 + 1):
                for px in range(x0, x1 + 1):
                    if point_in_poly(pts, px * 64 + 32, py * 64 + 32):
                        put(px, py, d["li"], d["seq"], d["part"], d["fill"])

    # outline post-pass
    style, layers = ir["style"], ir["arch"]["layers"]
    ol = style["outline"]
    nbrs = ((1, 0), (-1, 0), (0, 1), (0, -1))
    if ol["mode"] == "outer":
        sep = {layers.index(n) for n in ol.get("separate_layers", []) if n in layers}
        out = dict(grid)
        for (x, y), (li, seq, part, _, _) in grid.items():
            if li in sep:
                for dx, dy in nbrs:
                    n = grid.get((x + dx, y + dy))
                    if n and n[0] not in sep:
                        out[(x, y)] = (li, seq, part, ol["ramp"], "base")
                        break
        for (x, y) in grid:
            for dx, dy in nbrs:
                p = (x + dx, y + dy)
                if p not in grid and (not clip or (0 <= p[0] < W and 0 <= p[1] < H)):
                    out[p] = (len(layers), 0, None, ol["ramp"], "base")
        grid = out
    # Per-part inner outline (style pack outline.inner): a pixel of a listed
    # part becomes outline where it touches a pixel of ANOTHER part drawn
    # before it ((li, seq) is the painter's order). One side only -> always
    # exactly 1px, landing on the part that was drawn over the other.
    inner = ol.get("inner", {"mode": "none"})
    if inner.get("mode") == "part":
        want = inner.get("parts")
        listed = None if want is None else set(want)
        out = dict(grid)
        for (x, y), (li, seq, part, _, _) in grid.items():
            if part is None or (listed is not None and part not in listed):
                continue
            for dx, dy in nbrs:
                n = grid.get((x + dx, y + dy))
                if n and n[2] is not None and n[2] != part and (n[0], n[1]) < (li, seq):
                    out[(x, y)] = (li, seq, part, ol["ramp"], "base")
                    break
        grid = out
    return grid, clipped


def to_rgba(ir, grid):
    W, H = ir["W"], ir["H"]
    ramps = ir["style"]["ramps"]
    buf = bytearray(W * H * 4)
    for (x, y), v in grid.items():
        r, g, b = ramps[v[3]][v[4]]
        i = (y * W + x) * 4
        buf[i:i + 4] = bytes((r, g, b, 255))
    return bytes(buf)


def png_bytes(w, h, rgba, scale=1, bg=None):
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            px = rgba[(y * w + x) * 4:(y * w + x) * 4 + 4]
            if bg and px[3] == 0:
                px = bytes(bg) + b"\xff"
            row += px * scale
        rows += [b"\x00" + bytes(row)] * scale
    raw = b"".join(rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def resolve_auto_cell(ir, entries):
    """2.1 auto canvas (spec-next 01 §2): one cell for the whole asset.

    Union bbox of every frame of every animation (outline included) + margin,
    rounded up to multiple_of, raised to min_cell(size), placed per `align`
    (clamped so the bbox keeps >= margin on every side) and translated into the
    final cell. Metrics were evaluated at the provisional cell, so this is a
    pure grid/anchor/hitbox shift: nothing is re-derived, nothing can clip."""
    a = ir["auto"]
    xs = [x for _, _, f in entries for (x, _) in f["grid"]]
    ys = [y for _, _, f in entries for (_, y) in f["grid"]]
    minx, maxx = (min(xs), max(xs)) if xs else (0, 0)
    miny, maxy = (min(ys), max(ys)) if ys else (0, 0)
    bw = (maxx - minx + 1) if xs else 0
    bh = (maxy - miny + 1) if ys else 0
    m, k = a["margin"], a["multiple_of"]
    w, h = bw + 2 * m, bh + 2 * m
    mc = ir.get("min_cell")
    if mc:
        w, h = max(w, mc[0]), max(h, mc[1])
    w, h = max(1, (w + k - 1) // k * k), max(1, (h + k - 1) // k * k)
    px, py = ir["anchors"].get("origin", {}).get("at", [0, 0])
    if a["align"] == "top_left":
        dx, dy = m - minx, m - miny
    elif a["align"] == "pivot_center":
        dx, dy = w // 2 - px, h // 2 - py
    else:                                   # pivot_bottom_center
        dx, dy = w // 2 - px, h - 1 - m - py
    if xs:                                  # clamp into [margin .. cell-1-margin]
        dx = max(m - minx, min((w - 1 - m) - maxx, dx))
    if ys:
        dy = max(m - miny, min((h - 1 - m) - maxy, dy))
    ir["W"], ir["H"] = w, h
    for _, _, f in entries:
        f["grid"] = {(x + dx, y + dy): v for (x, y), v in f["grid"].items()}
        f["clipped"] = 0
    for ad in ir["anchors"].values():
        ad["at"] = [ad["at"][0] + dx, ad["at"][1] + dy]
    for hb in ir["hitboxes"]:
        hb["rect"] = [hb["rect"][0] + dx, hb["rect"][1] + dy, hb["rect"][2], hb["rect"][3]]


def render_all(ir):
    """Returns {'base': frame, 'idle': [frames]...}; frame = dict(grid, rgba, clipped, offsets).
    For an auto canvas the cell is derived here, before rgba is produced."""
    res = {}
    auto = bool(ir.get("auto"))

    def mk(offsets):
        grid, clipped = render_frame(ir, offsets, clip=not auto)
        return {"grid": grid, "rgba": None, "clipped": clipped, "offsets": offsets}

    entries = [("base", 0, mk({}))]
    for name, tpl in ir["animations"].items():
        for i, fr in enumerate(tpl["frames"]):
            entries.append((name, i, mk({k: tuple(v) for k, v in fr["offsets"].items()})))
    if auto:
        resolve_auto_cell(ir, entries)
    for _, _, f in entries:
        f["rgba"] = to_rgba(ir, f["grid"])
    for name, i, f in entries:
        res.setdefault(name, []).append(f)
    return res


# --------------------------------------------------------------------------
# Asset assertions
# --------------------------------------------------------------------------
def run_tests(ir, renders):
    asset = ir["asset"]
    W, H = ir["W"], ir["H"]
    frames = [f for fs in renders.values() for f in fs]
    results, diags = [], []

    def res(i, t, ok, observed, expected, msg=None):
        r = {"assert": t["assert"], "status": "PASS" if ok else "FAIL", "observed": observed, "expected": expected}
        results.append(r)
        if not ok:
            diags.append(D("E_TEST_FAILED", ptr("tests", i), msg or f"assertion '{t['assert']}' failed",
                           observed=observed, expected=expected))

    for i, t in enumerate(asset.get("tests", [])):
        a = t["assert"]
        if a == "dimensions":
            res(i, t, [W, H] == t.get("expected"), [W, H], t.get("expected"))
        elif a == "palette_size":
            cols = {(v[3], v[4]) for f in frames for v in f["grid"].values()}
            res(i, t, len(cols) <= t["max"], len(cols), f"<= {t['max']}")
        elif a == "feature_present":
            res(i, t, t["recipe"] in ir["features"], ir["features"], f"contains {t['recipe']}",
                f"feature '{t['recipe']}' is not in the compiled asset")
        elif a == "silhouette_area":
            frac = len(renders["base"][0]["grid"]) / (W * H)
            res(i, t, t.get("min", 0) <= frac <= t.get("max", 1), round(frac, 4), f"{t.get('min', 0)}..{t.get('max', 1)}")
        elif a == "bbox_margin":
            m = min(min(x, y, W - 1 - x, H - 1 - y) for f in frames for (x, y) in f["grid"])
            res(i, t, m >= t.get("min", 0), m, f">= {t.get('min', 0)}",
                "opaque pixels come closer to the canvas edge than allowed (outline included)")
        elif a == "anchor_inside_canvas":
            an = ir["anchors"].get(t.get("anchor"))
            if an is None:
                res(i, t, False, t.get("anchor"), sorted(ir["anchors"]), "unknown anchor")
                continue
            ok, obs = True, []
            for f in frames:
                ox, oy = part_offset(f["offsets"], an["part"])
                x, y = an["at"][0] + ox, an["at"][1] + oy
                obs.append([x, y])
                ok &= 0 <= x < W and 0 <= y < H
            res(i, t, ok, obs[:1], "inside canvas in every frame")
        elif a == "root_stable":
            disp = max(max(abs(f["offsets"].get("root", (0, 0))[0]), abs(f["offsets"].get("root", (0, 0))[1])) for f in frames)
            res(i, t, disp <= t["max"], disp, f"<= {t['max']}")
        elif a == "frame_count":
            n = sum(len(fs) for k, fs in renders.items() if k != "base")
            res(i, t, n <= t["max"], n, f"<= {t['max']}")

    # implicit checks, always on
    cols = {(v[3], v[4]) for f in frames for v in f["grid"].values()}
    if len(cols) > ir["style"]["max_colors"]:
        diags.append(D("E_PALETTE_BUDGET", "/style_pack", "palette budget exceeded", observed=len(cols),
                       expected=f"<= {ir['style']['max_colors']}"))
    policy = ir.get("clip_policy", "error")
    for name, fs in renders.items():
        for n, f in enumerate(fs):
            if f["clipped"]:
                p = f"/animations/{name}/{n}" if name != "base" else "/"
                if policy == "error":
                    diags.append(D("E_CLIPPED", p,
                                   f"{f['clipped']} pixel(s) fall outside the canvas and clip_policy is 'error'",
                                   observed=f["clipped"], expected="0"))
                else:
                    diags.append(D("W_CLIPPED", p,
                                   f"{f['clipped']} pixel(s) fall outside the canvas and were clipped",
                                   observed=f["clipped"], severity="warning"))
    # minimum feature size (style pack min_feature_px): the *compiled* shape
    # extent, integer, independent of the raster. min_feature_px 1 (or an
    # absent field) is vacuous, so the baseline pays nothing here.
    minf = ir["style"].get("min_feature_px")
    if minf and minf > 1:
        for d in ir["drawables"]:
            w, h = shape_extent(d)
            if min(w, h) < minf:
                kind, _, rest = d["src"].partition(":")
                p = f"/features/{rest}" if kind == "feature" else f"/{kind}/{rest}"
                diags.append(D("W_FEATURE_TOO_SMALL", p,
                               f"shape of '{d['src']}' is {w}x{h}px, below min_feature_px {minf}",
                               observed=[w, h], expected=f">= {minf}px on both axes",
                               severity="warning"))
    # occlusion: a feature whose every drawable pixel is overpainted in every
    # frame contributes nothing to the picture (spec 01 §9).
    feat = {}
    for d in ir["drawables"]:
        if d["src"].startswith("feature:"):
            feat.setdefault(d["src"][len("feature:"):], set()).add((d["seq"], d["part"]))
    if feat:
        seen = {(v[1], v[2]) for f in frames for v in f["grid"].values()}
        for fname in sorted(feat):
            if not (feat[fname] & seen):
                diags.append(D("W_OCCLUDED", f"/features/{fname}",
                               f"feature '{fname}' is fully occluded in every frame",
                               severity="warning"))
    return results, diags


# --------------------------------------------------------------------------
# Size sweep (spec-next 01 §9): sizes x recipes x parameter extremes
# --------------------------------------------------------------------------
SWEEP_ANIM_NAMES = ("idle", "walk", "attack", "hurt", "death")


def shape_extent(d):
    """Filled extent of one compiled shape in whole pixels, using exactly the
    rasterizer's pixel test so the number matches what gets drawn."""
    if d["kind"] == "ellipse":
        return d["box"][2], d["box"][3]
    if d["kind"] == "px":
        xs = [p[0] for p in d["px"]]
        ys = [p[1] for p in d["px"]]
        return max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    pts = d["pts"]
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, x1 = min(xs) // 64, max(xs) // 64 + 1
    y0, y1 = min(ys) // 64, max(ys) // 64 + 1
    fx, fy = [], []
    for py in range(y0, y1 + 1):
        for px in range(x0, x1 + 1):
            if point_in_poly(pts, px * 64 + 32, py * 64 + 32):
                fx.append(px)
                fy.append(py)
    if not fx:
        return 0, 0
    return max(fx) - min(fx) + 1, max(fy) - min(fy) + 1


def body_asymmetry(grid, axis):
    """Pixels breaking mirror symmetry about column `axis` (spec 01 §6).
    Colour only: painter order/seq is not observable in the output."""
    bad = 0
    for (x, y), v in grid.items():
        m = grid.get((2 * axis - x, y))
        if m is None or (m[-2], m[-1]) != (v[-2], v[-1]):
            bad += 1
    return bad


def sweep_sizes(arch):
    """size.min, each class boundary on both sides (max, max+1), size.max."""
    spec = arch["size"]
    primary = next(iter(spec))
    lo, hi = spec[primary]["min"], spec[primary]["max"]
    vals = {lo, hi}
    for c in arch.get("classes", []):
        vals.add(c["max"])
        vals.add(c["max"] + 1)
    return sorted(v for v in vals if lo <= v <= hi)


def sweep_extremes(specs, avail, style):
    """Derived extreme parameter sets: int min/max, bool both, enum/ramp every
    value, percent declared bounds (else 0/100). A percent whose `of` is not an
    archetype dimension is unavailable there and is skipped, not guessed."""
    mins, maxs, singles = {}, {}, []
    for k in sorted(specs):
        s, t = specs[k], specs[k]["type"]
        if t == "percent":
            if s.get("of") not in avail:
                continue
            mins[k], maxs[k] = s.get("min", 0), s.get("max", 100)
        elif t == "int":
            mins[k], maxs[k] = s["min"], s["max"]
        elif t == "bool":
            mins[k], maxs[k] = False, True
            singles += [(k, "false", False), (k, "true", True)]
        else:                                   # enum | ramp
            if t == "enum":
                vals = list(s["values"])
            else:
                vals = s.get("allow") or [r for r in sorted(style["ramps"])
                                          if r != style["outline"]["ramp"]]
                vals = [v for v in vals if v in style["ramps"]]
            if not vals:
                continue
            mins[k], maxs[k] = vals[0], vals[-1]
            singles += [(k, str(v), v) for v in vals]
    return mins, maxs, singles


def write_contact_sheet(path, cells, nrows, ncols, scale=4, pad=2, bg=(52, 56, 72)):
    """Contact sheet: one row per size, columns = body + per-recipe variants.
    cells[(row, col)] = (w, h, rgba); absent cells stay background. Cells are
    bottom-aligned in their row band like the spec-next prototype."""
    colw = [8] * ncols
    rowh = [8] * nrows
    for (r, c), (w, h, _) in cells.items():
        colw[c] = max(colw[c], w)
        rowh[r] = max(rowh[r], h)
    W = sum(colw) + pad * (ncols + 1)
    H = sum(rowh) + pad * (nrows + 1)
    buf = bytearray(W * H * 4)
    for (r, c), (w, h, rgba) in sorted(cells.items()):
        x0 = pad + sum(colw[:c]) + (colw[c] - w) // 2
        y0 = pad + sum(rowh[:r]) + (rowh[r] - h)
        for y in range(h):
            dst = ((y0 + y) * W + x0) * 4
            buf[dst:dst + w * 4] = rgba[y * w * 4:(y + 1) * w * 4]
    path.write_bytes(png_bytes(W, H, bytes(buf), scale=scale, bg=bg))


def sweep_archetype(arch, reg, base_path="", out_dir=None):
    """Spec-next 01 §9 size sweep for one archetype.

    Sizes = size.min, each class boundary (both sides), size.max; the cell is
    the archetype's own min_cell(size) (else canvas.default), so the sweep also
    executes the min-cell formula. Per size: the body alone (palette, clipping,
    mirror symmetry when `symmetric`), then every applicable recipe alone at
    default, its authored examples and derived extreme parameters; a recipe
    outside its declared `sizes` span is skipped there, not failed. Writes
    <out_dir>/<arch>.contact_sheet.png and .table.json. Returns
    (diagnostics, summary); diagnostic paths are
    <base_path>/sweep/<size>/<feature>/<label><path>."""
    size_specs = arch.get("size") or {}
    if not size_specs:
        return [], None
    name = arch["archetype"]
    primary = next(iter(size_specs))
    out_dir = Path(out_dir) if out_dir else ROOT / "out" / "sweep"
    out_dir.mkdir(parents=True, exist_ok=True)
    regx = dict(reg)
    regx["archetypes"] = dict(reg["archetypes"])
    regx["archetypes"][name] = arch              # fixture archetypes are not in the registry
    style_name = sorted(reg["styles"])[0]
    style = reg["styles"][style_name]
    sizes = sweep_sizes(arch)
    avail = (set(size_specs) | set(arch.get("params", {}))
             | {m[0] for m in arch.get("metrics", [])})
    recipes = [r for _, r in sorted(reg["recipes"].items()) if name in r["applies_to"]]

    def doc(size_val, cell, features):
        a = {"arcforge": "2.1",
             "asset": {"id": f"sweep.{name}", "type": "character", "version": "1.0.0"},
             "target": {"profile": "arcade1-draft", "canvas": list(cell), "clip_policy": "error"},
             "style_pack": style_name,
             "archetype": {"name": name, "size": {primary: size_val}},
             "features": features}
        anims = {n: {"template": n} for n in SWEEP_ANIM_NAMES if n in arch.get("animations", {})}
        if anims:
            a["animations"] = anims
        return a

    def p_at(sv, feature, label, p):
        return f"{base_path}/sweep/{sv}/{feature}/{label}" + p

    # per-recipe variant sets: deterministic labels, deduped on parameters
    per_recipe = []
    for rec in recipes:
        mins, maxs, singles = sweep_extremes(rec["parameters"], avail, style)
        variants = ([("default", {})]
                    + [(f"example#{i}", dict(ex)) for i, ex in enumerate(rec.get("examples", []), 1)]
                    + [("all_min", mins), ("all_max", maxs)]
                    + [(f"{k}={lv}", {k: v}) for k, lv, v in singles])
        seen, uniq = set(), []
        for label, params in variants:
            key = json.dumps(params, sort_keys=True)
            if key not in seen:
                seen.add(key)
                uniq.append((label, params))
        per_recipe.append((rec, uniq))

    # sheet columns: body + default/all_min/all_max of each recipe
    col_of, columns = {}, ["body"]
    for rec, uniq in per_recipe:
        for label, _ in uniq:
            if label in ("default", "all_min", "all_max"):
                col_of[(rec["recipe"], label)] = len(columns)
                columns.append(f"{rec['recipe']}:{label}")

    def failed(sv, ds, rows, why):
        for d in ds:
            diags.append(dict(d, path=p_at(sv, "body", "default", d["path"])))
        rows.append({"size": sv, "error": why})

    diags, rows, cells = [], [], {}
    for ri, sv in enumerate(sizes):
        # ---- cell probe: execute the archetype's own min_cell(size) -------
        d0, ir0 = compile_asset(doc(sv, arch["canvas"]["default"], []), regx, ROOT)
        if ir0 is None:
            small = [d for d in d0 if d["code"] == "E_CELL_TOO_SMALL"]
            other = [d for d in d0 if d["code"] != "E_CELL_TOO_SMALL"]
            if not (small and not other):
                failed(sv, other or d0, rows, "compile failed at canvas.default")
                continue
            cell = list(small[0]["expected"])   # min_cell(size) from the diagnostic
            d0, ir0 = compile_asset(doc(sv, cell, []), regx, ROOT)
            if ir0 is None:
                failed(sv, d0, rows, "compile failed at min_cell")
                continue
        else:
            cell = ir0.get("min_cell") or arch["canvas"]["default"]
            if cell != arch["canvas"]["default"]:
                d0, ir0 = compile_asset(doc(sv, cell, []), regx, ROOT)
                if ir0 is None:
                    failed(sv, d0, rows, "compile failed at min_cell")
                    continue
        # ---- body: palette/clipping + mirror symmetry ---------------------
        renders = render_all(ir0)
        _, bdiags = run_tests(ir0, renders)
        asym = 0
        if arch.get("symmetric"):
            axis = ir0["env"].get("cx", ir0["W"] // 2)
            asym = body_asymmetry(renders["base"][0]["grid"], axis)
            if asym:
                diags.append(D("E_BODY_ASYMMETRY", p_at(sv, "body", "default", "/symmetry"),
                               f"body is not mirror-symmetric about x={axis} at {primary}={sv}",
                               observed=asym, expected=0))
        for d in bdiags:
            diags.append(dict(d, path=p_at(sv, "body", "default", d["path"])))
        cells[(ri, 0)] = (ir0["W"], ir0["H"], renders["base"][0]["rgba"])
        row = {"size": sv, "cell": [ir0["W"], ir0["H"]],
               "body": {"asymmetric_px": asym, "codes": sorted({d["code"] for d in bdiags})},
               "recipes": {}}
        # ---- every applicable recipe alone, at its parameter extremes ------
        for rec, uniq in per_recipe:
            rname = rec["recipe"]
            span = rec.get("sizes")
            inside = span is None or span["min"] <= sv <= span["max"]
            rrow = {}
            for label, params in uniq:
                if not inside:
                    rrow[label] = {"skipped": f"outside sizes {span['min']}..{span['max']}"}
                    continue
                dv, irv = compile_asset(doc(sv, cell, [{"recipe": rname, "params": params}]), regx, ROOT)
                vdiags = list(dv)
                if irv is None:
                    for d in vdiags:
                        diags.append(dict(d, path=p_at(sv, rname, label, d["path"])))
                    rrow[label] = {"codes": sorted({d["code"] for d in vdiags})}
                    continue
                vrenders = render_all(irv)
                _, v2 = run_tests(irv, vrenders)
                vdiags += v2
                if (rname, label) in col_of:
                    cells[(ri, col_of[(rname, label)])] = (irv["W"], irv["H"], vrenders["base"][0]["rgba"])
                for d in vdiags:
                    diags.append(dict(d, path=p_at(sv, rname, label, d["path"])))
                rrow[label] = {"codes": sorted({d["code"] for d in vdiags})}
            row["recipes"][rname] = rrow
        rows.append(row)

    sheet = out_dir / f"{name}.contact_sheet.png"
    write_contact_sheet(sheet, cells, len(sizes), len(columns))
    tpath = out_dir / f"{name}.table.json"
    tpath.write_text(json.dumps({"archetype": name, "primary_size": primary,
                                 "sizes": sizes, "columns": columns, "rows": rows},
                                indent=2) + "\n")

    def rel(p):
        try:
            return str(p.relative_to(ROOT))
        except ValueError:
            return str(p)

    summary = {"sizes": sizes, "recipes": [rec["recipe"] for rec, _ in per_recipe],
               "cells": len(cells), "sheet": rel(sheet), "table": rel(tpath)}
    return diags, summary


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------
def report(stage, asset_id, diags, **extra):
    status = "FAIL" if any(d["severity"] == "error" for d in diags) else "PASS"
    r = {"status": status, "asset": asset_id, "stage": stage, "diagnostics": diags}
    r.update(extra)
    return r


def load_asset(path):
    p = Path(path)
    return json.loads(p.read_text()), p.parent


def asset_id(asset):
    return asset.get("asset", {}).get("id", "?") if isinstance(asset, dict) else "?"


def known_parts(reg):
    """Every part name an archetype can attach a drawable to (parts + anchor targets)."""
    names = set()
    for arch in reg["archetypes"].values():
        names |= set(arch["parts"])
        names |= {a["part"] for a in arch.get("anchors", {}).values()}
    return sorted(names)


def is_style_pack_doc(doc):
    return (isinstance(doc, dict) and "style_pack" in doc and "ramps" in doc
            and "arcforge" not in doc)


def check_style_pack(doc, reg, base_path=""):
    """Schema + registry cross-checks for a style-pack document."""
    diags = schema_diags(doc, "style_pack.schema.json", base_path)
    if diags:
        return diags
    inner = doc.get("outline", {}).get("inner", {})
    if inner.get("mode") != "part":
        return diags
    allp = known_parts(reg)
    for i, p in enumerate(inner.get("parts", [])):
        if p not in allp:
            path = base_path + ptr("outline", "inner", "parts", i)
            sug = close(p, allp)
            diags.append(D("E_UNKNOWN_PART", path, f"NO PART EXISTS named '{p}' in any archetype",
                           observed=p, expected=allp, suggestions=sug,
                           fix=([{"op": "replace", "path": path, "value": sug[0]}] if sug else
                                [{"op": "remove", "path": path}])))
    return diags


def cmd_validate(args):
    reg = load_registry()
    doc, base = load_asset(args.asset)
    if is_style_pack_doc(doc):
        diags = check_style_pack(doc, reg)
        r = report("validate", doc.get("style_pack", "?"), diags)
    else:
        diags, ir = compile_asset(doc, reg, base)
        r = report("validate", asset_id(doc), diags)
    print(json.dumps(r, indent=2))
    return 0 if r["status"] == "PASS" else 1


def cmd_render(args):
    reg = load_registry()
    asset, base = load_asset(args.asset)
    diags, ir = compile_asset(asset, reg, base)
    if ir is None:
        print(json.dumps(report("validate", asset_id(asset), diags), indent=2))
        return 1
    renders = render_all(ir)
    out = Path(args.out) / asset["asset"]["id"]
    out.mkdir(parents=True, exist_ok=True)
    W, H, s = ir["W"], ir["H"], args.scale
    (out / "base.png").write_bytes(png_bytes(W, H, renders["base"][0]["rgba"]))
    atlas = {"cell": [W, H],
             "pivot": list(ir["anchors"].get("origin", {}).get("at", [0, 0])),
             "animations": {}}
    rows = [(n, fs) for n, fs in renders.items() if n != "base"]
    cols = max([len(fs) for _, fs in rows] + [1])
    sheet = bytearray(W * cols * H * max(len(rows), 1) * 4)
    for ri, (n, fs) in enumerate(rows):
        tpl = ir["animations"][n]
        atlas["animations"][n] = {"row": ri, "frames": len(fs), "fps": tpl["fps"], "loop": tpl["loop"],
                                  "hit_frames": tpl.get("hit_frames", [])}
        for ci, f in enumerate(fs):
            (out / f"{n}_{ci}.png").write_bytes(png_bytes(W, H, f["rgba"]))
            for y in range(H):
                dst = ((ri * H + y) * W * cols + ci * W) * 4
                sheet[dst:dst + W * 4] = f["rgba"][y * W * 4:(y + 1) * W * 4]
    if rows:
        (out / "spritesheet.png").write_bytes(png_bytes(W * cols, H * len(rows), bytes(sheet)))
    atlas["hitboxes"] = ir["hitboxes"]
    atlas["anchors"] = {k: v for k, v in ir["anchors"].items()}
    (out / "atlas.json").write_text(json.dumps(atlas, indent=2))
    # preview: base frame + every animation row, scaled, on a dark backdrop
    pw = W * cols + (cols * 0)
    allrows = [("base", renders["base"])] + rows
    prev = bytearray(pw * H * len(allrows) * 4)
    for ri, (_, fs) in enumerate(allrows):
        for ci, f in enumerate(fs):
            for y in range(H):
                dst = ((ri * H + y) * pw + ci * W) * 4
                prev[dst:dst + W * 4] = f["rgba"][y * W * 4:(y + 1) * W * 4]
    (out / "preview.png").write_bytes(png_bytes(pw, H * len(allrows), bytes(prev), scale=s, bg=(52, 56, 72)))
    print(json.dumps(report("render", asset["asset"]["id"], [], stats={"out": str(out), "frames": sum(len(fs) for fs in renders.values())}), indent=2))
    return 0


def cmd_test(args):
    reg = load_registry()
    asset, base = load_asset(args.asset)
    diags, ir = compile_asset(asset, reg, base)
    if ir is None:
        print(json.dumps(report("validate", asset_id(asset), diags), indent=2))
        return 1
    renders = render_all(ir)
    results, d2 = run_tests(ir, renders)
    r = report("test", asset["asset"]["id"], diags + d2, tests=results)
    print(json.dumps(r, indent=2))
    return 0 if r["status"] == "PASS" else 1


def frame_hashes(renders):
    return {f"{n}/{i}": hashlib.sha256(f["rgba"]).hexdigest() for n, fs in renders.items() for i, f in enumerate(fs)}


def cmd_golden(args):
    reg = load_registry()
    asset, base = load_asset(args.asset)
    diags, ir = compile_asset(asset, reg, base)
    if ir is None:
        print(json.dumps(report("validate", asset_id(asset), diags), indent=2))
        return 1
    hashes = frame_hashes(render_all(ir))
    gp = ROOT / "tests" / "golden" / f"{asset['asset']['id']}.json"
    if args.update:
        gp.write_text(json.dumps({"asset": asset["asset"]["id"], "frames": hashes}, indent=2) + "\n")
        print(json.dumps(report("golden", asset["asset"]["id"], [], stats={"written": str(gp), "frames": len(hashes)}), indent=2))
        return 0
    want = json.loads(gp.read_text())["frames"] if gp.exists() else {}
    d = [D("E_GOLDEN_MISMATCH", "/" + k, "frame differs from golden", observed=hashes.get(k, "missing")[:12],
           expected=want.get(k, "missing")[:12]) for k in sorted(set(hashes) | set(want)) if hashes.get(k) != want.get(k)]
    r = report("golden", asset["asset"]["id"], d, stats={"frames": len(hashes)})
    print(json.dumps(r, indent=2))
    return 0 if r["status"] == "PASS" else 1


def cmd_lint(args):
    reg = load_registry()
    diags = []
    for pat, schema in (("archetypes/*.archetype.json", "archetype.schema.json"),
                        ("recipes/**/*.recipe.json", "recipe.schema.json"),
                        ("styles/*.style.json", "style_pack.schema.json")):
        for p in sorted(REG.glob(pat)):
            rel = str(p.relative_to(ROOT))
            if schema == "style_pack.schema.json":
                for d in check_style_pack(load_json(p), reg, rel + ":"):
                    diags.append(d)
                continue
            for d in schema_diags(load_json(p), schema, rel):
                diags.append(d)
    for name, rec in reg["recipes"].items():
        for ex_i, ex in enumerate(rec["examples"]):
            style_name = sorted(reg["styles"])[0]
            asset = {"arcforge": "2.0", "asset": {"id": f"lint.{name}", "type": "character", "version": "1.0.0"},
                     "target": {"profile": "arcade1-draft", "canvas": reg["archetypes"][rec["applies_to"][0]]["canvas"]["default"],
                                # registry examples may sit at the canvas edge; the
                                # baseline keeps reporting that as W_CLIPPED, not an error
                                "clip_policy": "warn"},
                     "style_pack": style_name, "archetype": {"name": rec["applies_to"][0]},
                     "features": [{"recipe": name, "params": ex}]}
            d, ir = compile_asset(asset, reg, ROOT)
            for x in d:
                x["path"] = f"recipe:{name}:example#{ex_i}{x['path']}"
            diags += d
            if ir:
                _, d2 = run_tests(ir, render_all(ir))
                for x in d2:
                    x["path"] = f"recipe:{name}:example#{ex_i}{x['path']}"
                diags += d2
    # size sweep (01 §9) over every size-parametric archetype
    sweep_stats = {}
    for aname in sorted(reg["archetypes"]):
        a = reg["archetypes"][aname]
        if not a.get("size"):
            continue
        sd, summary = sweep_archetype(a, reg, base_path=f"archetype:{aname}",
                                      out_dir=ROOT / "out" / "sweep")
        diags += sd
        if summary:
            sweep_stats[aname] = summary
    r = report("lint", "registry", diags, stats={"archetypes": len(reg["archetypes"]), "recipes": len(reg["recipes"]),
                                                 "styles": len(reg["styles"]), "sweep": sweep_stats})
    print(json.dumps(r, indent=2))
    return 0 if r["status"] == "PASS" else 1


def cmd_sweep(args):
    reg = load_registry()
    arch, _ = load_asset(args.archetype)
    diags = schema_diags(arch, "archetype.schema.json")
    if diags:
        print(json.dumps(report("sweep", arch.get("archetype", "?"), diags), indent=2))
        return 1
    sd, summary = sweep_archetype(arch, reg, base_path=f"archetype:{arch['archetype']}",
                                  out_dir=ROOT / "out" / "sweep")
    r = report("sweep", arch["archetype"], sd, stats=summary or {})
    print(json.dumps(r, indent=2))
    return 0 if r["status"] == "PASS" else 1


def cmd_list(args):
    reg = load_registry()
    kinds = [args.kind] if args.kind else ["archetypes", "recipes", "styles"]
    out = {}
    for k in kinds:
        if k == "recipes":
            out[k] = {n: {"category": r["category"], "slot": r["slot"], "applies_to": r["applies_to"],
                          "parameters": sorted(r["parameters"])} for n, r in reg[k].items()}
        else:
            out[k] = {n: v.get("description", "") for n, v in reg[k].items()}
    print(json.dumps(out, indent=2))
    return 0


def cmd_describe(args):
    reg = load_registry()
    table = reg[{"recipe": "recipes", "archetype": "archetypes", "style": "styles"}[args.kind]]
    if args.name not in table:
        print(json.dumps(report("validate", args.name, [D("E_UNKNOWN_" + args.kind.upper(), "/", f"no {args.kind} '{args.name}'",
                                                          suggestions=close(args.name, table), expected=sorted(table))]), indent=2))
        return 1
    print(json.dumps(table[args.name], indent=2))
    return 0


def main():
    ap = argparse.ArgumentParser(prog="arcforge_ref")
    sp = ap.add_subparsers(dest="cmd", required=True)
    for n, f in (("validate", cmd_validate), ("test", cmd_test)):
        p = sp.add_parser(n)
        p.add_argument("asset")
        p.set_defaults(fn=f)
    p = sp.add_parser("render")
    p.add_argument("asset")
    p.add_argument("--out", default="out")
    p.add_argument("--scale", type=int, default=8)
    p.set_defaults(fn=cmd_render)
    p = sp.add_parser("golden")
    p.add_argument("asset")
    p.add_argument("--update", action="store_true")
    p.set_defaults(fn=cmd_golden)
    p = sp.add_parser("lint")
    p.set_defaults(fn=cmd_lint)
    p = sp.add_parser("sweep")
    p.add_argument("archetype")
    p.set_defaults(fn=cmd_sweep)
    p = sp.add_parser("list")
    p.add_argument("kind", nargs="?", choices=["archetypes", "recipes", "styles"])
    p.set_defaults(fn=cmd_list)
    p = sp.add_parser("describe")
    p.add_argument("kind", choices=["recipe", "archetype", "style"])
    p.add_argument("name")
    p.set_defaults(fn=cmd_describe)
    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
