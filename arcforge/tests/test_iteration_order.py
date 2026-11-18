#!/usr/bin/env python3
"""P0.7 conformance vectors: registry iteration order and hash-order hygiene.

Determinism contract (docs/ARCHITECTURE.md): no dict/set order dependence
anywhere in output. Two layers pin it:

  * static order pins -- load_registry() inserts in sorted-by-file-path
    order for all three kinds, and every set that reaches a report does
    so through sorted() (or through close(), whose nlargest tie-break is
    order-independent -- vectors in test_similarity.py);
  * dynamic proof -- every report-producing command is run in two
    separate processes with DIFFERENT PYTHONHASHSEED values (so `set`
    and `str` hashing differ) and the stdout bytes, plus every file the
    command writes (atlas.json, sweep table), must be identical. Any
    hash-order leak fails here immediately.

Exit code 0 = all vectors hold.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "arcforge_ref.py"
REG = ROOT / "registry"
fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A          # noqa: E402

# --- 1. registry key order == sorted-by-file-path order --------------------
KINDS = [("archetypes", "archetypes/*.archetype.json", "archetype"),
         ("recipes", "recipes/**/*.recipe.json", "recipe"),
         ("styles", "styles/*.style.json", "style_pack")]

reg = A.load_registry()
for kind, pattern, key in KINDS:
    expected = [json.loads(p.read_text())[key] for p in sorted(REG.glob(pattern))]
    got = list(reg[kind])
    check(f"load_registry {kind} == sorted-by-path", got == expected,
          "" if got == expected else f"got {got[:5]}... want {expected[:5]}...")

# --- 2. report bytes are identical across hash seeds -----------------------
def cli(args, seed, cwd=None):
    env = dict(os.environ, PYTHONHASHSEED=seed)
    p = subprocess.run([sys.executable, str(TOOL)] + args,
                       capture_output=True, text=True, env=env, cwd=cwd)
    if p.returncode not in (0, 1):
        raise RuntimeError(f"exit {p.returncode} for {args}: {p.stderr[-400:]}")
    return p.stdout


def same_bytes(label, args, extra=None):
    """extra: optional callable(seed) -> {name: bytes} for written files."""
    try:
        a = cli(args, "0")
        fa = extra("0") if extra else {}
        b = cli(args, "99991")
        fb = extra("99991") if extra else {}
    except RuntimeError as e:
        check(f"hashseed [{label}]", False, str(e))
        return
    ok = a == b
    detail = "" if ok else f"stdout differs:\n  seed0    {a[:300]!r}\n  seed99991 {b[:300]!r}"
    check(f"hashseed [{label}] stdout", ok, detail)
    for name in sorted(set(fa) | set(fb)):
        ok = fa.get(name) == fb.get(name)
        check(f"hashseed [{label}] {name}", ok,
              "" if ok else f"seed0 {fa.get(name, b'')[:80]!r} != seed99991 {fb.get(name, b'')[:80]!r}")


# pick a negative fixture that actually exercises suggestions (close() over
# a set-like option collection) so the interesting path is covered too
sug_fixture = None
for f in sorted((ROOT / "tests" / "invalid").glob("*.json")):
    if f.name == "expectations.json":
        continue
    out = cli(["validate", str(f)], "0")
    if '"suggestions": [' in out and '"suggestions": []' not in out:
        sug_fixture = f
        break
check("found an invalid fixture with non-empty suggestions", sug_fixture is not None,
      "" if sug_fixture else "no fixture produced suggestions")
check("fixture suggestions are hash-order stable",
      sug_fixture is None or cli(["validate", str(sug_fixture)], "0")
      == cli(["validate", str(sug_fixture)], "99991"))

same_bytes("lint", ["lint"])
same_bytes("list", ["list"])
lst = json.loads(cli(["list"], "0"))
same_bytes("describe recipe", ["describe", "recipe", "helmet_closed"])
for plural, singular in (("archetypes", "archetype"), ("styles", "style")):
    if lst.get(plural):
        first = next(iter(lst[plural]))
        same_bytes(f"describe {singular}", ["describe", singular, first])
same_bytes("test hero.iron_knight", ["test", str(ROOT / "examples" / "hero.iron_knight.json")])
same_bytes("golden hero.iron_knight (read-only)",
           ["golden", str(ROOT / "examples" / "hero.iron_knight.json")])
same_bytes("validate hero.iron_knight", ["validate", str(ROOT / "examples" / "hero.iron_knight.json")])

# the list report itself must be in sorted-by-path order (byte level)
for kind, pattern, key in KINDS:
    expected = [json.loads(p.read_text())[key] for p in sorted(REG.glob(pattern))]
    got = list(lst.get(kind, {}))
    check(f"list report {kind} order == sorted-by-path", got == expected,
          "" if got == expected else f"got {got[:5]}... want {expected[:5]}...")

# render: stdout + atlas.json (no-newline sink), same --out for both seeds
scratch = Path(tempfile.mkdtemp(prefix="p07_", dir="/tmp/opencode"))


same_bytes("render hero.iron_knight", ["render", str(ROOT / "examples" / "hero.iron_knight.json"),
                                       "--out", str(scratch / "probe"), "--scale", "1"],
           extra=lambda seed: (lambda d: {f"atlas/{k}": (d / k / "atlas.json").read_bytes()
                                          for k in sorted(os.listdir(d))})(scratch / "probe"))

# sweep: stdout + table.json (+newline sink) -- only size-parametric
# archetypes write a table, so pick the first one that does
arch_for_sweep = None
for arch_name in lst.get("archetypes", {}):
    p = REG / "archetypes" / f"{arch_name}.archetype.json"
    cli(["sweep", str(p)], "0")
    if (ROOT / "out" / "sweep" / f"{arch_name}.table.json").exists():
        arch_for_sweep = arch_name
        break
check("found a size-parametric archetype whose sweep writes a table",
      arch_for_sweep is not None,
      "" if arch_for_sweep else "no archetype produced a table")
if arch_for_sweep:
    same_bytes(f"sweep {arch_for_sweep}",
               ["sweep", str(REG / "archetypes" / f"{arch_for_sweep}.archetype.json")],
               extra=lambda seed: {"table": (ROOT / "out" / "sweep" /
                                             f"{arch_for_sweep}.table.json").read_bytes()})

# --- 3. close() over a real set == over its sorted list --------------------
names = set(reg["recipes"])
sample = sorted(names)[0]
check("close(set) == close(sorted(set)) (order-independent tie-break)",
      A.close(sample, names) == A.close(sample, sorted(names)),
      f"{A.close(sample, names)} vs {A.close(sample, sorted(names))}")

# --- 4. source hygiene: sets reach reports only through sorted() -----------
src = (ROOT / "tools" / "arcforge_ref.py").read_text()
MUST_EXIST = [
    'for p in sorted(REG.glob("archetypes/*.archetype.json"))',
    'for p in sorted(REG.glob("recipes/**/*.recipe.json"))',
    'for p in sorted(REG.glob("styles/*.style.json"))',
    "for fname in sorted(feat)",                       # W_OCCLUDED feature sets
    "expected=sorted(overridable)",                    # E_TRACK_PARAM set
    "return sorted(names)",                            # known_parts set
    "for k, who in sorted(rec_kinds.items())",
    "for (r, c), (w, h, rgba) in sorted(cells.items())",  # sheet drawing
    "observed=sorted(rec[\"geometry\"])",
    "f\"recipe parameters shadow archetype names: {sorted(clash)}\"",
    "for k in sorted(set(hashes) | set(want))",         # golden compare keys
]
for needle in MUST_EXIST:
    check(f"sorted() pin present: {needle[:52]}", needle in src)

# no raw iteration over the audited set variables
BAD = ["for fname in feat:", "for k in overridable:", "for n in names:",
       "for k in seen:", "for v in clash:"]
leaked = [b for b in BAD if b in src]
check("no unsorted iteration over audited sets", not leaked, str(leaked))

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
