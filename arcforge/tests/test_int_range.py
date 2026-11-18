#!/usr/bin/env python3
"""P0.3 conformance vectors: integer range and floor semantics.

The card defines two things the C compiler must reproduce exactly:

  * **Range.** Every integer written in a document (free-form `metadata`
    included) and every integer literal in an expression must fit in int32
    [-2147483648, 2147483647]. JSON integers are rejected by a
    whole-document walk as `E_INT_RANGE`; expression literals are rejected
    at parse time as `ExprError`, which the compiler reports as
    `E_ARCHETYPE_EXPR` / `E_RECIPE_EXPR`. Unary minus folds before the
    check (`-2147483648` is written in range). Booleans and floats are
    not integers here (floats are out of the card's scope -- see
    ../../docs/experiments/arcforge-integer-overflow.md).

  * **Floor.** `//`, `%`, `rot_fixed`, `to_int`, `half` and every raster
    division floor for negatives; Python's oracle floors, a naive C `/`
    truncates, and the pinned vectors below differ between the two.

Results of *operations* are not range-checked at parse time (that is the
open finding above), so `2147483647 * 2` still evaluates exactly here.

Exit code 0 = all vectors hold.
"""
import copy
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A   # noqa: E402

fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


def tuples(diags):
    return [(d["code"], d["path"], d["message"]) for d in diags]


def mut(doc, path, val):
    doc = copy.deepcopy(doc)
    t = doc
    for k in path[:-1]:
        t = t[k]
    t[path[-1]] = val
    return doc


RANGE = "-2147483648..2147483647"
ASSET = A.load_json(ROOT / "examples" / "hero.iron_knight.json")

# --- 1. document integers: boundary, skip, nesting, payload ---------------
DOC_CASES = [
    ("max int32 in range", 2147483647, []),
    ("min int32 in range", -2147483648, []),
    ("max int32 + 1 rejected", 2147483648,
     [("E_INT_RANGE", "/metadata/n", f"integer 2147483648 is outside the valid range {RANGE}")]),
    ("min int32 - 1 rejected", -2147483649,
     [("E_INT_RANGE", "/metadata/n", f"integer -2147483649 is outside the valid range {RANGE}")]),
    ("2^32 rejected", 4294967296,
     [("E_INT_RANGE", "/metadata/n", f"integer 4294967296 is outside the valid range {RANGE}")]),
    ("boolean is not an integer", True, []),
    ("huge float not range-checked", 1e300, []),
    ("float with int32-exceeding value not range-checked", 2147483647.5, []),
]
for label, value, want in DOC_CASES:
    got = tuples(A.schema_diags(mut(ASSET, ["metadata"], {"n": value}), "asset.schema.json"))
    check(f"document [{label}]", got == want,
          "" if got == want else f"got {got}")

# nested pointer + payload
d = A.schema_diags(mut(ASSET, ["metadata"], {"deep": {"x": [1, -2147483649]}}),
                   "asset.schema.json")
want = [("E_INT_RANGE", "/metadata/deep/x/1",
         f"integer -2147483649 is outside the valid range {RANGE}")]
check("nested pointer", tuples(d) == want, str(tuples(d)))
d = d[0]
check("payload carries observed/expected",
      d.get("observed") == -2147483649 and d.get("expected") == RANGE
      and d.get("severity") == "error" and "fix" not in d,
      str({k: d.get(k) for k in ("observed", "expected", "severity", "fix")}))

# ordering: at one path, the schema keyword (rank 2) precedes our walk (rank 17)
bad = mut(ASSET, ["target", "canvas"], [4294967296, 32])
got = tuples(A.schema_diags(bad, "asset.schema.json"))
want = [("E_SCHEMA", "/target/canvas/0", "expected <= 256, got 4294967296"),
        ("E_INT_RANGE", "/target/canvas/0",
         f"integer 4294967296 is outside the valid range {RANGE}")]
check("ordering: E_SCHEMA before E_INT_RANGE at the same path", got == want,
      "" if got == want else f"got {got}")

# --- 2. expression literals: parse-time rejection, unary-minus fold --------
EXPR_OK = ["2147483647", "-2147483648", "+2147483647", "- 2147483648",
           "-2147483648 + 1", "min(-2147483648, 0)", "clamp(0, -2147483648, 5)"]
for e in EXPR_OK:
    try:
        A.ev(e, {})
        check(f"expression accepts {e!r}", True)
    except A.ExprError as ex:
        check(f"expression accepts {e!r}", False, f"unexpected ExprError: {ex}")

EXPR_BAD = [
    ("2147483648", 2147483648),
    ("+2147483648", 2147483648),
    ("-2147483649", -2147483649),
    ("-4294967296", -4294967296),
    ("0 - 2147483648", 2147483648),
    ("min(3000000000, 0)", 3000000000),
]
for e, value in EXPR_BAD:
    try:
        A.ev(e, {})
        check(f"expression rejects {e!r}", False, "no ExprError")
    except A.ExprError as ex:
        want = f"integer {value} is outside the valid range {RANGE}"
        check(f"expression rejects {e!r}", str(ex) == want, f"got {ex!r}")

# parse-time only: operation *results* are not checked (open finding)
check("results are not range-checked (parse-time only)",
      A.ev("2147483647 * 2", {}) == 4294967294)

# --- 3. floor semantics: operators, helpers, rot_fixed --------------------
FLOOR_CASES = [
    ("-7 // 2", A.ev("-7 // 2", {}), -4),
    ("-7 % 2", A.ev("-7 % 2", {}), 1),
    ("7 // -2", A.ev("7 // -2", {}), -4),
    ("7 % -2", A.ev("7 % -2", {}), -1),
    ("-7 // -2", A.ev("-7 // -2", {}), 3),
    ("to_int(-3.5)", A.to_int(-3.5), -4),
    ("to_int(-3)", A.to_int(-3), -3),
    ("half(-7)", A.FUNCS["half"](-7), -4),
    ("odd(-3)", A.FUNCS["odd"](-3), -3),
    ("even(-3)", A.FUNCS["even"](-3), -2),
    ("round(-0.5)", A.FUNCS["round"](-0.5), 0),
    ("floor(-1.5)", A.FUNCS["floor"](-1.5), -2),
]
for label, got, want in FLOOR_CASES:
    check(f"floor [{label}]", got == want, f"got {got} want {want}")

# rot_fixed: pinned outputs. The first vector's quotient is -125.4648 (floor
# -126, truncation -125) and the third's is -127.5 (floor -128, truncation
# -127) -- a truncating C division renders both one pixel off.
ROT_CASES = [
    ((-10, -10, 37), (-126, -896)),
    ((5, -3, 90), (192, 320)),
    ((-7, 2, 180), (448, -128)),
]
for args, want in ROT_CASES:
    got = A.rot_fixed(*args)
    check(f"rot_fixed{args}", got == want, f"got {got} want {want}")

# --- 4. integration: the codes the compiler reports -----------------------
reg = A.load_registry()

# archetype metric with an out-of-range literal
arch_name = "humanoid_front_scalable"
reg2 = copy.deepcopy(reg)
metrics = reg2["archetypes"][arch_name]["metrics"]
path_i = len(metrics)
metrics.append(["zz_probe", "2147483648 + 1"])
asset1 = mut(ASSET, ["archetype"], {"name": arch_name})
diags, _ir = A.compile_asset(asset1, reg2, str(ROOT / "examples"))
want = ("E_ARCHETYPE_EXPR", f"/archetype/metrics/{path_i}",
        f"metric 'zz_probe' failed to evaluate: integer 2147483648 is outside the valid range {RANGE}")
got = tuples(diags)
check("integration: archetype metric literal -> E_ARCHETYPE_EXPR", want in got,
      "" if want in got else f"missing {want}; got {got[:6]}")

# recipe local with an out-of-range literal (feature #0 of the hero asset)
reg3 = copy.deepcopy(reg)
rec_name = ASSET["features"][0]["recipe"]
reg3["recipes"][rec_name]["locals"] = [["zz_probe", "4000000000 + 1"]]
diags, _ir = A.compile_asset(ASSET, reg3, str(ROOT / "examples"))
want = ("E_RECIPE_EXPR", "/features/0",
        f"recipe '{rec_name}' local 'zz_probe' failed to evaluate: integer 4000000000 is outside the valid range {RANGE}")
got = tuples(diags)
check("integration: recipe local literal -> E_RECIPE_EXPR", want in got,
      "" if want in got else f"missing {want}; got {got[:6]}")

# --- 5. corpus hygiene: no out-of-range integer anywhere we ship ---------
LO, HI = A.INT_MIN, A.INT_MAX
bad_json = []


def walk_json(node, path, fname):
    if isinstance(node, dict):
        for k, v in node.items():
            walk_json(v, f"{path}/{k}", fname)
    elif isinstance(node, list):
        for i, v in enumerate(node):
            walk_json(v, f"{path}/{i}", fname)
    elif isinstance(node, int) and not isinstance(node, bool) and not LO <= node <= HI:
        bad_json.append((fname, path, node))


files = sorted((ROOT / "examples").glob("*.json")) + \
        sorted((ROOT / "registry").rglob("*.json")) + \
        sorted((ROOT / "schema").glob("*.json"))
for f in files:
    walk_json(json.loads(f.read_text()), "", str(f.relative_to(ROOT)))
check("no out-of-range JSON integer in examples/ registry/ schema/",
      not bad_json, str(bad_json[:5]))

# integer literals inside strings (expressions): runs of 10+ digits are the
# only ones that can leave int32. A hit means the literal itself is out of
# range (an in-range negative literal keeps its sign only if written at the
# start of the run -- none exist in the corpus today).
bad_literals = []
for f in files:
    for m in re.finditer(r"(?<![\w.])\d{10,}", f.read_text()):
        if not LO <= int(m.group()) <= HI:
            bad_literals.append((str(f.relative_to(ROOT)), m.group()))
check("no out-of-range integer literal in shipped strings",
      not bad_literals, str(bad_literals[:5]))

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
