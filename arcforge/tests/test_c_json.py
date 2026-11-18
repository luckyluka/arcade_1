#!/usr/bin/env python3
"""M4.1a conformance: the C JSON layer vs the Python oracle.

The bundled C parser/writer (arcforge/c/json.c) must agree with the
Python reference (load_json / dump_json, docs/OUTPUT_FORMAT.md) on two
dimensions:

  * acceptance -- for every checked-in JSON file, the C parser accepts
    exactly the files Python's strict loader accepts (NaN/Infinity
    tokens rejected on both sides, no duplicates in the corpus);
  * bytes -- every file both accept re-serialises to IDENTICAL bytes in
    both tools, in the pretty (indent=2) form and the compact form used
    for message fragments, and the bytes are 7-bit ASCII.

The embedded C vectors (build + run under plain flags AND under
-fsanitize=undefined,address, per M4 acceptance item 5) cover the
grammar edges the corpus does not contain (reject vectors, float
formatting, escaping).

Known, deliberate divergences (spec'd, not tested here):
  * duplicate object keys: Python keeps the last, C rejects (the C
    contract is stricter; the corpus contains no duplicates -- this
    test proves that);
  * integers beyond int64: Python is exact (bigint), C rejects at
    parse (the corpus stays within int64; E_INT_RANGE fires on both
    sides for values in [2^31, 2^63)).

Exit code 0 = all vectors hold.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import arcforge_ref as A          # noqa: E402

fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"{'PASS' if ok else 'FAIL'}  {name} {detail}")
    fails += 0 if ok else 1


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


# --- 1. build both binaries (plain + sanitizers) ---------------------------
for extra, binname in (([], "arcforge_json_test"),
                       (["SAN=1"], "arcforge_json_test_san")):
    p = run(["make", "-C", str(ROOT / "c")] + extra)
    check(f"build {binname} clean (-Wall -Wextra -Werror)",
          p.returncode == 0, (p.stdout + p.stderr)[-400:])

BIN = ROOT / "build" / "arcforge_json_test"
BIN_SAN = ROOT / "build" / "arcforge_json_test_san"

# --- 2. embedded vectors, plain and sanitized ------------------------------
for binpath, label in ((BIN, "plain"), (BIN_SAN, "sanitized")):
    p = run([str(binpath)])
    check(f"embedded vectors [{label}]", p.returncode == 0,
          "" if p.returncode == 0 else p.stdout[-600:])

# --- 3. corpus differential ------------------------------------------------
CORPUS_DIRS = ("examples", "registry", "schema", "tests", "spec-next")
files = []
for d in CORPUS_DIRS:
    p = ROOT / d
    if p.exists():
        files += sorted(p.rglob("*.json"))
check("corpus found", len(files) >= 50, f"{len(files)} files")

ascii_ok = accept_parity = rewrite_parity = compact_parity = deterministic = 0
bad = {"accept": [], "rewrite": [], "compact": [], "ascii": [], "det": []}

for f in files:
    rel = str(f.relative_to(ROOT))
    # Python acceptance (the strict P0.5 loader)
    try:
        obj = A.load_json(f)
        py_ok = True
    except Exception:
        obj = None
        py_ok = False

    # C acceptance
    p = run([str(BIN), "--parse-check", str(f)])
    c_ok = p.returncode == 0

    if py_ok == c_ok:
        accept_parity += 1
    else:
        bad["accept"].append(f"{rel} python={py_ok} c={c_ok}")
    if not (py_ok and c_ok):
        continue

    # byte parity: pretty and compact
    want_pretty = A.dump_json(obj).encode("ascii")
    want_compact = A.dump_json(obj, indent=None).encode("ascii")
    p1 = run([str(BIN), "--rewrite", str(f)])
    p2 = run([str(BIN), "--rewrite", str(f)])
    p3 = run([str(BIN), "--compact", str(f)])
    got_pretty = p1.stdout.encode("utf-8", "surrogateescape")
    got_compact = p3.stdout.encode("utf-8", "surrogateescape")

    if p1.returncode == 0 and got_pretty == want_pretty:
        rewrite_parity += 1
    else:
        bad["rewrite"].append(rel)
    if p3.returncode == 0 and got_compact == want_compact:
        compact_parity += 1
    else:
        bad["compact"].append(rel)
    try:
        got_pretty.decode("ascii")
        ascii_ok += 1
    except UnicodeDecodeError:
        bad["ascii"].append(rel)
    if p1.returncode == 0 and p2.stdout == p1.stdout:
        deterministic += 1
    else:
        bad["det"].append(rel)

n = len(files)
check(f"parse acceptance parity ({n} files)", accept_parity == n,
      str(bad["accept"][:4]))
check(f"pretty rewrite byte parity ({rewrite_parity}/{n})",
      rewrite_parity == n, str(bad["rewrite"][:4]))
check(f"compact rewrite byte parity ({compact_parity}/{n})",
      compact_parity == n, str(bad["compact"][:4]))
check("rewrite output is 7-bit ASCII", ascii_ok == n, str(bad["ascii"][:4]))
check("C rewrite is byte-deterministic across runs",
      deterministic == n, str(bad["det"][:4]))

# --- 4. sanitized binary over the corpus (UB/leaks on real data) ----------
san_bad = []
for f in files:
    try:
        A.load_json(f)
    except Exception:
        continue  # acceptance parity already covered; only valid files here
    p = run([str(BIN_SAN), "--rewrite", str(f)])
    if p.returncode != 0:
        san_bad.append(str(f.relative_to(ROOT)))
check("sanitized rewrite of the whole corpus exits 0",
      not san_bad, str(san_bad[:4]))

print("ALL GREEN" if fails == 0 else f"{fails} FAILURES")
sys.exit(1 if fails else 0)
