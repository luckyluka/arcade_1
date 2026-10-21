#!/usr/bin/env python3
"""tools/measure_round6.py — the clean-room round's metrics (card 7.8).

The card lists the success metrics: the attempts to the first compliant
asset, the number of DOC GAPs, whether art/CRITIQUE.md was used, whether
any hand-edited generated file exists (must be none), that `arc1-art
check` is clean on all the assets, and that the game is golden-locked via
card 4.2's `arcade1 golden`.

This script measures the OBJECTIVE ones and reports the subjective ones
from the round's log. It does not judge the round — it hands the operator
the numbers, and prints the log's gaps so they can be classified.

    python3 tools/measure_round6.py [round-dir] [--report FILE]

Exit: 0 when every objective check passes, 1 otherwise.
"""

import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build", "arcade1")
ART = os.path.join(ROOT, "tools", "arc1-art")


def run(*args, **kw):
    return subprocess.run(list(args), capture_output=True, text=True,
                          cwd=kw.get("cwd", ROOT))


def check_assets(round_dir):
    """Every asset passes `arc1-art check`."""
    results = []
    manifest = os.path.join(round_dir, "assets.json")
    if not os.path.isfile(manifest):
        return [{"asset": "(no manifest)", "ok": False,
                 "why": "no assets.json in the round folder"}]
    with open(manifest) as f:
        doc = json.load(f)
    for sprite in doc.get("sprites", []):
        path = os.path.join(round_dir, sprite["file"])
        if not os.path.isfile(path):
            results.append({"asset": sprite["name"], "ok": False,
                            "why": "the strip is missing"})
            continue
        # the frame size: the sprite's own frame (the strip is frames wide)
        name = os.path.basename(path)
        fw = fh = 0
        try:
            with open(path, "rb") as f:
                data = f.read()
            import struct
            w, h = struct.unpack(">II", data[16:24])
            fw = w // sprite.get("frames", 1)
            fh = h
        except Exception as e:                       # pragma: no cover
            results.append({"asset": name, "ok": False, "why": str(e)})
            continue
        r = run(ART, "check", path, "--frame", "%dx%d" % (fw, fh))
        results.append({"asset": name, "ok": r.returncode == 0,
                        "why": "" if r.returncode == 0
                        else r.stderr.strip().split("\n")[0]})
    return results


def check_regenerated(round_dir):
    """No generated file was hand-edited.

    The round is copied to a temporary folder, every generator in it is run
    again there, the asset compiler is run again, and the two trees are
    compared file by file: if anything differs, a generated file was
    hand-edited (or the build is not reproducible)."""
    import filecmp
    import shutil
    import tempfile

    tmp = tempfile.mkdtemp(prefix="arcade1-round6-")
    copy = os.path.join(tmp, "round")
    shutil.copytree(round_dir, copy)
    try:
        for dirpath, dirs, files in os.walk(copy):
            dirs[:] = [d for d in dirs if d != "__pycache__"]
            for name in sorted(files):
                if name.endswith(".py"):
                    run(sys.executable, os.path.join(dirpath, name),
                        cwd=dirpath)
        manifest = os.path.join(copy, "assets.json")
        if os.path.isfile(manifest):
            run(sys.executable, os.path.join(ROOT, "tools", "arc1_assetc.py"),
                "--assets", manifest, "--out-dir", copy, cwd=copy)

        # the files that are NOT generated: the agent's own work
        skip = ("game.c", "ROUND_LOG.md", "BRIEF.md", "in.txt")
        found = []
        for dirpath, dirs, files in os.walk(copy):
            dirs[:] = [d for d in dirs if d != "__pycache__"]
            for name in sorted(files):
                if name in skip or name.endswith((".arc1", ".syms", ".golden",
                                                   ".elf", ".log", ".pyc")):
                    continue
                fresh = os.path.join(dirpath, name)
                orig = os.path.join(round_dir, os.path.relpath(fresh, copy))
                if not os.path.isfile(orig):
                    found.append({"file": os.path.relpath(fresh, ROOT),
                                  "ok": False, "why": "it is new"})
                elif not filecmp.cmp(fresh, orig, shallow=False):
                    found.append({"file": os.path.relpath(orig, ROOT),
                                  "ok": False,
                                  "why": "it differs from a fresh build — a "
                                         "generated file was hand-edited"})
        return found
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def check_golden(round_dir):
    """The game is golden-locked with the CLI's own `golden` command and
    the golden passes."""
    image = None
    for name in sorted(os.listdir(round_dir)):
        if name.endswith(".arc1"):
            image = os.path.join(round_dir, name)
            break
    if image is None:
        return {"ok": False, "why": "no .arc1 image in the round folder"}
    golden = None
    for name in sorted(os.listdir(round_dir)):
        if name.endswith(".golden") or name.endswith(".golden.txt"):
            golden = os.path.join(round_dir, name)
            break
    if golden is None:
        return {"ok": False, "why": "no golden in the round folder"}
    script = None
    for name in sorted(os.listdir(round_dir)):
        if name.endswith(".txt") and "in" in name:
            script = os.path.join(round_dir, name)
            break
    if script is None:
        return {"ok": False, "why": "no input script in the round folder"}
    r = run(CLI, "test", image, "--input", script, "--golden", golden)
    return {"ok": r.returncode == 0,
            "why": "" if r.returncode == 0 else r.stdout.strip()[-200:]}


def read_log(round_dir):
    """The subjective metrics, from the round's log."""
    path = os.path.join(round_dir, "ROUND_LOG.md")
    if not os.path.isfile(path):
        return {"gaps": None, "first_asset": None, "critique_used": None,
                "why": "no ROUND_LOG.md"}
    text = open(path).read()
    # the DOC GAP section only: its numbered items
    section = ""
    m = re.search(r"^##+\s*DOC GAPs?\s*$(.*?)(^##|\Z)", text,
                  re.M | re.S)
    if m:
        section = m.group(1)
    gaps = len(re.findall(r"^\s*\d+\.\s+\S", section, re.M))
    critique = bool(re.search(r"CRITIQUE", text))
    first = re.search(r"first compliant asset[^\n]*\n(.*)", text, re.I)
    return {"gaps": gaps, "critique_used": critique,
            "first_asset": (first.group(1).strip() if first else None),
            "log_lines": len(text.split("\n"))}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    round_dir = args[0] if args else os.path.join(ROOT, "round6")
    round_dir = os.path.abspath(round_dir)
    if not os.path.isdir(round_dir):
        sys.stderr.write("measure_round6: no such folder: %s\n" % round_dir)
        return 2
    if not os.path.isfile(CLI):
        sys.stderr.write("measure_round6: no CLI at %s (make build/arcade1)\n"
                         % CLI)
        return 2

    assets = check_assets(round_dir)
    golden = check_golden(round_dir)
    regenerated = check_regenerated(round_dir)
    log = read_log(round_dir)

    report = {
        "round": os.path.relpath(round_dir, ROOT),
        "assets": assets,
        "assets_clean": all(a["ok"] for a in assets) and bool(assets),
        "golden": golden,
        "generators": regenerated,
        "no_hand_edited_generated_file": not regenerated,
        "log": log,
    }
    report["ok"] = (report["assets_clean"] and golden["ok"] and
                    report["no_hand_edited_generated_file"] and
                    log.get("gaps") is not None)

    print(json.dumps(report, indent=1, ensure_ascii=False))
    if not report["ok"]:
        sys.stderr.write("measure_round6: the round is not complete:\n")
        for a in assets:
            if not a["ok"]:
                sys.stderr.write("  asset %s: %s\n" % (a["asset"], a["why"]))
        if not golden["ok"]:
            sys.stderr.write("  golden: %s\n" % golden["why"])
        for g in regenerated:
            sys.stderr.write("  generated %s: %s\n"
                             % (g.get("file", g.get("generator", "?")),
                                g["why"]))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
