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
TONES = ("deep", "shadow", "base", "light", "high")   # a ramp has 3..5 of these; "ramp.*" = shaded


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
    def __init__(self, line, msg, code="E_PX_PARSE"):
        super().__init__(msg)
        self.line = line
        self.msg = msg
        self.code = code


def parse_px(text):
    """Parse a `.px` fragment (v1 or v2, spec-next 02 §G).

    v1: `rows` block after the shared header. v2: `px 2`, a declared
    `frames N`, and `frame i` blocks; size, legend and anchor are shared by
    every frame. File-grammar violations raise PxError with e.code —
    E_PX_FRAMES for the frame protocol, E_PX_PARSE otherwise. Whether a
    *consumer* may select a given frame is checked by the caller, against
    the shape's path, after this returns."""
    out = {"name": None, "size": None, "anchors": {}, "legend": {}, "rows": [],
           "version": 1, "frames": None, "frame_count": 1, "fps": None}
    block = None
    frame_idx = None
    seen_header = False
    declared = None
    raw_frames = {}
    for ln, raw in enumerate(text.splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if not raw.startswith((" ", "\t")):
            block = None
            parts = raw.split()
            kw = parts[0]
            if kw == "px":
                if parts[1:] not in (["1"], ["2"]):
                    raise PxError(ln, "unsupported px version (expected 'px 1' or 'px 2')")
                out["version"] = int(parts[1])
                seen_header = True
            elif not seen_header:
                raise PxError(ln, "first statement must be 'px 1' or 'px 2'")
            elif kw == "name" and len(parts) == 2:
                out["name"] = parts[1]
            elif kw == "size" and len(parts) == 3 and parts[1].isdigit() and parts[2].isdigit():
                out["size"] = (int(parts[1]), int(parts[2]))
            elif kw == "anchor" and len(parts) == 4 and parts[2].lstrip("-").isdigit() and parts[3].lstrip("-").isdigit():
                out["anchors"][parts[1]] = (int(parts[2]), int(parts[3]))
            elif kw == "frames" and len(parts) == 2 and parts[1].isdigit():
                if out["version"] == 1:
                    raise PxError(ln, "'frames N' requires 'px 2'", code="E_PX_FRAMES")
                declared = int(parts[1])
            elif kw == "fps" and len(parts) == 2 and parts[1].isdigit():
                if out["version"] == 1:
                    raise PxError(ln, "'fps' requires 'px 2'", code="E_PX_FRAMES")
                out["fps"] = int(parts[1])
            elif kw == "frame" and len(parts) == 2 and parts[1].isdigit():
                if out["version"] == 1:
                    raise PxError(ln, "'frame i' blocks require 'px 2'", code="E_PX_FRAMES")
                frame_idx = int(parts[1])
                if frame_idx in raw_frames:
                    raise PxError(ln, f"duplicate 'frame {frame_idx}' block", code="E_PX_FRAMES")
                raw_frames[frame_idx] = []
                block = "frame"
            elif kw in ("legend", "rows") and len(parts) == 1:
                if kw == "rows" and out["version"] == 2:
                    raise PxError(ln, "'px 2' uses 'frame i' blocks, not 'rows'", code="E_PX_FRAMES")
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
            elif block == "frame":
                raw_frames[frame_idx].append((ln, body))
            else:
                raise PxError(ln, "indented line outside a block")
    if not seen_header:
        raise PxError(1, "missing 'px 1' or 'px 2' header")
    if out["size"] is None:
        raise PxError(1, "missing 'size W H'")
    if out["version"] == 2:
        if declared is None:
            raise PxError(1, "px 2 requires 'frames N'", code="E_PX_FRAMES")
        if declared < 1:
            raise PxError(1, f"'frames {declared}' must be at least 1", code="E_PX_FRAMES")
        if sorted(raw_frames) != list(range(declared)):
            raise PxError(1, f"'frames {declared}' but frame blocks found for {sorted(raw_frames)} "
                             f"(need one each for 0..{declared - 1})", code="E_PX_FRAMES")
        out["frames"] = [raw_frames[i] for i in range(declared)]
        out["frame_count"] = declared
        frame_sets = out["frames"]
    else:
        frame_sets = [out["rows"]]
    w, h = out["size"]
    for fi, rows in enumerate(frame_sets):
        if len(rows) != h:
            raise PxError(rows[0][0] if rows else 1,
                          f"frame {fi}: expected {h} rows, found {len(rows)}")
        for ln, row in rows:
            if len(row) != w:
                raise PxError(ln, f"frame {fi}: row has {len(row)} pixels, expected {w}")
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
# Schema diagnostics (ROADMAP P0.1 + P0.2)
#
# jsonschema supplies only the *structure* of a failure: the keyword that
# failed, the expected value, the instance and the path. The wording comes
# from the tables below, because the C compiler (M4) must reproduce every
# report byte-for-byte and no library text may leak into a message. One
# schema check produces one list of diagnostics, deduplicated and ordered by
# (path, kind, message).
#
# Name formats (P0.2): the schemas declare a vocabulary name via
# `"format"` and the check below runs it by hand -- no regex engine in the
# dependency-free C build, and the 64-character cap keeps every identifier
# under the 200-character threshold where difflib's SequenceMatcher.autojunk
# starts applying, so similarity behaves the same in both tools.
# --------------------------------------------------------------------------
def _digits(s):
    return bool(s) and all("0" <= c <= "9" for c in s)


def _is_identifier(s):
    """`identifier`: 1..64 characters of [a-z0-9_]."""
    return 0 < len(s) <= 64 and all(c == "_" or "a" <= c <= "z" or "0" <= c <= "9" for c in s)


def _is_dotted_identifier(s):
    """`dotted_identifier`: identifiers joined by `.` (at least one)."""
    return all(_is_identifier(p) for p in s.split("."))


def _is_semver(s):
    """`semver`: exactly three groups of 1..n digits `0-9` joined by `.`."""
    parts = s.split(".")
    return len(parts) == 3 and all(_digits(p) for p in parts)


def _is_category_path(s):
    """`category_path`: identifiers joined by `/` (at least one)."""
    return all(_is_identifier(p) for p in s.split("/"))


def _is_parameter_name(s):
    """`parameter_name`: `p_` + identifier, 64 characters total."""
    return s.startswith("p_") and len(s) <= 64 and _is_identifier(s[2:])


def _is_diagnostic_code(s):
    """`diagnostic_code`: `E_`/`W_` followed by 1..n of [A-Z_]."""
    return len(s) >= 3 and s[:2] in ("E_", "W_") and \
        all("A" <= c <= "Z" or c == "_" for c in s[2:])


VOCABULARY = {
    "identifier": _is_identifier,
    "dotted_identifier": _is_dotted_identifier,
    "semver": _is_semver,
    "category_path": _is_category_path,
    "parameter_name": _is_parameter_name,
    "diagnostic_code": _is_diagnostic_code,
}

KIND_RANK = {       # the "kind" half of the (path, kind, message) ordering
    "additionalProperties": 0, "required": 1, "type": 2, "enum": 3,
    "const": 4, "pattern": 5, "minimum": 6, "maximum": 7, "minItems": 8,
    "maxItems": 9, "minProperties": 10, "maxProperties": 11,
    "uniqueItems": 12, "items": 13, "oneOf": 14, "anyOf": 15, "allOf": 16,
}
KIND_UNKNOWN = 99


def _lit(v):
    """Literal for a message: JSON encoding, byte-stable on every platform."""
    return json.dumps(v)


def _typename(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "boolean"
    if isinstance(v, int):
        return "integer"
    if isinstance(v, float):
        return "number"
    if isinstance(v, str):
        return "string"
    if isinstance(v, list):
        return "array"
    return "object"


def _scalar(v):
    """observed/expected carry scalars; a whole object in prose is noise."""
    return v if v is None or isinstance(v, (str, int, float, bool)) else None


def _emit(out, kind, path, message, code="E_SCHEMA", **kw):
    out.append((tuple(path.split("/")), KIND_RANK.get(kind, KIND_UNKNOWN),
                kind, message, D(code, path, message, **kw)))


def _vocab_message(value, fmt):
    return f"{_lit(value)} is not a valid {fmt.replace('_', ' ')}"


def _resolve_ref(root, ref):
    """Resolve a local `#/$defs/...` reference inside one schema document."""
    if not isinstance(ref, str) or not ref.startswith("#/"):
        return None
    node = root
    for part in ref[2:].split("/"):
        part = part.replace("~1", "/").replace("~0", "~")
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node if isinstance(node, dict) else None


def _collect_formats(doc, schema, root, prefix, out, dpath=""):
    """P0.2: run the pattern vocabulary by hand.

    Walks every place a schema can attach a `format`: string values, object
    keys (propertyNames), array items (prefixItems then items),
    additionalProperties values, and each oneOf/allOf/if/then/else branch.
    `dpath` is the pointer of `doc` inside the document, `prefix` the
    file-level prefix used by registry lint. jsonschema does not assert
    `format`, so this is the only place the check happens.
    """
    if not isinstance(schema, dict):
        return
    if "$ref" in schema:
        target = _resolve_ref(root, schema["$ref"])
        # refs in schema/ always point at $defs; a broken one is a schema bug
        if target is not None:
            _collect_formats(doc, target, root, prefix, out, dpath)
        return
    fmt = schema.get("format")
    if isinstance(fmt, str) and fmt in VOCABULARY and isinstance(doc, str) \
            and not VOCABULARY[fmt](doc):
        _emit(out, "pattern", prefix + dpath, _vocab_message(doc, fmt),
              observed=doc, expected=fmt)
    if isinstance(doc, dict):
        props = schema.get("properties")
        extra = schema.get("additionalProperties")
        for k, v in doc.items():
            if isinstance(props, dict) and k in props:
                _collect_formats(v, props[k], root, prefix, out, dpath + ptr(k))
            if isinstance(extra, dict):
                _collect_formats(v, extra, root, prefix, out, dpath + ptr(k))
        names = schema.get("propertyNames")
        if isinstance(names, dict):
            for k in doc:
                _collect_formats(k, names, root, prefix, out, dpath + ptr(k))
    elif isinstance(doc, list):
        start = 0
        pfx = schema.get("prefixItems")
        if isinstance(pfx, list):
            for i, v in enumerate(doc[:len(pfx)]):
                _collect_formats(v, pfx[i], root, prefix, out, dpath + ptr(i))
            start = len(pfx)
        items = schema.get("items")
        if isinstance(items, dict):
            for i, v in enumerate(doc[start:], start):
                _collect_formats(v, items, root, prefix, out, dpath + ptr(i))
    for kw in ("oneOf", "allOf", "anyOf"):
        branches = schema.get(kw)
        if isinstance(branches, list):
            for br in branches:
                _collect_formats(doc, br, root, prefix, out, dpath)
    cond = schema.get("if")
    if isinstance(cond, dict):
        # if/then/else: assert `then` only when the condition holds (and
        # `else` only when it does not). The conditions in schema/ are flat
        # (no $ref), so the condition is checked as written.
        from jsonschema import Draft202012Validator
        matched = Draft202012Validator(cond).is_valid(doc)
        branch = schema.get("then" if matched else "else")
        if isinstance(branch, dict):
            _collect_formats(doc, branch, root, prefix, out, dpath)
    # `not` is deliberately not walked: a format inside it must NOT hold,
    # so asserting it there would invert the meaning (schema/ has no `not`).


def _collect_schema_error(e, prefix, root, out):
    """Turn one jsonschema ValidationError into our diagnostics.

    Never reads e.message: every sentence is composed here from the
    keyword, the expected value and the instance. `prefix` is the pointer
    this document sits at (a registry file path, or the enclosing oneOf
    value); `root` is the whole schema, for $defs when re-checking branches.
    """
    kind = e.validator
    path = prefix + ptr(*e.absolute_path)

    if kind == "additionalProperties":
        # structural: the instance's keys that the object schema does not
        # declare. jsonschema would only tell us this in prose.
        props = e.schema.get("properties", {}) if isinstance(e.schema, dict) else {}
        for k in sorted(k for k in e.instance if k not in props):
            _emit(out, kind, path + ptr(k),
                  f"unknown field '{k}'. Free text is metadata, never executable visual intent; "
                  "use archetype/features/params or a custom px_fragment",
                  code="E_UNKNOWN_FIELD", suggestions=["features", "custom", "metadata"])
        return

    if kind == "oneOf":
        _collect_oneof(e, prefix, root, out)
        return

    if kind == "required" and isinstance(e.instance, dict):
        # one diagnostic per missing key; the set is recomputed from the
        # required list and the instance instead of parsed out of prose.
        for k in (k for k in e.validator_value if k not in e.instance):
            _emit(out, kind, path, f"'{k}' is a required property", expected=k)
        return

    if kind == "type":
        exp = e.validator_value
        shape = exp if isinstance(exp, str) else " or ".join(exp)
        _emit(out, kind, path, f"expected {shape}, got {_typename(e.instance)}",
              observed=_scalar(e.instance), expected=shape)
        return

    if kind == "enum":
        exp = list(e.validator_value)
        _emit(out, kind, path, f"expected one of {_lit(exp)}, got {_lit(e.instance)}",
              observed=_scalar(e.instance), expected=exp)
        return

    if kind == "const":
        _emit(out, kind, path, f"expected {_lit(e.validator_value)}, got {_lit(e.instance)}",
              observed=_scalar(e.instance), expected=e.validator_value)
        return

    if kind in ("minimum", "maximum"):
        op = ">=" if kind == "minimum" else "<="
        _emit(out, kind, path, f"expected {op} {_lit(e.validator_value)}, got {_lit(e.instance)}",
              observed=_scalar(e.instance), expected=f"{op} {e.validator_value}")
        return

    if kind in ("minItems", "maxItems", "minProperties", "maxProperties"):
        at = "at least" if kind.startswith("min") else "at most"
        b = e.validator_value
        unit = ("entry" if b == 1 else "entries") if kind.endswith("Items") \
            else ("key" if b == 1 else "keys")
        n = len(e.instance)
        _emit(out, kind, path, f"expected {at} {b} {unit}, got {n}",
              observed=n, expected=f"{at} {b}")
        return

    if kind == "uniqueItems":
        dup, found = None, False
        for i, v in enumerate(e.instance):
            if v in e.instance[:i]:
                dup, found = v, True
                break
        _emit(out, kind, path,
              f"duplicate entry {_lit(dup)}" if found else "array entries must be unique",
              observed=_scalar(dup) if found else None)
        return

    if kind == "items":
        # `items: false` behind prefixItems: the array may not grow longer.
        n = len(e.instance) if isinstance(e.instance, list) else None
        if n is not None and isinstance(e.schema, dict) and "prefixItems" in e.schema:
            b = len(e.schema["prefixItems"])
            _emit(out, kind, path, f"expected at most {b} entries, got {n}",
                  observed=n, expected=f"at most {b}")
            return

    if e.context:   # anyOf and friends wrap their failures
        for sub in sorted(e.context, key=lambda c: (tuple(map(str, c.absolute_path)), c.validator)):
            _collect_schema_error(sub, prefix, root, out)
        return
    _emit(out, kind, path, f"value does not satisfy the '{kind}' constraint",
          observed=_scalar(e.instance))


def _collect_oneof(e, prefix, root, out):
    """Explain a oneOf failure with the form the document reached for.

    The branches are re-checked structurally (jsonschema only says that
    none of them fits):
      1. a branch that fails solely because keys are missing at the value
         itself names the intended form;
      2. otherwise the deepest disagreement -- the entry or field that is
         actually wrong -- beats a bare "wrong shape" at the top, and
         branches that disagree at the same spot are merged into one
         message;
      3. if every branch only disputes the top-level shape, the shapes are
         merged ("expected array or object, got string").
    Branches are validated inside a wrapper carrying the schema's $defs,
    because a branch may $ref them (`#/$defs/...`).
    """
    path = prefix + ptr(*e.absolute_path)
    branches = e.schema.get("oneOf") if isinstance(e.schema, dict) else None
    if not isinstance(branches, list) or not branches:
        _emit(out, "oneOf", path, "value does not satisfy one of the allowed forms",
              observed=_scalar(e.instance))
        return
    from jsonschema import Draft202012Validator
    defs = root.get("$defs", {}) if isinstance(root, dict) else {}
    checked = [list(Draft202012Validator({"$defs": defs, "allOf": [b]}).iter_errors(e.instance))
               for b in branches]
    if sum(1 for es in checked if not es) > 1:
        _emit(out, "oneOf", path, "value matches more than one allowed form",
              observed=_scalar(e.instance))
        return
    intended = [es for es in checked
                if es and all(c.validator == "required" and not list(c.absolute_path) for c in es)]
    if intended:
        for sub in intended[0]:
            _collect_schema_error(sub, path, root, out)
        return

    allerrs = [c for es in checked for c in es]
    located = [(ptr(*c.absolute_path), c) for c in allerrs]
    depth = max(p.count("/") for p, _c in located)
    if depth:
        pool = [(p, c) for p, c in located if p.count("/") == depth]
    else:
        specific = [(p, c) for p, c in located if c.validator != "type"]
        pool = specific or located
    groups = {}
    for p, c in pool:
        groups.setdefault((p, c.validator), []).append(c)
    for key in sorted(groups, key=lambda k: (k[0], KIND_RANK.get(k[1], KIND_UNKNOWN), k[1])):
        g = groups[key]
        if len(g) > 1 and g[0].validator in ("const", "type"):
            exps = []
            for c in g:
                if c.validator_value not in exps:
                    exps.append(c.validator_value)
            gp = path + key[0]
            if g[0].validator == "type":
                shape = " or ".join(x if isinstance(x, str) else " or ".join(x) for x in exps)
                _emit(out, "type", gp, f"expected {shape}, got {_typename(g[0].instance)}",
                      observed=_scalar(g[0].instance), expected=shape)
            elif len(exps) == 1:
                _emit(out, "const", gp,
                      f"expected {_lit(exps[0])}, got {_lit(g[0].instance)}",
                      observed=_scalar(g[0].instance), expected=exps[0])
            else:
                _emit(out, "const", gp,
                      f"expected one of {_lit(exps)}, got {_lit(g[0].instance)}",
                      observed=_scalar(g[0].instance), expected=list(exps))
            continue
        for c in g:
            _collect_schema_error(c, path, root, out)


def schema_diags(doc, schema_name, base_path=""):
    """Schema failures as our diagnostics, ordered (path, kind, message).

    Two sources: jsonschema for the structural keywords, and the hand-run
    pattern vocabulary (P0.2) for `format`. Deduplicated: the same message
    at the same path can be reached through more than one keyword, more
    than one oneOf branch, or both sources."""
    from jsonschema import Draft202012Validator
    root = load_json(SCHEMA_DIR / schema_name)
    entries = []
    for e in Draft202012Validator(root).iter_errors(doc):
        _collect_schema_error(e, base_path, root, entries)
    _collect_formats(doc, root, root, base_path, entries)
    entries.sort(key=lambda t: t[:4])
    diags, seen = [], set()
    for _parts, _rank, _kind, message, d in entries:
        key = (d["code"], d["path"], message)
        if key not in seen:
            seen.add(key)
            diags.append(d)
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


def resolve_fill(fill, env, ramps, swap=None):
    if fill == "outline":
        return ("outline", "base")
    m = re.fullmatch(r"(\$?[A-Za-z_][A-Za-z0-9_]*)\.(deep|shadow|base|light|high|\*)", fill)
    if not m:
        raise ExprError(f"bad fill {fill!r}")
    ramp = m.group(1)
    if ramp.startswith("$"):
        if ramp[1:] not in env:
            raise ExprError(f"unknown variable {ramp}")
        ramp = env[ramp[1:]]
    if swap and ramp in swap:
        ramp = swap[ramp]   # palette_swap (02 §E): applied when fills resolve
    if ramp not in ramps or (m.group(2) != "*" and m.group(2) not in ramps[ramp]):
        raise ExprError(f"ramp/tone '{ramp}.{m.group(2)}' does not exist in the style pack")
    return (ramp, m.group(2))


def build_shape(sh, env, anchor_at, layer_idx, ramps, swap=None):
    """Evaluate one shape; returns a drawable dict (without part/seq) or None."""
    if "when" in sh and not ev(sh["when"], env):
        return None
    fill = resolve_fill(sh["fill"], env, ramps, swap)
    ax, ay = anchor_at if anchor_at else (0, 0)
    deg = to_int(ev(sh["rotate"], env)) if "rotate" in sh else 0
    if not -180 <= deg <= 180:
        raise ExprError("rotate must be within -180..180 degrees")
    kind = sh["shape"]
    if fill[1] == "*" and kind not in ("ellipse", "capsule"):
        raise ExprError("only ellipse and capsule shapes can be shaded (fill 'ramp.*')")
    if kind == "capsule":
        # thick line segment (round caps, or caps "flat"); x1,y1,x2,y2 are the cap centres, w the width in px
        x1, y1, x2, y2, w = (to_int(ev(sh[k], env)) for k in ("x1", "y1", "x2", "y2", "w"))
        if w <= 0:
            return None
        a, b = rot_fixed(x1, y1, deg), rot_fixed(x2, y2, deg)
        return {"kind": "capsule", "p": (ax * 64 + a[0], ay * 64 + a[1], ax * 64 + b[0], ay * 64 + b[1]),
                "hw": w * 32, "flat": sh.get("caps") == "flat", "fill": fill, "li": layer_idx}
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


class ShapeErr(Exception):
    """A shape-vocabulary violation (spec 02 §C): reported per shape with the
    new codes E_SHAPE_PARAM / E_REPEAT_BOUND while the rest of the shape list
    still compiles. Expression failures stay ExprError (E_ARCHETYPE_EXPR /
    E_RECIPE_EXPR), exactly as before."""
    def __init__(self, code, msg, observed=None, expected=None):
        super().__init__(msg)
        self.code, self.msg = code, msg
        self.observed, self.expected = observed, expected

    def diag(self, path):
        return D(self.code, path, self.msg, observed=self.observed, expected=self.expected)


def bresenham(x1, y1, x2, y2):
    """Integer line through both endpoints inclusive, fixed step order."""
    pts = []
    dx, dy = abs(x2 - x1), -abs(y2 - y1)
    sx = 1 if x1 < x2 else -1
    sy = 1 if y1 < y2 else -1
    err = dx + dy
    x, y = x1, y1
    while True:
        pts.append((x, y))
        if x == x2 and y == y2:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x += sx
        if e2 <= dx:
            err += dx
            y += sy
    return pts


def reflect_shape(d, a):
    """Exact integer reflection of one compiled drawable about column a, applied
    before rasterization (spec 02 §C `mirror`). Poly points are fixed-point:
    X' = (2a+1)*64 - X mirrors pixel centres exactly, so a rectangle
    [x, x+w) lands on [2a-x-w+1, 2a-x+1) — the same set the rasterizer fills.
    px entries keep `at` and mirror their offsets: dx' = 2a - 2*at - dx."""
    if d["kind"] == "ellipse":
        x, y, w, h = d["box"]
        d["box"] = (2 * a - x - w + 1, y, w, h)
    elif d["kind"] == "px":
        ax = d["at"][0]
        d["px"] = sorted((2 * a - 2 * ax - dx, dy, fill) for dx, dy, fill in d["px"])
    elif d["kind"] == "capsule":
        x0, y0, x1, y1 = d["p"]
        d["p"] = ((2 * a + 1) * 64 - x0, y0, (2 * a + 1) * 64 - x1, y1)
    else:
        d["pts"] = [((2 * a + 1) * 64 - X, Y) for X, Y in d["pts"]]
    return d


def shift_shape(d, dx, dy):
    """Translate one compiled drawable by whole pixels (repeat steps)."""
    if d["kind"] == "ellipse":
        x, y, w, h = d["box"]
        d["box"] = (x + dx, y + dy, w, h)
    elif d["kind"] == "px":
        ax, ay = d["at"]
        d["at"] = (ax + dx, ay + dy)
    elif d["kind"] == "capsule":
        x0, y0, x1, y1 = d["p"]
        d["p"] = (x0 + dx * 64, y0 + dy * 64, x1 + dx * 64, y1 + dy * 64)
    else:
        d["pts"] = [(X + dx * 64, Y + dy * 64) for X, Y in d["pts"]]
    return d


def shape_layer(sh, g):
    """Layer of a shape; recipe children inherit it from their repeat."""
    layer = sh.get("layer", g.get("layer"))
    if layer is None:
        raise ExprError("shape has no layer")
    if layer not in g["layers"]:
        raise ExprError(f"unknown layer '{layer}'")
    return layer


def shape_anchor(sh, g):
    """Anchor of a recipe shape; children inherit it from their repeat.
    Archetype shapes have no anchor unless they are a px fragment."""
    anchor = sh.get("anchor", g.get("anchor"))
    if anchor is None:
        raise ExprError("shape has no anchor")
    if anchor not in g["anchors"]:
        raise ExprError(f"unknown anchor '{anchor}'")
    return anchor


def build_line(sh, env, at, li, fill_str, ramps, swap=None):
    """`line {x1,y1,x2,y2,thickness}` (spec 02 §C): integer Bresenham with a
    thickness^2 square brush centred on every step; thickness must be a positive
    odd integer. Emitted as a px drawable, which the rasterizer draws as-is."""
    deg = to_int(ev(sh["rotate"], env)) if "rotate" in sh else 0
    if deg:
        raise ShapeErr("E_SHAPE_PARAM", "line cannot be rotated", observed=deg, expected=0)
    x1, y1, x2, y2 = (to_int(ev(sh[k], env)) for k in ("x1", "y1", "x2", "y2"))
    th = to_int(ev(sh["thickness"], env))
    if th < 1 or th % 2 == 0:
        raise ShapeErr("E_SHAPE_PARAM", "line thickness must be a positive odd integer",
                       observed=th, expected="odd >= 1")
    fill = resolve_fill(fill_str, env, ramps, swap)
    r = th // 2
    ax, ay = at if at else (0, 0)
    pts = sorted({(px + i, py + j)
                  for px, py in bresenham(x1, y1, x2, y2)
                  for i in range(-r, r + 1) for j in range(-r, r + 1)})
    return {"kind": "px", "at": (ax, ay), "px": [(dx, dy, fill) for dx, dy in pts], "li": li}


def build_px(sh, g, at, li, path):
    """`px {file, frame?, flip?}` (spec 02 §C + §G): a fixed .px fragment
    inside a shape list; `frame` (default 0) selects a px-2 frame, `flip`
    mirrors the pixel offsets about the fragment's attach pixel. File and
    legend failures reuse the custom-fragment codes; an unselectable frame
    is E_PX_FRAMES at the shape's /frame."""
    fpath = Path(g["base_dir"]) / sh["file"]
    if not fpath.exists():
        g["diags"].append(D("E_PX_NOT_FOUND", path + "/file", f"fragment file not found: {sh['file']}"))
        return []
    try:
        px = parse_px(fpath.read_text())
    except PxError as e:
        g["diags"].append(D(e.code, path + "/file", f"{sh['file']} line {e.line}: {e.msg}"))
        return []
    frame = sh.get("frame", 0)
    if not 0 <= frame < px["frame_count"]:
        g["diags"].append(D("E_PX_FRAMES", path + "/frame",
                            f"fragment '{sh['file']}' has {px['frame_count']} frame(s), requested frame {frame}",
                            observed=frame, expected=f"0..{px['frame_count'] - 1}"))
        return []
    legend = {}
    bad = False
    for ch, val in px["legend"].items():
        if val == "transparent":
            continue
        try:
            legend[ch] = resolve_fill(val, {}, g["ramps"], g.get("swap"))
        except ExprError as e:
            g["diags"].append(D("E_PX_LEGEND", path + "/file", f"{sh['file']}: legend '{ch}': {e}"))
            bad = True
    if bad:
        return []
    rows = px["rows"] if px["version"] == 1 else px["frames"][frame]
    pxs = []
    fax, fay = px["anchors"]["attach"]
    for r, (_, row) in enumerate(rows):
        for col, ch in enumerate(row):
            if ch in legend:
                pxs.append((col - fax, r - fay, legend[ch]))
    flip = sh.get("flip")
    if flip == "x":
        pxs = [(-dx, dy, fill) for dx, dy, fill in pxs]
    elif flip == "y":
        pxs = [(dx, -dy, fill) for dx, dy, fill in pxs]
    if not pxs:
        return []
    ax, ay = at if at else (0, 0)
    return [{"kind": "px", "at": (ax, ay), "px": pxs, "li": li}]


def build_repeat(sh, env, g, path, depth):
    """`repeat {var,count,step_x,step_y,max,shapes[]}` (spec 02 §C): a bounded
    loop; `$var` and `$i` are bound per iteration in fixed order 0..count-1.
    count <= max <= 256 and nesting <= 2 levels, else E_REPEAT_BOUND."""
    if depth >= 2:
        raise ShapeErr("E_REPEAT_BOUND", "repeat nesting exceeds 2 levels",
                       observed=depth + 1, expected="<= 2")
    mx = to_int(ev(sh["max"], env))
    if mx < 0 or mx > 256:
        raise ShapeErr("E_REPEAT_BOUND", "repeat max must be within 0..256",
                       observed=mx, expected="0..256")
    cnt = to_int(ev(sh["count"], env))
    if cnt < 0 or cnt > mx:
        raise ShapeErr("E_REPEAT_BOUND", "repeat count must be within 0..max",
                       observed=cnt, expected=f"0..{mx}")
    var = sh["var"]
    step_x = to_int(ev(sh.get("step_x", 0), env))
    step_y = to_int(ev(sh.get("step_y", 0), env))
    child_g = dict(g)
    if g["mode"] == "recipe":
        # children inherit anchor/layer/fill from the repeat itself
        child_g["anchor"] = sh.get("anchor")
        child_g["layer"] = sh.get("layer")
        child_g["fill"] = sh.get("fill")
    out = []
    n0 = len(g["diags"])
    for i in range(cnt):
        cenv = dict(env)
        cenv[var] = i
        cenv["i"] = i
        for d in compile_shapes(sh["shapes"], cenv, child_g, f"{path}/shapes", depth + 1):
            out.append(shift_shape(d, i * step_x, i * step_y))
    # a broken child compiles once per iteration: report each distinct
    # diagnostic once instead of count times
    tail, seen = [], set()
    for d in g["diags"][n0:]:
        key = json.dumps(d, sort_keys=True)
        if key not in seen:
            seen.add(key)
            tail.append(d)
    g["diags"][n0:] = tail
    return out


def build_one(sh, env, g, path, depth):
    """Compile one shape into drawables (part stamped; seq/src left to the
    caller's add()). Raises ShapeErr or ExprError; returns [] when `when`
    gates the shape off or a px file failed (already reported)."""
    kind = sh["shape"]
    if "when" in sh and not ev(sh["when"], env):
        return []
    if "mirror" in sh and "rotate" in sh:
        raise ShapeErr("E_SHAPE_PARAM", "mirror cannot be combined with rotate")
    if kind == "repeat":
        if g["mode"] == "recipe":
            # children inherit these; validate them even when count is 0
            shape_layer(sh, g)
            shape_anchor(sh, g)
        out = build_repeat(sh, env, g, path, depth)
        # no part stamping: every drawable already carries its own part
    else:
        li = g["layers"].index(shape_layer(sh, g))
        if g["mode"] == "recipe":
            a = g["anchors"][shape_anchor(sh, g)]
            at, part = a["at"], a["part"]
        else:
            part = g["part"]
            at = None
            if "anchor" in sh:            # archetype px fragment at an anchor
                a = g["anchors"][shape_anchor(sh, g)]
                at, part = a["at"], a["part"]
        if kind == "px":
            out = build_px(sh, g, at, li, path)
        else:
            fill = sh.get("fill", g.get("fill"))
            if fill is None:
                raise ShapeErr("E_SHAPE_PARAM",
                               "shape has no fill and its repeat declares none")
            if kind == "line":
                out = [build_line(sh, env, at, li, fill, g["ramps"], g.get("swap"))]
            else:
                if "fill" not in sh:
                    sh = {**sh, "fill": fill}   # inherited from the repeat
                d = build_shape(sh, env, at, li, g["ramps"], g.get("swap"))
                out = [d] if d else []
        for d in out:
            d["part"] = part
    if sh.get("mirror") == "x":
        # also draw the exact integer reflection (spec 02 §C); reflect_shape
        # rebinds box/px/pts, so a shallow copy leaves the original intact
        out += [reflect_shape(dict(d), g["axis"]) for d in out]
    return out


def compile_shapes(shapes, env, g, path, depth=0):
    """Compile a shape list. ShapeErr is reported per shape and the rest of
    the list still compiles; ExprError/ValueError propagate to the caller's
    existing handler (E_ARCHETYPE_EXPR / E_RECIPE_EXPR)."""
    out = []
    for i, sh in enumerate(shapes):
        try:
            out.extend(build_one(sh, env, g, f"{path}/{i}", depth))
        except ShapeErr as e:
            g["diags"].append(e.diag(f"{path}/{i}"))
    return out


def frame_state_key(fr):
    """Content key of a frame's 02 §F geometry state (`params` tracks +
    `swap`), or None when the frame draws the base geometry. sort_keys makes
    the key byte-deterministic; compile builds one drawable set per distinct
    key, render picks it back up."""
    track, smap = fr.get("params") or {}, fr.get("swap") or {}
    if not track and not smap:
        return None
    return json.dumps({"params": track, "swap": smap}, sort_keys=True, separators=(",", ":"))


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
    # ---- palette_swap (02 §E): ramp-name map applied when fills resolve;
    # both ends must exist in the style pack ------------------------------
    swap = asset.get("palette_swap") or {}
    for k in sorted(swap):
        for who, name in (("source", k), ("target", swap[k])):
            if name not in ramps:
                diags.append(D("E_UNKNOWN_RAMP", ptr("palette_swap", k),
                               f"palette_swap {who} ramp '{name}' does not exist in the style pack",
                               observed=name, expected=sorted(ramps),
                               suggestions=close(name, sorted(ramps)),
                               fix=[{"op": "remove", "path": ptr("palette_swap", k)}]))
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

    # ---- mirror axis (2.1 §C): archetype `mirror_axis`, else $cx, else
    # floor($W/2) — the same fallback the symmetry check uses ---------------
    axis_name = arch.get("mirror_axis", "cx")
    axis_val = env.get(axis_name)
    if isinstance(axis_val, int) and not isinstance(axis_val, bool):
        axis = axis_val
    elif "mirror_axis" in arch:
        axis_keys = sorted(env)
        diags.append(D("E_ARCHETYPE_EXPR", ptr("archetype", "mirror_axis"),
                       f"mirror_axis '{axis_name}' is not an integer metric or parameter",
                       observed=axis_name, expected=axis_keys,
                       suggestions=close(axis_name, axis_keys)))
        axis = W // 2
    else:
        axis = W // 2

    # archetype body (2.1 §C: mirror / line / repeat / px shapes)
    g_arch = {"mode": "archetype", "ramps": ramps, "swap": swap, "axis": axis, "layers": layers,
              "base_dir": base_dir, "diags": diags, "anchors": anchors, "part": None}
    try:
        for part, pdef in arch["parts"].items():
            g_arch["part"] = part
            for d in compile_shapes(pdef["shapes"], env, g_arch,
                                    ptr("archetype", "parts", part, "shapes")):
                add(d, d["part"], f"archetype:{part}")
    except (ExprError, ValueError) as e:
        diags.append(D("E_ARCHETYPE_EXPR", "/archetype", f"archetype geometry failed: {e}"))

    # features
    g_rec = {"mode": "recipe", "ramps": ramps, "swap": swap, "axis": axis, "layers": layers,
             "base_dir": base_dir, "diags": diags, "anchors": anchors}
    used_slots = {}
    feature_names = []
    used_recs = []
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
        # geometry is either one array shared by every applies_to member or an
        # object keyed by archetype name (2.1); a keyed recipe must cover this
        # archetype or it contributes nothing here.
        geo = rec["geometry"]
        if isinstance(geo, dict):
            geo = geo.get(arch["archetype"])
            if geo is None:
                diags.append(D("E_RECIPE_GEOMETRY", fp,
                               f"recipe '{name}' has no geometry keyed for archetype '{arch['archetype']}'",
                               observed=sorted(rec["geometry"]), expected=rec["applies_to"],
                               suggestions=close(arch["archetype"], rec["geometry"])))
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
        used_recs.append((fp, name, rec, given))
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
        gpath = fp + "/geometry" + (ptr(arch["archetype"]) if isinstance(rec["geometry"], dict) else "")
        try:
            for d in compile_shapes(geo, renv, g_rec, gpath):
                add(d, d["part"], f"feature:{name}")
            for hb in rec.get("hitboxes", []):
                a = anchors[hb["anchor"]]
                x, y, w, h = (to_int(ev(v, renv)) for v in hb["rect"])
                hitboxes.append({"id": hb["id"], "rect": [a["at"][0] + x, a["at"][1] + y, w, h],
                                 "part": a["part"], "active_in": hb["active_in"]})
        except ExprError as e:
            diags.append(D("E_RECIPE_EXPR", fp, f"recipe '{name}' failed to evaluate: {e}"))

    # custom px fragments (env-independent: a frame state copies them whole)
    custom_start = len(drawables)
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
            diags.append(D(e.code, cp + "/file", f"{c['file']} line {e.line}: {e.msg}"))
            continue
        frame = c.get("frame", 0)
        if not 0 <= frame < px["frame_count"]:
            diags.append(D("E_PX_FRAMES", cp + "/frame",
                           f"fragment '{c['file']}' has {px['frame_count']} frame(s), requested frame {frame}",
                           observed=frame, expected=f"0..{px['frame_count'] - 1}"))
            continue
        legend = {}
        bad = False
        for ch, val in px["legend"].items():
            if val == "transparent":
                continue
            try:
                legend[ch] = resolve_fill(val, {}, ramps, swap)
            except ExprError as e:
                diags.append(D("E_PX_LEGEND", cp + "/file", f"{c['file']}: legend '{ch}': {e}"))
                bad = True
        if bad:
            continue
        a = anchors[c["anchor"]]
        pxs = []
        fax, fay = px["anchors"]["attach"]
        rows = px["rows"] if px["version"] == 1 else px["frames"][frame]
        for r, (_, row) in enumerate(rows):
            for col, ch in enumerate(row):
                if ch in legend:
                    pxs.append((col - fax, r - fay, legend[ch]))
        add({"kind": "px", "at": tuple(a["at"]), "px": pxs, "li": layers.index(c["layer"])}, a["part"], f"custom:{c['id']}")

    # animations
    anims = {}
    bound = {}
    for name, a in asset.get("animations", {}).items():
        tpl = arch["animations"].get(a["template"])
        if not tpl:
            diags.append(D("E_UNKNOWN_ANIMATION_TEMPLATE", ptr("animations", name, "template"),
                           f"archetype has no animation template '{a['template']}'",
                           expected=sorted(arch["animations"]), suggestions=close(a["template"], arch["animations"])))
            continue
        anims[name] = tpl
        bound.setdefault(a["template"], tpl)

    # ---- 02 §F: tracks/swap validated on the templates this asset binds ---
    # (a template nothing names is never compiled — plan gap 8)
    TRACK_KINDS = ("int", "bool", "enum", "percent")
    overridable = set(size_specs)
    overridable |= {k for k, s in arch.get("params", {}).items() if s.get("type") in TRACK_KINDS}
    rec_kinds = {}
    for _, rname, rec, _ in used_recs:
        for k, s in rec["parameters"].items():
            rec_kinds.setdefault(k, []).append((rname, s.get("type")))
    for k, who in sorted(rec_kinds.items()):
        if all(t in TRACK_KINDS for _, t in who):
            overridable.add(k)
    for tname, tpl in bound.items():
        for fi, fr in enumerate(tpl["frames"]):
            tpath = ptr("archetype", "animations", tname, "frames", fi)
            for k in sorted(fr.get("params") or {}):
                if k not in overridable:
                    diags.append(D("E_TRACK_PARAM", tpath + ptr("params", k),
                                   f"frame track '{k}' is not an overridable parameter of this archetype",
                                   observed=k, expected=sorted(overridable),
                                   suggestions=close(k, overridable),
                                   fix=[{"op": "remove", "path": tpath + ptr("params", k)}]))
            for part in sorted(fr.get("swap") or {}):
                sset = fr["swap"][part]
                spath = tpath + ptr("swap", part)
                if part not in arch["parts"]:
                    diags.append(D("E_UNKNOWN_PART", spath, f"archetype has no part '{part}'",
                                   observed=part, expected=sorted(arch["parts"]),
                                   suggestions=close(part, arch["parts"]),
                                   fix=[{"op": "remove", "path": spath}]))
                    continue
                vs = sorted(arch["parts"][part].get("variants", {}))
                if sset not in vs:
                    sug = close(sset, vs)
                    diags.append(D("E_UNKNOWN_SWAP_SET", spath,
                                   f"part '{part}' has no variant set '{sset}'",
                                   observed=sset, expected=vs, suggestions=sug,
                                   fix=([{"op": "replace", "path": spath, "value": sug[0]}] if len(sug) == 1
                                        else [{"op": "remove", "path": spath}])))

    if any(d["severity"] == "error" for d in diags):
        return diags, None

    # ---- 02 §F: per-frame geometry states ---------------------------------
    # A frame with `params`/`swap` draws its own geometry: the env (size,
    # class/proportions, parameters, metrics) is re-derived at the track's
    # values and part shape lists are swapped; feature geometry is rebuilt in
    # that env. Anchors, hitboxes and part offsets stay at their base values
    # (the atlas exports them once), and recipe `sizes` spans are an
    # asset-level contract checked once at the chosen size. States build here,
    # at compile time, so every failure reports with a path.
    custom_drawables = drawables[custom_start:]

    def track_env(track, tpath):
        """Re-derive the expression env at a frame's track values. Size tracks
        clamp silently (02 §F); archetype track values validate like supplied
        values; recipe track values are applied per feature by make_state.
        Returns (fenv, recipe_track)."""
        fsize = dict(size_env)
        arch_track, rec_track = {}, {}
        for k in sorted(track):
            v = track[k]
            if k in size_specs:
                if isinstance(v, bool) or not isinstance(v, int):
                    diags.append(D("E_TRACK_PARAM", tpath + ptr("params", k),
                                   f"size track '{k}' must be an integer",
                                   observed=v, expected="integer"))
                    continue
                spec = size_specs[k]
                fsize[k] = max(spec["min"], min(spec["max"], v))     # clamped silently
            elif k in arch["params"]:
                arch_track[k] = v
            else:
                rec_track[k] = v
        fmenv = {"W": W, "H": H}
        fmenv.update(fsize)
        classes = arch.get("classes", [])
        cls_name, cls_idx = "default", 0
        if size_specs and classes:
            primary = next(iter(size_specs))
            sv = fsize[primary]
            cands = [c for c in classes if sv <= c["max"]]
            pick = min(cands, key=lambda c: c["max"]) if cands else max(classes, key=lambda c: c["max"])
            cls_name, cls_idx = pick["name"], classes.index(pick)
        fmenv["class"], fmenv["class_idx"] = cls_name, cls_idx
        row = arch.get("proportions", {}).get(cls_name)
        if isinstance(row, dict):
            fmenv.update(row)
        given = dict(asset["archetype"].get("params", {}))
        given.update(arch_track)
        fparams = check_params(arch["params"], given, tpath + ptr("params"), style, diags,
                               "frame track", env=fmenv)
        fenv = dict(fmenv)
        fenv.update(fparams)
        for mname, mexpr in arch.get("metrics", []):
            try:
                fenv[mname] = to_int(ev(mexpr, fenv))
            except (ExprError, ValueError) as e:
                diags.append(D("E_ARCHETYPE_EXPR", tpath + ptr("params"),
                               f"metric '{mname}' failed to evaluate in the frame state: {e}"))
                fenv[mname] = 0
        return fenv, rec_track

    def make_state(track, smap, tpath):
        """The drawable set for one (params, swap) frame state."""
        fenv, frec = track_env(track, tpath)
        # mirror axis re-derived at this state's env (the name was validated
        # at base; integer by construction, silent fallback otherwise)
        axis_name = arch.get("mirror_axis", "cx")
        av = fenv.get(axis_name)
        faxis = av if isinstance(av, int) and not isinstance(av, bool) else W // 2
        out = []

        def sadd(d, src):
            d.update({"seq": len(out), "src": src})
            out.append(d)

        g = {"mode": "archetype", "ramps": ramps, "swap": swap, "axis": faxis,
             "layers": layers, "base_dir": base_dir, "diags": diags,
             "anchors": anchors, "part": None}
        for part, pdef in arch["parts"].items():
            if part in smap:
                shapes = pdef["variants"][smap[part]]
                gpath = ptr("archetype", "parts", part, "variants", smap[part])
            else:
                shapes = pdef["shapes"]
                gpath = ptr("archetype", "parts", part, "shapes")
            g["part"] = part
            try:
                for d in compile_shapes(shapes, fenv, g, gpath):
                    sadd(d, f"archetype:{part}")
            except (ExprError, ValueError) as e:
                diags.append(D("E_ARCHETYPE_EXPR", tpath,
                               f"archetype geometry failed in the frame state: {e}"))
        g2 = {"mode": "recipe", "ramps": ramps, "swap": swap, "axis": faxis,
              "layers": layers, "base_dir": base_dir, "diags": diags,
              "anchors": anchors}
        for fp, rname, rec, given in used_recs:
            rgiven = dict(given)
            rgiven.update({k: v for k, v in frec.items() if k in rec["parameters"]})
            before = len(diags)
            rparams = check_params(rec["parameters"], rgiven, tpath + ptr("params"),
                                   style, diags, f"frame track of recipe '{rname}'", env=fenv)
            if len(diags) > before:
                continue
            renv = dict(fenv)
            renv.update(rparams)
            geo = rec["geometry"]
            if isinstance(geo, dict):
                geo = geo.get(arch["archetype"])
            gpath = fp + "/geometry" + (ptr(arch["archetype"]) if isinstance(rec["geometry"], dict) else "")
            try:
                for lname, lexpr in rec.get("locals", []):
                    renv[lname] = to_int(ev(lexpr, renv))
            except (ExprError, ValueError) as e:
                diags.append(D("E_RECIPE_EXPR", tpath,
                               f"recipe '{rname}' local failed in the frame state: {e}"))
                continue
            try:
                for d in compile_shapes(geo, renv, g2, gpath):
                    sadd(d, f"feature:{rname}")
            except (ExprError, ValueError) as e:
                diags.append(D("E_RECIPE_EXPR", tpath,
                               f"recipe '{rname}' failed to evaluate in the frame state: {e}"))
        # custom fragments are env-independent; renumber so (li, seq) keeps
        # the base painter order (archetype, features, custom)
        out.extend(dict(d) for d in custom_drawables)
        for i, d in enumerate(out):
            d["seq"] = i
        return out

    states = {}
    for tname, tpl in bound.items():
        for fi, fr in enumerate(tpl["frames"]):
            key = frame_state_key(fr)
            if key is None or key in states:
                continue
            states[key] = make_state(fr.get("params") or {}, fr.get("swap") or {},
                                     ptr("archetype", "animations", tname, "frames", fi))
    if any(d["severity"] == "error" for d in diags):
        return diags, None
    return diags, {
        "asset": asset, "arch": arch, "style": style, "W": W, "H": H, "drawables": drawables,
        "anchors": anchors, "parts": parts, "features": feature_names, "animations": anims, "hitboxes": hitboxes,
        "auto": auto, "min_cell": mc, "clip_policy": asset["target"].get("clip_policy", "error"),
        "env": env, "states": states,
    }


# --------------------------------------------------------------------------
# Rasterizer
# --------------------------------------------------------------------------
def part_offset(offsets, part):
    rx, ry = offsets.get("root", (0, 0))
    px, py = offsets.get(part, (0, 0)) if part != "root" else (0, 0)
    return rx + px, ry + py


def rotf(x, y, deg):
    """Rotate a 1/64-px vector by whole degrees. Integer only; floor division."""
    c, s = COS[deg], SIN[deg]
    return (x * c - y * s + 500000) // 1000000, (x * s + y * c + 500000) // 1000000


def part_rot(rots, part):
    r = (rots or {}).get(part)
    return r if r and r[0] else None


def rot_pt(X, Y, r):
    """Rotate fixed-point (1/64 px) point about whole-pixel pivot r = (deg, px, py)."""
    dx, dy = rotf(X - r[1] * 64, Y - r[2] * 64, r[0])
    return r[1] * 64 + dx, r[2] * 64 + dy


# Volume shading (style pack "shading"). All integer: normals are scaled by 1024,
# the light vector is ~unit*1024, tone position t is in 1/1000. Floor division.
LIGHT = (-606, -660, 496)
DITH = (-45, 15, 45, -15)          # 2x2 Bayer, indexed (y&1)*2+(x&1)


def shade_pick(style, tones, nx, ny, x, y):
    nz = math.isqrt(max(0, 1024 * 1024 - nx * nx - ny * ny))
    sh = style.get("shading", {})
    lx, ly, lz = sh.get("light", LIGHT)
    dot = (nx * lx + ny * ly + nz * lz) // 1024
    t = ((dot + 460) * 1000) // 1485
    if sh.get("dither") == "bayer2":
        t += DITH[(y & 1) * 2 + (x & 1)]
    n = len(tones)
    return tones[max(0, min(n - 1, (t * n) // 1000))]


def render_frame(ir, offsets, clip=True, drawables=None, rots=None):
    """Rasterize one frame. clip=False renders on an unbounded plane (auto
    canvas pass 1): nothing is dropped, `clipped` stays 0. `drawables`
    overrides the base set with a 02 §F frame state's geometry."""
    W, H = ir["W"], ir["H"]
    ramps = ir["style"]["ramps"]
    tl = {r: [t for t in TONES if t in v] for r, v in ramps.items()}
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

    ds = ir["drawables"] if drawables is None else drawables
    for d in sorted(ds, key=lambda d: (d["li"], d["seq"])):
        ox, oy = part_offset(offsets, d["part"])
        rot = part_rot(rots, d["part"])      # joint rotation about a pivot, applied before the offset
        fill = d.get("fill")
        shaded = fill is not None and fill[1] == "*"
        if d["kind"] == "px":
            ax, ay = d["at"]
            if rot:
                X, Y = rot_pt(ax * 64, ay * 64, rot)
                ax, ay = (X + 32) // 64, (Y + 32) // 64
            for dx, dy, f in d["px"]:
                put(ax + ox + dx, ay + oy + dy, d["li"], d["seq"], d["part"], f)
        elif d["kind"] == "ellipse":
            x, y, w, h = d["box"]
            if rot:                          # the centre rotates, the (axis aligned) shape does not
                X, Y = rot_pt((2 * x + w) * 32, (2 * y + h) * 32, rot)
                x, y = ((X + 16) // 32 - w) // 2, ((Y + 16) // 32 - h) // 2
            x, y = x + ox, y + oy
            for py in range(y, y + h):
                for px in range(x, x + w):
                    dx = (2 * px + 1) - (2 * x + w)
                    dy = (2 * py + 1) - (2 * y + h)
                    if dx * dx * h * h + dy * dy * w * w <= w * w * h * h:
                        f = (fill[0], shade_pick(ir["style"], tl[fill[0]], dx * 1024 // w, dy * 1024 // h, px, py)) if shaded else fill
                        put(px, py, d["li"], d["seq"], d["part"], f)
        elif d["kind"] == "capsule":
            x0, y0, x1, y1 = d["p"]
            if rot:
                x0, y0 = rot_pt(x0, y0, rot)
                x1, y1 = rot_pt(x1, y1, rot)
            x0, x1, y0, y1 = x0 + ox * 64, x1 + ox * 64, y0 + oy * 64, y1 + oy * 64
            hw = d["hw"]
            sx, sy = x1 - x0, y1 - y0
            l2 = sx * sx + sy * sy
            for py in range((min(y0, y1) - hw) // 64, (max(y0, y1) + hw) // 64 + 2):
                for px in range((min(x0, x1) - hw) // 64, (max(x0, x1) + hw) // 64 + 2):
                    cx, cy = px * 64 + 32, py * 64 + 32
                    t = 0 if l2 == 0 else (cx - x0) * sx + (cy - y0) * sy
                    if d["flat"] and not 0 <= t <= l2:      # caps "flat": no round end beyond the cap centres
                        continue
                    t = 0 if l2 == 0 else max(0, min(l2, t))
                    qx, qy = (x0, y0) if l2 == 0 else (x0 + (sx * t) // l2, y0 + (sy * t) // l2)
                    ex, ey = cx - qx, cy - qy
                    if ex * ex + ey * ey <= hw * hw:
                        f = (fill[0], shade_pick(ir["style"], tl[fill[0]], ex * 1024 // hw, ey * 1024 // hw, px, py)) if shaded else fill
                        put(px, py, d["li"], d["seq"], d["part"], f)
        else:
            pts = [(fx + ox * 64, fy + oy * 64) for fx, fy in d["pts"]]
            if rot:
                pts = [rot_pt(px_, py_, rot) for px_, py_ in pts]
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
    states = ir.get("states") or {}

    def mk(offsets, ds=None, rots=None):
        grid, clipped = render_frame(ir, offsets, clip=not auto, drawables=ds, rots=rots)
        return {"grid": grid, "rgba": None, "clipped": clipped, "offsets": offsets, "rots": rots or {}}

    entries = [("base", 0, mk({}))]
    for name, tpl in ir["animations"].items():
        for i, fr in enumerate(tpl["frames"]):
            key = frame_state_key(fr)
            ds = states[key] if key is not None else None
            entries.append((name, i, mk({k: tuple(v) for k, v in fr["offsets"].items()}, ds,
                                        {k: tuple(v) for k, v in fr.get("rotate", {}).items()})))
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
        # ---- spec-next 02 §H (X1) ----------------------------------------
        elif a == "baseline_stable":
            an = ir["anchors"].get(t.get("anchor"))
            if an is None:
                res(i, t, False, t.get("anchor"), sorted(ir["anchors"]), "unknown anchor")
                continue
            ys = [an["at"][1] + part_offset(f["offsets"], an["part"])[1] for f in frames]
            spread = max(ys) - min(ys)
            res(i, t, spread <= t["max"], spread, f"<= {t['max']}",
                f"anchor '{t.get('anchor')}' floats {spread}px between frames (feet must not float)")
        elif a == "pivot_stable":
            org = ir["anchors"].get("origin")
            if org is None:
                res(i, t, False, "origin", sorted(ir["anchors"]), "no 'origin' anchor")
                continue
            bx, by = org["at"]
            disp = 0
            for f in frames:
                ox, oy = part_offset(f["offsets"], org["part"])
                disp = max(disp, abs(org["at"][0] + ox - bx), abs(org["at"][1] + oy - by))
            res(i, t, disp <= t["max"], disp, f"<= {t['max']}",
                "the origin anchor drifts from its base-frame position")
        elif a == "min_feature":
            # `detail` (02 §E `affects: ["detail"]`) is documentation-only — no
            # runtime marker exists — so every compiled shape counts (base
            # geometry plus every frame state).
            dims = []
            for pool in [ir["drawables"]] + list((ir.get("states") or {}).values()):
                for d in pool:
                    w, h = shape_extent(d)
                    if w and h:
                        dims.append(min(w, h))
            obs = min(dims) if dims else 0
            res(i, t, obs >= t["min"], obs, f">= {t['min']}",
                "a compiled shape is thinner than the minimum (all shapes count: the 'detail' marker is documentation-only)")
        elif a == "cell_fill":
            ratios = [len(f["grid"]) / (W * H) for f in frames]
            lo, hi = round(min(ratios), 4), round(max(ratios), 4)
            res(i, t, lo >= t.get("min", 0) and hi <= t.get("max", 1), [lo, hi],
                f"{t.get('min', 0)}..{t.get('max', 1)}", "cell opacity ratio outside the declared band")
        elif a == "seamless":
            # tileable borders must be opaque and free of outline pixels,
            # in every rendered frame (02 §H)
            olr = ir["style"]["outline"]["ramp"]
            violations = 0
            for f in frames:
                g = f["grid"]
                cells = []
                if "x" in t["axes"]:
                    cells += [(0, y) for y in range(H)] + [(W - 1, y) for y in range(H)]
                if "y" in t["axes"]:
                    cells += [(x, 0) for x in range(W)] + [(x, H - 1) for x in range(W)]
                for c in cells:
                    v = g.get(c)
                    if v is None or v[3] == olr:
                        violations += 1
            res(i, t, violations == 0, violations, "0",
                "tileable borders carry transparent or outline pixels")
        elif a == "jitter_max":
            # centre-of-mass step between consecutive frames, per animation
            worst = 0
            for name, fs in renders.items():
                if name == "base":
                    continue
                coms = []
                for f in fs:
                    n = len(f["grid"])
                    if n:
                        coms.append((sum(x for x, _ in f["grid"]) // n, sum(y for _, y in f["grid"]) // n))
                for pa, pb in zip(coms, coms[1:]):
                    worst = max(worst, abs(pb[0] - pa[0]), abs(pb[1] - pa[1]))
            res(i, t, worst <= t["max"], worst, f"<= {t['max']}",
                "centre of mass jumps between consecutive frames")
        elif a == "orphan_pixels":
            worst, where = 0, None
            for name, fs in renders.items():
                for fi, f in enumerate(fs):
                    g = f["grid"]
                    c = sum(1 for x, y in g
                            if all((x + dx, y + dy) not in g for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))))
                    if c > worst:
                        worst, where = c, (name, fi)
            res(i, t, worst <= t["max"], worst, f"<= {t['max']}",
                f"isolated single pixel(s) in frame {where}")

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
    if d["kind"] == "capsule":      # bounding box of the swept disc (an upper bound for flat caps)
        x0, y0, x1, y1 = d["p"]
        return (abs(x1 - x0) + 2 * d["hw"]) // 64, (abs(y1 - y0) + 2 * d["hw"]) // 64
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


# --------------------------------------------------------------------------
# Derived assets (spec-next 02 §E): extends, variants, palette_swap
# --------------------------------------------------------------------------
EXTENDS_MAX_HOPS = 3


def merge_patch(base, patch):
    """RFC 7386 JSON Merge Patch: objects merge recursively, null deletes a
    key, everything else (arrays included) replaces whole. Documents are
    never mutated in place, so shared subtrees are safe."""
    if not isinstance(base, dict) or not isinstance(patch, dict):
        return patch
    out = dict(base)
    for k, v in patch.items():
        if v is None:
            out.pop(k, None)
        else:
            out[k] = merge_patch(out[k], v) if k in out else v
    return out


def resolve_extends(doc, file_path, diags):
    """Follow the `extends` chain (02 §E): the path is relative to each
    extending file, must stay inside the repo root, at most
    EXTENDS_MAX_HOPS links, no file visited twice. Returns the merged base
    document, or None when a diagnostic was reported. All diagnostics are
    reported at /extends of the file the command was given."""
    cur, cur_file = doc, Path(file_path).resolve()
    visited = {cur_file}
    hops = 0
    while isinstance(cur, dict) and "extends" in cur:
        ref = cur["extends"]
        if not isinstance(ref, str):
            diags.append(D("E_SCHEMA", "/extends", "'extends' must be a string",
                           observed=type(ref).__name__, expected="string"))
            return None
        target = (cur_file.parent / ref).resolve()
        where = f" (at '{cur_file.name}')" if cur_file != Path(file_path).resolve() else ""
        if target in visited:
            diags.append(D("E_EXTENDS_CYCLE", "/extends",
                           f"extends chain revisits '{ref}'{where}: cycle",
                           observed=ref, expected="a file not already in the chain",
                           fix=[{"op": "remove", "path": "/extends"}]))
            return None
        if hops >= EXTENDS_MAX_HOPS:
            diags.append(D("E_EXTENDS_DEPTH", "/extends",
                           f"extends chain exceeds {EXTENDS_MAX_HOPS} links{where}",
                           observed=hops + 1, expected=f"<= {EXTENDS_MAX_HOPS}",
                           fix=[{"op": "remove", "path": "/extends"}]))
            return None
        root = ROOT.resolve()
        if target != root and root not in target.parents:
            diags.append(D("E_EXTENDS_PATH", "/extends",
                           f"extends target '{ref}' escapes the repo root{where}",
                           observed=ref, expected=f"a path inside {root}",
                           fix=[{"op": "remove", "path": "/extends"}]))
            return None
        if not target.is_file():
            diags.append(D("E_EXTENDS_PATH", "/extends",
                           f"extends target '{ref}' not found{where}",
                           observed=ref, expected="an existing asset file",
                           fix=[{"op": "remove", "path": "/extends"}]))
            return None
        try:
            parent = json.loads(target.read_text())
        except (OSError, json.JSONDecodeError) as e:
            diags.append(D("E_EXTENDS_PATH", "/extends",
                           f"extends target '{ref}' cannot be read: {e}{where}",
                           observed=ref, expected="a readable JSON asset"))
            return None
        if not (isinstance(parent, dict) and "arcforge" in parent):
            diags.append(D("E_EXTENDS_PATH", "/extends",
                           f"extends target '{ref}' is not an asset document{where}",
                           observed=ref, expected="an asset document"))
            return None
        if "extends" not in parent:
            # a complete document is checked here; a patch's own fields are
            # checked on the merged result by compile_asset
            bad = schema_diags(parent, "asset.schema.json")
            if bad:
                diags.append(D("E_EXTENDS_PATH", "/extends",
                               f"extends target '{ref}' is invalid: {bad[0]['code']} at {bad[0]['path'] or '/'}: {bad[0]['message']}{where}",
                               observed=ref, expected="a valid asset document"))
                return None
        own = {k: v for k, v in cur.items() if k != "extends"}
        cur = merge_patch(parent, own)
        cur_file = target
        hops += 1
    return cur


def expand_variants(base, diags):
    """02 §E `variants`: ordered entries composed from the base document (or
    an earlier entry) plus their RFC-7386 `override`. Returns
    [(id, doc, path_prefix)] — base first with prefix '', entry i with
    '/variants/<i>' — or None when a diagnostic was reported. Diagnostics
    compiled from a prefixed doc are reported under that file pointer."""
    entries = base.get("variants") or []
    base_doc = {k: v for k, v in base.items() if k != "variants"}
    base_id = base_doc["asset"]["id"]
    out = [(base_id, base_doc, "")]
    known = {base_id: base_doc}
    for i, e in enumerate(entries):
        p = f"/variants/{i}"
        if not isinstance(e, dict) or not isinstance(e.get("id"), str):
            diags.append(D("E_SCHEMA", p, "'id' is a required property",
                           expected="string"))
            continue
        vid = e["id"]
        if "from" in e and not isinstance(e["from"], str):
            diags.append(D("E_SCHEMA", p + "/from", "'from' must be a string",
                           observed=type(e["from"]).__name__, expected="string"))
            continue
        if "override" in e and not isinstance(e["override"], dict):
            diags.append(D("E_SCHEMA", p + "/override", "'override' must be an object",
                           observed=type(e["override"]).__name__, expected="object"))
            continue
        if vid in known:
            diags.append(D("E_DUPLICATE_ID", p + "/id",
                           f"compiled id '{vid}' is already used in this file",
                           observed=vid, expected=sorted(known),
                           fix=[{"op": "remove", "path": p}]))
            continue
        if "from" in e:
            if e["from"] not in known:
                diags.append(D("E_UNKNOWN_VARIANT", p + "/from",
                               f"no variant '{e['from']}' earlier in this file to build from",
                               observed=e["from"], expected=sorted(known),
                               suggestions=close(e["from"], sorted(known)),
                               fix=[{"op": "remove", "path": p + "/from"}]))
                continue
            src = known[e["from"]]
        else:
            src = base_doc
        doc = merge_patch(src, e.get("override") or {})
        if "palette_swap" in e:
            doc = dict(doc)
            doc["palette_swap"] = merge_patch(doc.get("palette_swap") or {}, e["palette_swap"])
        doc = dict(doc)
        doc["asset"] = dict(doc.get("asset") or {}, id=vid)
        doc.pop("extends", None)
        doc.pop("variants", None)
        out.append((vid, doc, p))
        known[vid] = doc
    return out


def load_asset_docs(path):
    """Load an asset file and expand `extends` + `variants` (02 §E).
    Returns (docs, diags, base_dir); docs is empty when a file-level
    diagnostic was reported. A document with `extends` is a patch: its
    required fields may come from the target, so the raw schema check runs
    only for documents without one — types and unknown keys of a patch are
    caught on the merged document by compile_asset, and expansion reports
    its own diagnostics first."""
    doc, base = load_asset(path)
    if "extends" not in doc:
        raw = schema_diags(doc, "asset.schema.json")
        if raw:
            return [], raw, base
    diags = []
    merged = resolve_extends(doc, path, diags)
    if merged is None:
        return [], diags, base
    docs = expand_variants(merged, diags)
    if diags:
        return [], diags, base
    return docs, [], base


def prefixed(diags, pfx):
    """Report a variant's diagnostics under its file pointer /variants/<i>."""
    return [dict(d, path=pfx + d["path"]) for d in diags] if pfx else diags


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


def is_recipe_doc(doc):
    return isinstance(doc, dict) and "recipe" in doc and "geometry" in doc and "arcforge" not in doc


def check_recipe_doc(doc, reg, base_path=""):
    """Schema + keyed-geometry cross-checks for a recipe document.

    An array geometry is shared by every applies_to member. An object geometry
    is keyed by archetype name and must cover applies_to exactly: a missing key
    would silently drop the recipe on that archetype, an extra key can never be
    reached. Both are E_RECIPE_GEOMETRY."""
    diags = schema_diags(doc, "recipe.schema.json", base_path)
    if diags:
        return diags
    geo = doc.get("geometry")
    if not isinstance(geo, dict):
        return diags
    applies = doc.get("applies_to", [])
    for a in applies:
        if a not in geo:
            sug = close(a, geo)
            diags.append(D("E_RECIPE_GEOMETRY", base_path + "/geometry",
                           f"recipe '{doc.get('recipe', '?')}' has no geometry keyed for applies_to archetype '{a}'",
                           observed=sorted(geo), expected=a, suggestions=sug,
                           fix=([{"op": "add", "path": base_path + "/geometry" + ptr(sug[0]),
                                  "value": geo[sug[0]]}] if sug else None)))
    for k in geo:
        if k not in applies:
            diags.append(D("E_RECIPE_GEOMETRY", base_path + "/geometry" + ptr(k),
                           f"geometry key '{k}' is not in applies_to",
                           observed=k, expected=applies, suggestions=close(k, applies),
                           fix=[{"op": "remove", "path": base_path + "/geometry" + ptr(k)}]))
    return diags


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
    elif is_recipe_doc(doc):
        diags = check_recipe_doc(doc, reg)
        r = report("validate", doc.get("recipe", "?"), diags)
    else:
        docs, xdiags, base = load_asset_docs(args.asset)
        if xdiags:
            r = report("validate", asset_id(doc), xdiags)
        else:
            diags, ids = [], []
            for aid, d2, pfx in docs:
                d3, _ = compile_asset(d2, reg, base)
                diags += prefixed(d3, pfx)
                ids.append(aid)
            extra = {"stats": {"ids": ids}} if len(ids) > 1 else {}
            r = report("validate", asset_id(doc), diags, **extra)
    print(json.dumps(r, indent=2))
    return 0 if r["status"] == "PASS" else 1


def cmd_render(args):
    reg = load_registry()
    docs, xdiags, base = load_asset_docs(args.asset)
    if xdiags:
        doc, _ = load_asset(args.asset)
        print(json.dumps(report("validate", asset_id(doc), xdiags), indent=2))
        return 1
    compiled = []
    fail = []
    ids = []
    for aid, d2, pfx in docs:
        diags, ir = compile_asset(d2, reg, base)
        fail += prefixed(diags, pfx)
        ids.append(aid)
        if ir is None:
            continue
        compiled.append((aid, d2, pfx, ir))
    if len(compiled) != len(docs):
        doc, _ = load_asset(args.asset)
        print(json.dumps(report("validate", asset_id(doc), fail), indent=2))
        return 1
    stats = {}
    total = 0
    for aid, d2, pfx, ir in compiled:
        renders = render_all(ir)
        out = Path(args.out) / aid
        out.mkdir(parents=True, exist_ok=True)
        W, H, s = ir["W"], ir["H"], args.scale
        (out / "base.png").write_bytes(png_bytes(W, H, renders["base"][0]["rgba"]))
        atlas = {"cell": [W, H],
                 # 02 §F: art is authored facing right; a game flips left at runtime
                 "facing": "right",
                 "pivot": list(ir["anchors"].get("origin", {}).get("at", [0, 0])),
                 "animations": {}}
        rows = [(n, fs) for n, fs in renders.items() if n != "base"]
        cols = max([len(fs) for _, fs in rows] + [1])
        sheet = bytearray(W * cols * H * max(len(rows), 1) * 4)
        for ri, (n, fs) in enumerate(rows):
            tpl = ir["animations"][n]
            atlas["animations"][n] = {"row": ri, "frames": len(fs), "fps": tpl["fps"], "loop": tpl["loop"],
                                      # per-frame duration in ticks at fps: hold multiplies the
                                      # base tick; `frames` counts rendered frames, not ticks
                                      "durations": [fr.get("hold", 1) for fr in tpl["frames"]],
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
        stats[aid] = str(out)
        total += sum(len(fs) for fs in renders.values())
    st = {"out": stats[ids[0]], "frames": total}
    if len(ids) > 1:
        st = {"out": stats, "frames": total, "ids": ids}
    print(json.dumps(report("render", ids[0], [], stats=st), indent=2))
    return 0


def cmd_test(args):
    reg = load_registry()
    docs, xdiags, base = load_asset_docs(args.asset)
    if xdiags:
        doc, _ = load_asset(args.asset)
        print(json.dumps(report("validate", asset_id(doc), xdiags), indent=2))
        return 1
    diags, results, ids = [], [], []
    for aid, d2, pfx in docs:
        d3, ir = compile_asset(d2, reg, base)
        ids.append(aid)
        if ir is None:
            diags += prefixed(d3, pfx)
            continue
        renders = render_all(ir)
        r2, d4 = run_tests(ir, renders)
        diags += prefixed(d3 + d4, pfx)
        results += r2
    if not results and diags:
        # nothing compiled (the single-document v2.0 shape): validate stage
        print(json.dumps(report("validate", ids[0], diags), indent=2))
        return 1
    extra = {"tests": results}
    if len(ids) > 1:
        extra["stats"] = {"ids": ids}
    r = report("test", ids[0], diags, **extra)
    print(json.dumps(r, indent=2))
    return 0 if r["status"] == "PASS" else 1


def frame_hashes(renders):
    return {f"{n}/{i}": hashlib.sha256(f["rgba"]).hexdigest() for n, fs in renders.items() for i, f in enumerate(fs)}


def cmd_golden(args):
    reg = load_registry()
    docs, xdiags, base = load_asset_docs(args.asset)
    if xdiags:
        doc, _ = load_asset(args.asset)
        print(json.dumps(report("validate", asset_id(doc), xdiags), indent=2))
        return 1
    diags, ids, total, written, compiled_n = [], [], 0, [], 0
    for aid, d2, pfx in docs:
        diags3, ir = compile_asset(d2, reg, base)
        ids.append(aid)
        if ir is None:
            diags += prefixed(diags3, pfx)
            continue
        compiled_n += 1
        hashes = frame_hashes(render_all(ir))
        total += len(hashes)
        gp = ROOT / "tests" / "golden" / f"{aid}.json"
        if args.update:
            gp.write_text(json.dumps({"asset": aid, "frames": hashes}, indent=2) + "\n")
            written.append(str(gp))
            continue
        want = json.loads(gp.read_text())["frames"] if gp.exists() else {}
        diags += [dict(D("E_GOLDEN_MISMATCH", "/" + k, "frame differs from golden",
                         observed=hashes.get(k, "missing")[:12],
                         expected=want.get(k, "missing")[:12]), path=pfx + "/" + k)
                  for k in sorted(set(hashes) | set(want)) if hashes.get(k) != want.get(k)]
    if not compiled_n and diags:
        print(json.dumps(report("validate", ids[0], diags), indent=2))
        return 1
    if args.update and not diags:
        st = {"written": written[0] if len(written) == 1 else written, "frames": total}
        if len(ids) > 1:
            st["ids"] = ids
        print(json.dumps(report("golden", ids[0], [], stats=st), indent=2))
        return 0
    st = {"frames": total}
    if len(ids) > 1:
        st["ids"] = ids
    r = report("golden", ids[0], diags, stats=st)
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
            if schema == "recipe.schema.json":
                for d in check_recipe_doc(load_json(p), reg, rel):
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
