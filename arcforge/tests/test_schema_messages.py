#!/usr/bin/env python3
"""P0.1 conformance vectors for schema diagnostics.

jsonschema reports *which* constraint failed; the reference tool words it
itself (see schema_diags in tools/arcforge_ref.py). The C compiler (ROADMAP
M4) must reproduce every report byte-for-byte, so this pins:

  * the exact message of every failure kind the schemas can produce,
  * the (path, kind, message) ordering -- including across paths,
  * the absence of wording only a JSON Schema library would produce,
  * the registry file prefix on lint paths.

Exit code 0 = all vectors hold.
"""
import copy
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


ASSET = A.load_json(ROOT / "examples" / "hero.iron_knight.json")
ARCH = A.load_json(ROOT / "registry" / "archetypes" / "humanoid_hero.archetype.json")
REC = A.load_json(sorted((ROOT / "registry" / "recipes").rglob("*.recipe.json"))[0])
STYLE = A.load_json(ROOT / "registry" / "styles" / "arcade_16.style.json")

UNKNOWN_FIELD = ("unknown field '{k}'. Free text is metadata, never executable "
                 "visual intent; use archetype/features/params or a custom px_fragment")

# (label, schema, builder(docs) -> document, expected [(code, path, message)])
CASES = [
    ("type (scalar)", "asset.schema.json",
     lambda: mut(ASSET, ["asset", "id"], 5),
     [("E_SCHEMA", "/asset/id", "expected string, got integer")]),

    ("type (list of types)", "archetype.schema.json",
     lambda: mut(ARCH, ["min_cell"], [True]),
     [("E_SCHEMA", "/min_cell", "expected at least 2 entries, got 1"),
      ("E_SCHEMA", "/min_cell/0", "expected integer or string, got boolean")]),

    ("enum", "asset.schema.json",
     lambda: mut(ASSET, ["asset", "type"], "bogus"),
     [("E_SCHEMA", "/asset/type",
       'expected one of ["character", "enemy", "boss", "projectile", "pickup", '
       '"effect", "tile", "ui"], got "bogus"')]),

    ("pattern (dotted identifier)", "asset.schema.json",
     lambda: mut(ASSET, ["asset", "id"], "UPPER"),
     [("E_SCHEMA", "/asset/id", '"UPPER" is not a valid dotted identifier')]),

    ("pattern (identifier)", "asset.schema.json",
     lambda: mut(ASSET, ["style_pack"], "BAD"),
     [("E_SCHEMA", "/style_pack", '"BAD" is not a valid identifier')]),

    ("required", "asset.schema.json",
     lambda: mut(ASSET, ["asset", "version"], delete=True),
     [("E_SCHEMA", "/asset", "'version' is a required property")]),

    ("additionalProperties", "asset.schema.json",
     lambda: mut(ASSET, ["bogus_visual"], 1),
     [("E_UNKNOWN_FIELD", "/bogus_visual", UNKNOWN_FIELD.format(k="bogus_visual"))]),

    ("oneOf (entry out of range)", "asset.schema.json",
     lambda: mut(ASSET, ["target", "canvas"], [0, 0]),
     [("E_SCHEMA", "/target/canvas/0", "expected >= 1, got 0"),
      ("E_SCHEMA", "/target/canvas/1", "expected >= 1, got 0")]),

    ("oneOf (shapes merged)", "asset.schema.json",
     lambda: mut(ASSET, ["target", "canvas"], "nope"),
     [("E_SCHEMA", "/target/canvas", "expected array or object, got string")]),

    ("oneOf (features item)", "asset.schema.json",
     lambda: mut(ASSET, ["features"], [5]),
     [("E_SCHEMA", "/features/0", "expected string or object, got integer")]),

    ("minProperties", "asset.schema.json",
     lambda: mut(ASSET, ["archetype", "size"], {}),
     [("E_SCHEMA", "/archetype/size", "expected at least 1 key, got 0")]),

    ("propertyNames", "archetype.schema.json",
     lambda: mut(ARCH, ["size"], {"Bad Name": {"min": 1, "max": 2, "default": 1}}),
     [("E_SCHEMA", "/size/Bad Name", '"Bad Name" is not a valid identifier')]),

    ("uniqueItems", "archetype.schema.json",
     lambda: mut(ARCH, ["layers"], ["a", "a"]),
     [("E_SCHEMA", "/layers", 'duplicate entry "a"')]),

    ("items (prefixItems bound)", "recipe.schema.json",
     lambda: mut(REC, ["locals"], [["a", "b", "c"]]),
     [("E_SCHEMA", "/locals/0", "expected at most 2 entries, got 3")]),

    ("oneOf (parameter missing keys)", "recipe.schema.json",
     lambda: mut(REC, ["parameters"], {"p_x": {"type": "int"}}),
     [("E_SCHEMA", "/parameters/p_x", "'default' is a required property"),
      ("E_SCHEMA", "/parameters/p_x", "'max' is a required property"),
      ("E_SCHEMA", "/parameters/p_x", "'min' is a required property")]),

    ("oneOf (parameter type merged)", "recipe.schema.json",
     lambda: mut(REC, ["parameters"], {"p_x": {"type": "float"}}),
     [("E_SCHEMA", "/parameters/p_x/type",
       'expected one of ["int", "bool", "enum", "ramp", "percent"], got "float"')]),

    ("oneOf (geometry scalar)", "recipe.schema.json",
     lambda: mut(REC, ["geometry"], "x"),
     [("E_SCHEMA", "/geometry", "expected array or object, got string")]),

    ("oneOf (propertyNames inside)", "recipe.schema.json",
     lambda: mut(REC, ["geometry"], {"Bad Name": []}),
     [("E_SCHEMA", "/geometry/Bad Name", '"Bad Name" is not a valid identifier'),
      ("E_SCHEMA", "/geometry/Bad Name", "expected at least 1 entry, got 0")]),

    ("minimum", "style_pack.schema.json",
     lambda: mut(STYLE, ["max_colors"], 0),
     [("E_SCHEMA", "/max_colors", "expected >= 2, got 0")]),

    ("maximum", "style_pack.schema.json",
     lambda: mut(STYLE, ["max_colors"], 999),
     [("E_SCHEMA", "/max_colors", "expected <= 256, got 999")]),

    ("type (style)", "style_pack.schema.json",
     lambda: mut(STYLE, ["ramps"], 5),
     [("E_SCHEMA", "/ramps", "expected object, got integer")]),

    ("ordering across paths", "asset.schema.json",
     lambda: _combined(),
     [("E_SCHEMA", "/asset", "'version' is a required property"),
      ("E_SCHEMA", "/asset/id", '"UPPER" is not a valid dotted identifier'),
      ("E_UNKNOWN_FIELD", "/bogus_visual", UNKNOWN_FIELD.format(k="bogus_visual")),
      ("E_SCHEMA", "/features/0", "expected string or object, got integer"),
      ("E_SCHEMA", "/style_pack", '"BAD" is not a valid identifier')]),
]


def _combined():
    """Several violations at once: the report must come out path-ordered."""
    d = mut(ASSET, ["bogus_visual"], 1)
    d = mut(d, ["asset", "id"], "UPPER")
    d = mut(d, ["asset", "version"], delete=True)
    d = mut(d, ["style_pack"], "BAD")
    return mut(d, ["features"], [5])


# wording a JSON Schema library would produce; none of it may appear
BANNED = [
    "is not of type", "is not one of", "does not match '",
    "are required properties", "was unexpected", "were unexpected",
    "is not valid under any", "is valid under each",
    "is less than the minimum", "is greater than the maximum",
    "is too short", "is too long", "does not have enough properties",
    "has too many properties", "has non-unique elements", "is not allowed",
]


def tuples(diags):
    return [(d["code"], d["path"], d["message"]) for d in diags]


all_messages = []
codes = set()
for label, schema, build, expected in CASES:
    got = tuples(A.schema_diags(build(), schema))
    check(f"messages [{label}]", got == expected,
          "" if got == expected else f"\n      got {got}\n      want {expected}")
    all_messages += [m for _c, _p, m in got]
    codes |= {c for c, _p, _m in got}

# lint-style paths carry the registry file as a prefix
prefixed = tuples(A.schema_diags(mut(REC, ["geometry"], "x"), "recipe.schema.json",
                                 "registry/recipes/weapon/sword.recipe.json"))
check("messages [registry prefix]", prefixed ==
      [("E_SCHEMA", "registry/recipes/weapon/sword.recipe.json/geometry",
        "expected array or object, got string")], f"got {prefixed}")
all_messages += [m for _c, _p, m in prefixed]
codes |= {c for c, _p, _m in prefixed}

for bad in BANNED:
    hits = [m for m in all_messages if bad in m]
    check(f"no library wording {bad!r}", not hits, str(hits[:2]))

check("only E_SCHEMA / E_UNKNOWN_FIELD", codes <= {"E_SCHEMA", "E_UNKNOWN_FIELD"},
      str(sorted(codes)))

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
