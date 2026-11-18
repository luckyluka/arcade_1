#!/usr/bin/env python3
"""P0.5 conformance vectors: the JSON output format.

docs/OUTPUT_FORMAT.md is the normative spec; the tool's dump_json() is
its reference implementation. This file keeps both honest with a second,
independent writer written straight from the document:

  * exact byte vectors for every writer rule (layout, empty containers,
    string escaping incl. non-ASCII and astral, integers, the
    four-decimal float rule, compact message fragments);
  * a live corpus: every report shape the CLI can emit is captured,
    parsed, re-serialised with the independent writer and compared
    byte-for-byte (and must be 7-bit ASCII);
  * every checked-in golden file round-trips through the writer;
  * the float rule observed in a real report message, the NaN/Infinity
    read-time rejection, and source hygiene (no unconverted json.dumps
    emission, sink newline rules pinned in the source).

Exit code 0 = all vectors hold.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A   # noqa: E402

fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


# --------------------------------------------------------------------------
# Independent writer, derived from docs/OUTPUT_FORMAT.md (NOT from the
# tool -- two implementations of one spec is the point of this file).
# --------------------------------------------------------------------------
def writer(obj, indent=2):
    def w(v, depth):
        if v is None:
            return "null"
        if v is True:
            return "true"
        if v is False:
            return "false"
        if isinstance(v, int):
            return str(v)
        if isinstance(v, float):
            return format(v, ".4f")
        if isinstance(v, str):
            return json.dumps(v, ensure_ascii=True)
        if isinstance(v, list):
            if not v:
                return "[]"
            if indent is None:
                return "[" + ", ".join(w(x, depth + 1) for x in v) + "]"
            pad = " " * (indent * (depth + 1))
            return ("[\n" + pad + (",\n" + pad).join(w(x, depth + 1) for x in v)
                    + "\n" + " " * (indent * depth) + "]")
        if isinstance(v, dict):
            if not v:
                return "{}"
            items = [json.dumps(str(k), ensure_ascii=True) + ": " + w(x, depth + 1)
                     for k, x in v.items()]
            if indent is None:
                return "{" + ", ".join(items) + "}"
            pad = " " * (indent * (depth + 1))
            return ("{\n" + pad + (",\n" + pad).join(items)
                    + "\n" + " " * (indent * depth) + "}")
        raise TypeError(type(v).__name__)
    return w(obj, 0)


# --- 1. exact byte vectors -------------------------------------------------
VECTORS = [
    ("int", 42, "42"),
    ("negative int", -7, "-7"),
    ("bool/null", [True, False, None], "[true, false, null]"),
    ("empty object", {}, "{}"),
    ("empty array", [], "[]"),
    ("nested empties", {"a": {}, "b": [], "c": [[]]},
     '{\n  "a": {},\n  "b": [],\n  "c": [\n    []\n  ]\n}'),
    ("layout", {"b": 1, "a": [1, {"k": "v"}, []]},
     '{\n  "b": 1,\n  "a": [\n    1,\n    {\n      "k": "v"\n    },\n    []\n  ]\n}'),
    ("floats four decimals", [0.0, 1.0, 0.5, 0.6904, -0.0, -1.23456, 1234.56789],
     "[0.0000, 1.0000, 0.5000, 0.6904, -0.0000, -1.2346, 1234.5679]"),
    ("string escaping", {"s": 'q" \\ \n \t \x01 é 😀'},
     '{\n  "s": "q\\" \\\\ \\n \\t \\u0001 \\u00e9 \\ud83d\\ude00"\n}'),
    ("key escaping", {"é": 1}, '{\n  "\\u00e9": 1\n}'),
    ("slash not escaped", {"p": "a/b"}, '{\n  "p": "a/b"\n}'),
    ("large int exact", 4294967296, "4294967296"),
]
for label, obj, want in VECTORS:
    # compact expectations (no newline) are the indent=None form used for
    # message fragments; multi-line expectations are the indent=2 form
    got = writer(obj, indent=None) if "\n" not in want else writer(obj)
    check(f"writer [{label}]", got == want,
          "" if got == want else f"\n      got  {got!r}\n      want {want!r}")

# float never takes exponent form, even at extremes (rule 6)
big = writer(1e300)
check("writer [huge float is fixed-point]",
      "e" not in big and "E" not in big and big.startswith("1000") and big.endswith(".0000"))
check("writer [tiny float is fixed-point]", writer(1e-9) == "0.0000")

# --- 2. the reference implementation agrees with the independent writer ----
TORTURE = {"status": "FAIL", "n": [1, -2, 0], "f": [0.5, -0.0, 0.6904],
           "s": ['a"b', "é", "😀", "\n\x01"], "e": {}, "l": [],
           "nested": {"k": [{"x": None}, True, False]}}
check("reference dump_json == independent writer (indent)",
      A.dump_json(TORTURE) == writer(TORTURE))
check("reference dump_json == independent writer (compact/_lit)",
      A.dump_json(TORTURE, indent=None) == writer(TORTURE, indent=None))
check("_lit is the compact writer", A._lit(["int", "bool"]) == '["int", "bool"]'
      and A._lit({"a": 1}) == '{"a": 1}' and A._lit(0.5) == "0.5000")

NUM4 = [(0.2, "0.2000"), (0.6904, "0.6904"), (0.5, "0.5000"), (1.0, "1.0000"),
        (2, "2"), (-1, "-1"), (0, "0")]
for value, want in NUM4:
    check(f"num4({value!r}) == {want!r}", A.num4(value) == want,
          f"got {A.num4(value)!r}")

# --- 3. live corpus: every report round-trips byte-for-byte ---------------
def cli(*args):
    p = subprocess.run([sys.executable, str(ROOT / "tools" / "arcforge_ref.py")]
                       + list(args), capture_output=True, text=True)
    return p


def check_report(label, args, contains=None):
    p = cli(*args)
    if p.returncode not in (0, 1):
        check(f"report [{label}]", False, f"exit {p.returncode}: {p.stderr[-300:]}")
        return
    out = p.stdout
    try:
        out.encode("ascii")
    except UnicodeEncodeError as e:
        check(f"report [{label}]", False, f"not 7-bit ASCII: {e}")
        return
    try:
        obj = json.loads(out)
    except json.JSONDecodeError as e:
        check(f"report [{label}]", False, f"stdout is not parseable JSON: {e}")
        return
    want = writer(obj) + "\n"
    ok = out == want
    detail = "" if ok else f"\n      got  {out[:400]!r}\n      want {want[:400]!r}"
    if ok and contains is not None:
        ok = contains in out
        detail = "" if ok else f"missing {contains!r} in report"
    check(f"report [{label}]", ok, detail)


for ex in sorted((ROOT / "examples").glob("*.json")):
    check_report(f"validate {ex.name}", ("validate", str(ex)))

exp = json.loads((ROOT / "tests" / "expectations.json").read_text()) \
    if (ROOT / "tests" / "expectations.json").exists() else \
    json.loads((ROOT / "tests" / "invalid" / "expectations.json").read_text())
for name, spec in exp.items():
    run = spec if isinstance(spec, list) else spec.get("run", "validate")
    if run != "validate":
        continue
    check_report(f"validate invalid/{name}", ("validate", str(ROOT / "tests" / "invalid" / f"{name}.json")))

check_report("lint", ("lint",))
check_report("list", ("list",))

lst = json.loads(cli("list").stdout)
KIND_SINGULAR = {"archetypes": "archetype", "recipes": "recipe", "styles": "style"}
for plural, singular in KIND_SINGULAR.items():
    if lst.get(plural):
        first = next(iter(lst[plural]))
        check_report(f"describe {singular} {first}", ("describe", singular, first))

# the float rule observed end-to-end in a real report: hero.iron_knight's
# silhouette band is 0.2..0.7 in the source document and must surface as
# "0.2000..0.7000" in the assertion results.
check_report("test hero.iron_knight", ("test", str(ROOT / "examples" / "hero.iron_knight.json")),
             contains='"expected": "0.2000..0.7000"')
check_report("golden hero.iron_knight", ("golden", str(ROOT / "examples" / "hero.iron_knight.json")))

# --- 4. file sinks --------------------------------------------------------
bad = []
for g in sorted((ROOT / "tests" / "golden").glob("*.json")):
    text = g.read_text()
    if writer(json.loads(text)) + "\n" != text:
        bad.append(g.name)
check("every checked-in golden round-trips (+newline sink)", not bad, str(bad))

# render: report on stdout (covered by round-trip) + atlas.json (no-newline
# sink) written to a scratch dir outside the repo.
scratch = Path(tempfile.mkdtemp(prefix="p05_", dir="/tmp/opencode"))
p = cli("render", str(ROOT / "examples" / "hero.iron_knight.json"),
        "--out", str(scratch), "--scale", "1")
if p.returncode not in (0, 1):
    check("render report", False, f"exit {p.returncode}: {p.stderr[-300:]}")
else:
    out = p.stdout
    try:
        obj = json.loads(out)
        ok = writer(obj) + "\n" == out
    except (json.JSONDecodeError, UnicodeEncodeError) as e:
        ok = False
        obj, e_ = None, e
    check("report [render]", ok, "" if ok else repr(out[:300]))
    atlases = list(scratch.rglob("atlas.json"))
    if atlases:
        text = atlases[0].read_text()
        check("atlas.json (no-newline sink) round-trips",
              writer(json.loads(text)) == text,
              "" if writer(json.loads(text)) == text else repr(text[:200]))
    else:
        check("atlas.json written", False, f"not found under {scratch}")

# sweep: report + table.json (+newline sink)
p = cli("sweep", str(ROOT / "registry" / "archetypes" / "humanoid_front_scalable.archetype.json"))
if p.returncode not in (0, 1):
    check("sweep report", False, f"exit {p.returncode}: {p.stderr[-300:]}")
else:
    out = p.stdout
    try:
        ok = writer(json.loads(out)) + "\n" == out
    except (json.JSONDecodeError, UnicodeEncodeError):
        ok = False
    check("report [sweep]", ok, "" if ok else repr(out[:300]))
    table = ROOT / "out" / "sweep" / "humanoid_front_scalable.table.json"
    if table.exists():
        text = table.read_text()
        check("table.json (+newline sink) round-trips",
              writer(json.loads(text)) + "\n" == text)
    else:
        check("table.json written", False, f"missing {table}")

# --- 5. read-time rejection of non-finite constants -----------------------
for token in ("NaN", "Infinity", "-Infinity"):
    with tempfile.NamedTemporaryFile("w", suffix=".json", dir="/tmp/opencode",
                                     delete=False) as f:
        f.write('{"x": %s}' % token)
        path = f.name
    try:
        A.load_json(path)
        check(f"load_json rejects {token}", False, "no exception")
    except json.JSONDecodeError:
        check(f"load_json rejects {token}", True)
    try:
        A.load_asset(path)
        check(f"load_asset rejects {token}", False, "no exception")
    except json.JSONDecodeError:
        check(f"load_asset rejects {token}", True)

# malformed JSON raises the same class (the rejection is not a new failure
# mode -- it joins the pre-existing one)
with tempfile.NamedTemporaryFile("w", suffix=".json", dir="/tmp/opencode",
                                 delete=False) as f:
    f.write('{"x": ')
    path = f.name
try:
    A.load_json(path)
    check("malformed JSON raises JSONDecodeError", False, "no exception")
except json.JSONDecodeError:
    check("malformed JSON raises JSONDecodeError", True)

# --- 6. source hygiene ----------------------------------------------------
src = (ROOT / "tools" / "arcforge_ref.py").read_text()
leaked = [ln.strip()[:90] for ln in src.splitlines()
          if "json.dumps(" in ln and "sort_keys=True" not in ln
          and "ensure_ascii=True" not in ln]
check("every json.dumps call is internal (sort_keys) or the writer's escaper",
      not leaked, str(leaked))
check("no json.dumps emission left on stdout", "print(json.dumps(" not in src)
check("atlas sink has no trailing newline", 'write_text(dump_json(atlas))' in src)
check("golden sink has trailing newline",
      'gp.write_text(dump_json({"asset": aid, "frames": hashes}) + "\\n")' in src)
check("stdout sinks print the writer",
      src.count("print(dump_json(") >= 15 and "print(dump_json(" in src)

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
