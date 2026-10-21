"""library — the asset library (ROADMAP_V4, after the clean-room rounds).

The rounds produced good art and then buried it: a village in `round6/`, a
lighthouse in `round7/`, five pixmaps in `art/px/`, two rigs in
`art/rigs/`. A later agent would never find it, and could not reuse it.

The library is the same art, **tidy and reusable**. One directory per
*pack*, and every pack is a unit with the same contract:

    art/library/<pack>/
        gen_<pack>.py   the generator (deterministic, stdlib only)
        <pack>.json     the manifest fragment: palettes, sprites, animations
        STYLE.md        the style sheet (grid, outline, light, palette, rules)
        strips/         the generated PNGs
        maps/           the generated map files (when the pack has maps)

The rules that make it a library and not a museum:

* **every pack is code** — the generator is the master, the strips are the
  build truth, and `arc1-art library --check` proves that regenerating a
  pack gives the very same bytes;
* **every pack says how it is meant to be used** — its style sheet, and
  the index, say the sizes, the anchor, the collision convention and the
  palette bank, so a pack is picked the way a kit is picked;
* **every pack is reviewed** — `art/reports/<pack>.md` carries the verdict
  the critique protocol asks for;
* **a pack is a fragment, not a game** — it declares what it offers; a
  game merges the packs it wants (`arc1-art merge`).

Deterministic by construction: no clock, no unseeded randomness, sorted
iteration.
"""

import json
import os
import re
import subprocess
import sys

__all__ = ["LibraryError", "PACKS", "pack_dir", "pack_generator",
           "regenerate_pack", "check_pack", "pack_assets", "build_index",
           "merge_packs"]


class LibraryError(Exception):
    """A teaching error: what is wrong and what to do about it."""


def _root():
    return os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))


def _library_dir():
    return os.path.join(_root(), "art", "library")


# The packs, in the order the index lists them. A pack is added here when
# it is added to the library.
PACKS = [
    "icons",
    "characters",
    "village",
    "lighthouse",
    "tiles",
    "effects",
    "ui",
    "backgrounds",
]


def pack_dir(name, root=None):
    """The pack's directory."""
    base = os.path.join(root or _root(), "art", "library")
    path = os.path.join(base, name)
    if not os.path.isdir(path):
        raise LibraryError("no such pack: %s (the packs are: %s)"
                           % (name, ", ".join(PACKS)))
    return path


def pack_generator(name, root=None):
    """The pack's generator script."""
    path = os.path.join(pack_dir(name, root), "gen_%s.py" % name)
    if not os.path.isfile(path):
        raise LibraryError("the pack %s has no generator (%s is missing)"
                           % (name, path))
    return path


def regenerate_pack(name, out_dir, root=None):
    """Run a pack's generator into `out_dir`; returns the files it wrote.

    The generator is called with --out-dir, so it can be regenerated
    anywhere (the library's own check uses a temporary directory).
    """
    script = pack_generator(name, root)
    r = subprocess.run([sys.executable, script, "--out-dir", out_dir],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise LibraryError("the generator of %s failed: %s"
                           % (name, (r.stderr or r.stdout).strip()[-400:]))
    return sorted(os.path.relpath(os.path.join(dirpath, f), out_dir)
                  for dirpath, _d, files in os.walk(out_dir) for f in files)


def check_pack(name, root=None):
    """Regenerate the pack into a temporary directory and compare it with
    the committed art, file by file.

    Returns (ok, differences) — the differences are the files that differ
    (or are missing, or are new).
    """
    import filecmp
    import shutil
    import tempfile

    base = pack_dir(name, root)
    tmp = tempfile.mkdtemp(prefix="arcade1-pack-")
    try:
        regenerate_pack(name, tmp, root)
        committed = {}
        for dirpath, _d, files in os.walk(base):
            for f in files:
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, base)
                if rel.endswith((".py", ".md")) or \
                        rel.startswith("pixmaps" + os.sep):
                    continue          # the sources, not the build output
                committed[rel] = full
        fresh = {}
        for dirpath, _d, files in os.walk(tmp):
            for f in files:
                full = os.path.join(dirpath, f)
                fresh[os.path.relpath(full, tmp)] = full
        problems = []
        for rel, path in sorted(committed.items()):
            if rel not in fresh:
                problems.append("%s: the generator does not produce it "
                                "any more" % rel)
            elif not filecmp.cmp(path, fresh[rel], shallow=False):
                problems.append("%s: a fresh build differs from the "
                                "committed one" % rel)
        for rel in sorted(fresh):
            if rel not in committed:
                problems.append("%s: the generator produces it, but it is "
                                "not committed" % rel)
        return (not problems), problems
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def pack_assets(name, root=None):
    """The pack's assets: the fragment's sprites and animations, with the
    strip's size and frame count."""
    base = pack_dir(name, root)
    fragment = os.path.join(base, "%s.json" % name)
    if not os.path.isfile(fragment):
        raise LibraryError("the pack %s has no manifest fragment (%s)"
                           % (name, fragment))
    with open(fragment) as f:
        doc = json.load(f)
    out = []
    roles = doc.get("roles", {})
    for sprite in doc.get("sprites", []):
        strip = os.path.join(base, sprite.get("file", ""))
        size = ""
        frames = sprite.get("frames", 1)
        if os.path.isfile(strip):
            import struct
            with open(strip, "rb") as f:
                data = f.read(24)
            w, h = struct.unpack(">II", data[16:24])
            size = "%dx%d" % (w // max(1, frames), h)
        out.append({"kind": roles.get(sprite.get("name"), "sprite"),
                    "name": sprite.get("name", "?"),
                    "file": sprite.get("file", ""), "size": size,
                    "frames": frames,
                    "palette": sprite.get("palette", "")})
    for anim in doc.get("animations", []):
        out.append({"kind": "animation", "name": anim.get("name", "?"),
                    "file": anim.get("sprite", ""), "size": "",
                    "frames": len(anim.get("frames", [])),
                    "palette": "%s ms" % anim.get("ms", "")})
    for m in doc.get("maps", []):
        out.append({"kind": "map", "name": m.get("name", "?"),
                    "file": m.get("file", ""), "size": "",
                    "frames": 0, "palette": ""})
    return out


def build_index(root=None):
    """The catalogue: art/library/INDEX.md.

    Every pack, what it offers, and how to use it.
    """
    base = os.path.join(root or _root(), "art", "library")
    lines = ["# The asset library",
              "",
              "Reusable asset packs. Every pack is **code**: a generator, a",
              "manifest fragment, a style sheet and the strips it produces —",
              "regenerate a pack and you get the same bytes.",
              "",
              "```sh",
              "make art-library          # regenerate every pack",
              "make art-library-check    # prove a fresh build is identical",
              "arc1-art merge <pack>...  # compose packs into a game's manifest",
              "```",
              "",
              "| pack | what it is for | assets |",
              "|---|---|---:|"]
    total = 0
    for name in PACKS:
        try:
            assets = pack_assets(name, root)
        except LibraryError as e:
            lines.append("| `%s` | **missing** (%s) | — |" % (name, e))
            continue
        total += len(assets)
        lines.append("| `%s` | %s | %d |"
                     % (name, _pack_summary(name, root), len(assets)))
    lines.append("")
    lines.append("**%d assets in %d packs.**" % (total, len(PACKS)))
    lines.append("")

    for name in PACKS:
        try:
            assets = pack_assets(name, root)
        except LibraryError:
            continue
        lines.append("## `%s`" % name)
        lines.append("")
        summary = _pack_summary(name, root)
        if summary:
            lines.append(summary)
            lines.append("")
        style = os.path.join(pack_dir(name, root), "STYLE.md")
        if os.path.isfile(style):
            # the style sheet's section headings — the index is a catalogue,
            # not a second copy of the sheet
            with open(style) as f:
                for line in f:
                    if line.startswith("## "):
                        lines.append("* %s" % line[3:].strip().lower())
            lines.append("")
        lines.append("| asset | kind | size | frames | palette |")
        lines.append("|---|---|---|---:|---|")
        for a in assets:
            lines.append("| `%s` | %s | %s | %s | %s |"
                         % (a["name"], a["kind"], a["size"] or "—",
                            a["frames"], a["palette"] or "—"))
        lines.append("")
        report = os.path.join(_root(), "art", "reports", "%s.md" % name)
        if os.path.isfile(report):
            with open(report) as f:
                text = f.read()
            m = re.search(r"^##+ Verdict\s*$(.*?)(^##|\Z)", text,
                          re.M | re.S)
            verdict = ""
            if m:
                for line in m.group(1).split("\n"):
                    if line.strip():
                        verdict = line.strip()
                        break
            if verdict:
                lines.append("**Review:** %s" % verdict)
                lines.append("")
    return "\n".join(lines)


def _pack_summary(name, root=None):
    """The pack's one-line description (from its fragment)."""
    fragment = os.path.join(pack_dir(name, root), "%s.json" % name)
    try:
        with open(fragment) as f:
            doc = json.load(f)
    except (OSError, ValueError):
        return ""
    return doc.get("description", "")


# --------------------------------------------------------------------------
# Merging packs into a game's manifest
# --------------------------------------------------------------------------

def merge_packs(names, out_path, root=None):
    """Compose the packs into ONE manifest a game can compile.

    The packs are independent, so their names and their palette banks can
    collide. The merge resolves both:

    * a **palette** keeps its bank if it is free, and moves to the next
      free one otherwise (the colours never change);
    * a **sprite** or **animation** keeps its name if it is unique, and
      gets the pack's name as a prefix otherwise.

    The result is a manifest like any other: `arc1-assetc` compiles it.
    """
    if not names:
        raise LibraryError("no packs to merge")
    manifest = {
        "format": 1,
        "description": "merged from the library: %s" % ", ".join(names),
        "palettes": {},
        "sprites": [],
        "animations": [],
        "maps": [],
    }
    out_dir = os.path.dirname(os.path.abspath(out_path))
    banks = {}
    sprite_names = set()
    anim_names = set()
    map_names = set()

    for name in names:
        fragment_path = os.path.join(pack_dir(name, root), "%s.json" % name)
        with open(fragment_path) as f:
            doc = json.load(f)

        # --- the palettes: keep the bank, or move to a free one ----------
        rename = {}
        for pal_name, pal in sorted(doc.get("palettes", {}).items()):
            bank = pal.get("bank", 0)
            if bank in banks and banks[bank] != pal_name:
                taken = set(banks)
                free = 0
                while free in taken:
                    free += 1
                bank = free
            banks[bank] = pal_name
            key = pal_name
            if key in manifest["palettes"]:
                key = "%s_%s" % (name, pal_name)
            manifest["palettes"][key] = {"bank": bank,
                                         "colors": list(pal["colors"])}
            rename[pal_name] = key

        # --- the sprites ---------------------------------------------------
        for sprite in doc.get("sprites", []):
            sprite = dict(sprite)
            if "palette" in sprite and sprite["palette"] in rename:
                sprite["palette"] = rename[sprite["palette"]]
            sname = sprite.get("name", "?")
            if sname in sprite_names:
                sname = "%s_%s" % (name, sname)
            sprite["name"] = sname
            sprite_names.add(sname)
            # the file path: the pack's own, relative to where the
            # merged manifest is written
            sprite["file"] = os.path.relpath(
                os.path.join(pack_dir(name, root), sprite["file"]), out_dir)
            manifest["sprites"].append(sprite)

        # --- the animations ------------------------------------------------
        for anim in doc.get("animations", []):
            anim = dict(anim)
            aname = anim.get("name", "?")
            if aname in anim_names:
                aname = "%s_%s" % (name, aname)
            anim["name"] = aname
            anim_names.add(aname)
            if anim.get("sprite") in sprite_names:
                anim["sprite"] = anim["sprite"]
            manifest["animations"].append(anim)

        # --- the maps (a pack may offer them) ------------------------------
        for m in doc.get("maps", []):
            m = dict(m)
            mname = m.get("name", "?")
            if mname in map_names:
                mname = "%s_%s" % (name, mname)
            m["name"] = mname
            map_names.add(mname)
            m["file"] = os.path.relpath(
                os.path.join(pack_dir(name, root), m["file"]), out_dir)
            manifest["maps"].append(m)

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")
    return manifest
