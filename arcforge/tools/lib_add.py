#!/usr/bin/env python3
"""Gate for new library items.
  python3 tools/lib_add.py staging/<name>.<recipe|archetype|style>.json [category]   (recipe needs a category folder)
  python3 tools/lib_add.py staging/<name>.px
  python3 tools/lib_add.py --remove <kind> <name> [category]
Accepted items are copied into registry/ (so later items and lint see them) and into output/ (the deliverable)."""
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REF = ROOT / "tools" / "arcforge_ref.py"
KEY = {"recipe": "recipe", "archetype": "archetype", "style": "style_pack"}


def run(*args):
    p = subprocess.run([sys.executable, str(REF), *args], capture_output=True, text=True, cwd=ROOT)
    try:
        return json.loads(p.stdout)
    except Exception:
        return {"status": "CRASH", "diagnostics": [{"severity": "error", "code": "CRASH", "path": "",
                                                    "message": (p.stdout + p.stderr)[-1500:]}]}


def show(diags, limit=12):
    for d in diags[:limit]:
        print(json.dumps({k: d[k] for k in ("code", "path", "message", "expected", "suggestions", "fix") if k in d}))
    if len(diags) > limit:
        print(f"... {len(diags) - limit} more")


def dest_rel(kind, base, cat):
    if kind == "archetype":
        return Path("registry/archetypes") / base
    if kind == "style":
        return Path("registry/styles") / base
    return Path("registry/recipes") / cat / base


def log(line):
    (ROOT / "output").mkdir(exist_ok=True)
    with open(ROOT / "output" / "accepted.txt", "a") as fh:
        fh.write(line + "\n")


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if a[0] == "--remove":
        kind, name = a[1], a[2]
        cat = a[3] if len(a) > 3 else ""
        if kind == "px":
            paths = [ROOT / "examples" / "fragments" / f"{name}.px", ROOT / "output" / "fragments" / f"{name}.px"]
        else:
            rel = dest_rel(kind, f"{name}.{kind}.json", cat)
            paths = [ROOT / rel, ROOT / "output" / rel]
        for p in paths:
            p.unlink(missing_ok=True)
        print("removed", name)
        return
    f = Path(a[0])
    cat = a[1] if len(a) > 1 else ""
    if f.suffix == ".px":
        sys.path.insert(0, str(ROOT / "tools"))
        import arcforge_ref as A
        try:
            A.parse_px(f.read_text())
        except A.PxError as e:
            print(f"REJECTED: {f.name} line {e.line}: {e.msg}")
            sys.exit(1)
        for d in (ROOT / "examples" / "fragments", ROOT / "output" / "fragments"):
            d.mkdir(parents=True, exist_ok=True)
            shutil.copy(f, d / f.name)
        log(f"px\t{f.stem}")
        print("ACCEPTED", f.name, "(format checked only; legend values are checked when an asset uses it)")
        return
    parts = f.name.split(".")
    if len(parts) != 3 or parts[1] not in KEY or parts[2] != "json":
        sys.exit("name the file <name>.<recipe|archetype|style>.json")
    name, kind = parts[0], parts[1]
    if kind == "recipe" and not cat:
        sys.exit("recipe needs a category folder, e.g. humanoid (match the sibling recipes)")
    try:
        doc = json.loads(f.read_text())
    except Exception as e:
        sys.exit(f"REJECTED: invalid JSON: {e}")
    if doc.get(KEY[kind]) != name:
        sys.exit(f"REJECTED: file name '{name}' must equal the '{KEY[kind]}' field (found {doc.get(KEY[kind])!r})")
    r = run("sweep" if kind == "archetype" else "validate", str(f))
    if r.get("status") != "PASS":
        print("REJECTED before registry:")
        show(r.get("diagnostics", []))
        sys.exit(1)
    rel = dest_rel(kind, f.name, cat)
    (ROOT / rel).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(f, ROOT / rel)
    lr = run("lint")
    errs = [d for d in lr.get("diagnostics", [])
            if d.get("severity") == "error" and (name in d.get("path", "") or d.get("code") == "CRASH")]
    if errs:
        (ROOT / rel).unlink()
        print("REJECTED by lint (removed from registry):")
        show(errs)
        sys.exit(1)
    out = ROOT / "output" / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(f, out)
    log(f"{kind}\t{name}\t{cat}")
    print("ACCEPTED", rel)


if __name__ == "__main__":
    main()
