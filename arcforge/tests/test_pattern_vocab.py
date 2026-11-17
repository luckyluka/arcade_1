#!/usr/bin/env python3
"""P0.2 conformance vectors for the pattern vocabulary.

The schemas declare names with `"format"` (no `pattern` anywhere -- a
dependency-free C build has no regex engine) and the reference tool checks
them by hand. This pins:

  * every vocabulary predicate, including its boundaries (64/65 characters,
    empty segments, non-ASCII digits),
  * the doc-level diagnostics each form produces, through schema_diags,
  * the walker's descent: properties, propertyNames, additionalProperties,
    prefixItems/items, $ref chains, oneOf branches,
  * if/then/else (asserted only on the matching side) and `not` (not walked),
  * schema hygiene: zero `pattern` keywords, every `format` in the vocabulary.

Exit code 0 = all vectors hold.
"""
import copy
import json
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


def mut(doc, path, val=None, delete=False):
    doc = copy.deepcopy(doc)
    t = doc
    for k in path[:-1]:
        t = t[k]
    if delete:
        del t[path[-1]]
    else:
        t[path[-1]] = val
    return doc


def tuples(diags):
    return [(d["code"], d["path"], d["message"]) for d in diags]


ASSET = A.load_json(ROOT / "examples" / "hero.iron_knight.json")
ARCH = A.load_json(ROOT / "registry" / "archetypes" / "humanoid_hero.archetype.json")
REC = A.load_json(sorted((ROOT / "registry" / "recipes").rglob("*.recipe.json"))[0])

# --- 1. predicate vectors -------------------------------------------------
# name -> (accepted values, rejected values)
PREDICATES = {
    "identifier": (
        ["a", "_", "1", "x1", "hero_iron", "a" * 64],
        ["", "UPPER", "a-b", "a b", "a" * 65, "é"]),
    "dotted_identifier": (
        ["a", "hero.iron_knight", "a.b.c"],
        ["", "a..b", ".a", "a.", "UPPER.x", "a-b.c", "a" * 65 + ".b"]),
    "semver": (
        ["2.0.0", "0.0.0", "02.10.3", "2026.11.17"],
        ["", "2.0", "2.0.0.0", "v2.0.0", "2..0", "2.0.", "٢.0.0", "2.0.x"]),
    "category_path": (
        ["weapon", "weapon/sword", "a/b/c"],
        ["", "/a", "a/", "a//b", "UPPER/x", "a-b/c"]),
    "parameter_name": (
        ["p_x", "p_1", "p_" + "a" * 62],
        ["p_", "px", "q_x", "P_x", "p_" + "a" * 63, "p_-"]),
    "diagnostic_code": (
        ["E_A", "W_CLIPPED", "E__X"],
        ["", "E", "E_", "X_A", "E_a", "e_A", "E-A"]),
}
for name, (good, bad) in PREDICATES.items():
    wrong_good = [v for v in good if not A.VOCABULARY[name](v)]
    wrong_bad = [v for v in bad if A.VOCABULARY[name](v)]
    check(f"accepts [{name}]", not wrong_good, str(wrong_good))
    check(f"rejects [{name}]", not wrong_bad, str(wrong_bad))

# --- 2. schema hygiene ----------------------------------------------------
schema_texts = {p.name: json.loads(p.read_text())
                for p in sorted((ROOT / "schema").glob("*.json"))}
raw_texts = {p.name: p.read_text() for p in sorted((ROOT / "schema").glob("*.json"))}
leaked = [n for n, t in raw_texts.items() if '"pattern"' in t]
check("no pattern keyword in schema/", not leaked, str(leaked))


def find_formats(node, acc):
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "format":
                acc.add(v)
            find_formats(v, acc)
    elif isinstance(node, list):
        for v in node:
            find_formats(v, acc)


formats = set()
find_formats(schema_texts, formats)
check("every format is in the vocabulary", formats <= set(A.VOCABULARY),
      f"{sorted(formats)}")
check("all six forms are used", formats == set(A.VOCABULARY),
      f"declared {sorted(formats)}")

# --- 3. doc-level vectors -------------------------------------------------
DOC_CASES = [
    ("semver (asset)", "asset.schema.json",
     lambda: mut(ASSET, ["asset", "version"], "2.0"),
     [("E_SCHEMA", "/asset/version", '"2.0" is not a valid semver')]),

    ("category_path (recipe)", "recipe.schema.json",
     lambda: mut(REC, ["category"], "BAD"),
     [("E_SCHEMA", "/category", '"BAD" is not a valid category path')]),

    ("semver (recipe)", "recipe.schema.json",
     lambda: mut(REC, ["version"], "v2"),
     [("E_SCHEMA", "/version", '"v2" is not a valid semver')]),

    ("identifier in oneOf branch", "asset.schema.json",
     lambda: mut(ASSET, ["features"], ["UPPER"]),
     [("E_SCHEMA", "/features/0", '"UPPER" is not a valid identifier')]),

    ("parameter_name (nested propertyNames)", "archetype.schema.json",
     lambda: mut(ARCH, ["proportions"], {"hero": {"bad_key": 1}}),
     [("E_SCHEMA", "/proportions/hero/bad_key",
       '"bad_key" is not a valid parameter name')]),

    ("additionalProperties value", "asset.schema.json",
     lambda: mut(ASSET, ["palette_swap"], {"steel": "BAD-RAMP"}),
     [("E_SCHEMA", "/palette_swap/steel", '"BAD-RAMP" is not a valid identifier')]),

    ("additionalProperties key", "asset.schema.json",
     lambda: mut(ASSET, ["palette_swap"], {"BAD-RAMP": "steel"}),
     [("E_SCHEMA", "/palette_swap/BAD-RAMP", '"BAD-RAMP" is not a valid identifier')]),

    ("prefixItems entry", "archetype.schema.json",
     lambda: mut(ARCH, ["metrics"], [["Bad Name", "x"]]),
     [("E_SCHEMA", "/metrics/0/0", '"Bad Name" is not a valid identifier')]),

    ("diagnostic_code", "diagnostics.schema.json",
     lambda: {"status": "FAIL", "asset": "x", "stage": "validate",
              "diagnostics": [{"code": "X_FOO", "severity": "error",
                               "path": "/", "message": "m"}]},
     [("E_SCHEMA", "/diagnostics/0/code", '"X_FOO" is not a valid diagnostic code')]),

    ("$ref chain into $defs", "recipe.schema.json",
     lambda: mut(REC, ["geometry"], [{"shape": "line", "fill": "red", "x1": 0,
                                      "y1": 0, "x2": 4, "thickness": 2,
                                      "var": "BAD_VAR"}]),
     [("E_SCHEMA", "/geometry/0", "'anchor' is a required property"),
      ("E_SCHEMA", "/geometry/0", "'layer' is a required property"),
      ("E_SCHEMA", "/geometry/0/var", '"BAD_VAR" is not a valid identifier')]),
]
for label, schema, build, expected in DOC_CASES:
    got = tuples(A.schema_diags(build(), schema))
    check(f"diagnostics [{label}]", got == expected,
          "" if got == expected else f"\n      got {got}\n      want {expected}")

# --- 4. walker descent on a synthetic schema ------------------------------
def walk(doc, schema):
    """Run the hand walker over a schema that is not in schema/."""
    out = []
    A._collect_formats(doc, schema, schema, "", out)
    return [t[4]["message"] for t in out]


COND = {"if": {"required": ["kind"], "properties": {"kind": {"const": "a"}}},
        "then": {"properties": {"name": {"format": "identifier"}}},
        "else": {"properties": {"name": {"format": "semver"}}}}
check("if/then asserted when matched",
      walk({"kind": "a", "name": "UPPER"}, COND) == ['"UPPER" is not a valid identifier'])
check("else asserted when if fails",
      walk({"kind": "b", "name": "UPPER"}, COND) == ['"UPPER" is not a valid semver'])
check("only the matching side is asserted",
      walk({"kind": "a", "name": "2.0.0"}, COND) == ['"2.0.0" is not a valid identifier'])
check("not is not walked",
      walk("UPPER", {"not": {"format": "identifier"}}) == [])
check("items entries are walked",
      walk(["UPPER"], {"type": "array", "items": {"format": "identifier"}})
      == ['"UPPER" is not a valid identifier'])
check("allOf branches are walked",
      walk("UPPER", {"allOf": [{"format": "identifier"}, {"type": "string"}]})
      == ['"UPPER" is not a valid identifier'])
check("non-strings are left to the type check",
      walk(5, {"format": "identifier"}) == [])

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
