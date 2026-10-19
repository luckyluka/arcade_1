#!/usr/bin/env python3
"""arc1-assetc — compile sprite art into a generated C header (Wave 1, v0).

Contract (docs/ROADMAP_CREATION_FLOW.md card 1.1):

    python3 tools/arc1_assetc.py --assets <assets.json> --out-dir <dir> [--report]

    exit 0 = compiled; 1 = validation error (stderr, one per asset);
    2 = usage/IO error

assets.json is self-describing ("format": 1):

    { "format": 1,
      "palettes": { "world": { "bank": 3, "colors": ["#000000", "#F8D8A8"] } },
      "sprites":  [ { "name": "player", "file": "art/player.png",
                      "palette": "world", "frames": 6 } ] }

Sprite images are horizontal strips: image width = frames x frame width.
The output header is byte-deterministic (no timestamps, fixed
16-bytes-per-line hex, names emitted in sorted order). The committed header
is the build truth and this tool is the master: regenerate, never hand-edit.

Palette semantics (card 1.2): a palette fills one device bank — colors[i]
lands at PALETTE[bank*16 + i], at most 16 colors, bank 0..15. The device
never DRAWS nibble 0 (transparent), so colors[0] is the declared key /
transparent slot and colors[1..15] are the drawable colours; art
conventions keep at most 15 colours plus transparent per bank.
RGB565 = (r>>3)<<11 | (g>>2)<<5 | (b>>3) — device upload semantics.
"""
import argparse
import json
import os
import struct
import sys
import zlib

FORMAT = 1
ROM_BUDGET = 262144    # CHEATSHEET: .arc1 payload 1..262144 (ROM size)

# Errors are collected per asset (exactly one line per failing asset) and
# printed together, so one run shows every broken entry, not just the first.
errors = []
_errored_assets = set()


def fail_asset(where, msg):
    """Record one error per asset: the first failure of an asset wins."""
    if where in _errored_assets:
        return
    _errored_assets.add(where)
    errors.append("%s: %s" % (where, msg))


class AutoPaletteError(Exception):
    """An auto palette exceeded the 15 drawable slots (ROADMAP_V3 4.2)."""


def rgb565(r, g, b):
    """Device upload semantics (CHEATSHEET palette section)."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def parse_hex_color(text):
    """Strict #RRGGBB -> (r, g, b); None when the shape is wrong."""
    if not isinstance(text, str) or len(text) != 7 or text[0] != "#":
        return None
    try:
        v = int(text[1:], 16)
    except ValueError:
        return None
    return (v >> 16 & 0xFF, v >> 8 & 0xFF, v & 0xFF)


def build_palette(name, spec):
    """Validate one named palette; return {"bank", "entries"} or None.

    entries is the full 16-slot bank in device order (index 0 = transparent
    slot value 0). Teaching errors are pinned by cards 1.2 / ROADMAP_V3 4.2.
    """
    where = "palette %s" % name
    if not isinstance(spec, dict):
        fail_asset(where, 'must be an object with "bank" and "colors"')
        return None

    bank = spec.get("bank")
    if not isinstance(bank, int) or isinstance(bank, bool):
        fail_asset(where, 'missing or non-integer "bank" — set "bank" to 0..15')
        return None
    if not 0 <= bank <= 15:
        fail_asset(where, "bank %d out of range 0..15" % bank)
        return None

    colors = spec.get("colors")
    if not isinstance(colors, list):
        fail_asset(where, 'missing or non-array "colors" — list "#RRGGBB" strings')
        return None
    if len(colors) > 16:
        fail_asset(where,
                   "%d colors (max 16) — merge colors or add a second bank"
                   % len(colors))
        return None

    entries = []
    seen_at = {}
    key_rgb = None
    for i, text in enumerate(colors):
        rgb = parse_hex_color(text)
        if rgb is None:
            fail_asset(where, 'bad color %s — use #RRGGBB' % json.dumps(text))
            return None
        if i == 0:
            key_rgb = rgb
        val = rgb565(*rgb)
        if val in seen_at:
            fail_asset(where,
                       "duplicate color %s at indices %d and %d — list each "
                       "color once" % (text, seen_at[val], i))
            return None
        seen_at[val] = i
        entries.append(val)

    entries += [0] * (16 - len(entries))
    return {"bank": bank, "entries": entries,
            "count": len(colors),
            # The key is matched on the AUTHORED 8-bit colour: a colour that
            # merely collapses onto it through >>3 must fail loudly instead
            # of silently vanishing (F3: no silent masking).
            "key_rgb": key_rgb}


def collect_auto(sprite_name, pixels):
    """Build a "palette": "auto" bank from pixel values in first-appearance
    order (row-major over all frames).

    `pixels` yields the RGB565 value of every pixel in order, or None for a
    transparent one (skipped). Assigns indices 1..15. All distinct colours
    are counted first so the teaching error reports the art's true total
    (ROADMAP_V3 4.2 shows "19 colours", not "16").
    """
    colors = []
    for p in pixels:
        if p is None:
            continue
        if p not in colors:
            colors.append(p)
    if len(colors) > 15:
        raise AutoPaletteError(
            'sprite %s: %d colours (max 15 + transparent) — reduce '
            'colours, use "quantize": "nearest" with an explicit '
            'palette, or split the sheet' % (sprite_name, len(colors)))
    return {"entries": [0] + colors + [0] * (15 - len(colors)),
            "count": 1 + len(colors), "key_rgb": None}


# --- image loading (card 1.3) ------------------------------------------------

class ImageError(Exception):
    """IO-class failure: unreadable/corrupt input -> exit 2."""


class UnsupportedImage(Exception):
    """Valid file, unsupported shape -> validation error (exit 1)."""


_PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
_PNG_CHANNELS = {0: 1, 2: 3, 3: 1, 6: 4}   # gray, RGB, palette, RGBA


def _png_unfilter(raw, w, h, bpp):
    """Reverse the five PNG scanline filters; returns raw channel bytes."""
    stride = w * bpp
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _ in range(h):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if f == 0:
            pass
        elif f == 1:                     # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:                     # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:                     # Average
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:                     # Paeth
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        else:
            raise ImageError("unknown PNG filter %d in scanline" % f)
        out += line
        prev = line
    return bytes(out)


def decode_png(data, label):
    """PNG (non-interlaced, 8-bit) -> (w, h, [(r, g, b, a), ...]) row-major."""
    if data[:8] != _PNG_MAGIC:
        raise ImageError("%s is not a PNG (bad signature)" % label)
    ihdr = plte = trns = None
    idat = []
    pos = 8
    while pos + 8 <= len(data):
        ln = int.from_bytes(data[pos:pos + 4], "big")
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if len(body) != ln:
            raise ImageError("%s is truncated (%s chunk)" % (label, tag.decode()))
        if tag == b"IHDR":
            ihdr = body
        elif tag == b"PLTE":
            plte = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
        pos += 12 + ln
    if ihdr is None or len(ihdr) != 13:
        raise ImageError("%s has no IHDR chunk" % label)
    w, h, depth, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", ihdr)
    if depth != 8 or inter != 0 or comp != 0 or filt != 0:
        raise UnsupportedImage(
            "%s is a %d-bit %sinterlaced PNG — need non-interlaced 8-bit "
            "(gray/RGB/palette/RGBA)"
            % (label, depth, "" if inter == 0 else "interlaced "))
    if ctype not in _PNG_CHANNELS:
        raise UnsupportedImage(
            "%s has PNG color type %d — need 0/2/3/6 (gray/RGB/palette/RGBA)"
            % (label, ctype))
    try:
        raw = zlib.decompress(b"".join(idat))
    except zlib.error as e:
        raise ImageError("%s has corrupt image data: %s" % (label, e))
    bpp = _PNG_CHANNELS[ctype]
    if len(raw) != h * (1 + w * bpp):
        raise ImageError("%s image data is %d bytes, want %d"
                         % (label, len(raw), h * (1 + w * bpp)))
    chan = _png_unfilter(raw, w, h, bpp)

    px = []
    if ctype == 0:                       # gray
        for i in range(w * h):
            g = chan[i]
            a = 0 if (trns and len(trns) >= 2 and g == int.from_bytes(trns[:2], "big")) else 255
            px.append((g, g, g, a))
    elif ctype == 2:                     # RGB
        for i in range(w * h):
            r, g, b = chan[i * 3:i * 3 + 3]
            a = 0 if (trns and len(trns) >= 6 and
                      (r, g, b) == tuple(trns[:6])) else 255
            px.append((r, g, b, a))
    elif ctype == 3:                     # palette indices
        if plte is None or len(plte) % 3:
            raise ImageError("%s palette image has no PLTE" % label)
        n = len(plte) // 3
        for i in range(w * h):
            idx = chan[i]
            if idx >= n:
                raise ImageError("%s pixel uses palette index %d but PLTE has %d"
                                 % (label, idx, n))
            r, g, b = plte[idx * 3:idx * 3 + 3]
            a = trns[idx] if (trns and idx < len(trns)) else 255
            px.append((r, g, b, a))
    else:                                 # RGBA
        for i in range(w * h):
            px.append(tuple(chan[i * 4:i * 4 + 4]))
    return (w, h, px)


def decode_ppm(data, label):
    """PPM P6 (maxval 255) -> (w, h, [(r, g, b, 255), ...]) row-major."""
    if not data.startswith(b"P6"):
        raise ImageError("%s is not a P6 PPM (bad magic)" % label)

    tokens = []
    i = 2
    while len(tokens) < 3 and i < len(data):
        c = data[i:i + 1]
        if c == b"#":
            while i < len(data) and data[i:i + 1] != b"\n":
                i += 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < len(data) and not data[j:j + 1].isspace():
                j += 1
            tokens.append(data[i:j])
            i = j
    if len(tokens) < 3:
        raise ImageError("%s has an incomplete PPM header" % label)
    try:
        w, h, maxval = (int(t) for t in tokens)
    except ValueError:
        raise ImageError("%s has a malformed PPM header" % label)
    if maxval != 255:
        raise UnsupportedImage("%s is a maxval-%d PPM — re-export as maxval 255"
                               % (label, maxval))
    i += 1                                # exactly one whitespace after maxval
    need = w * h * 3
    if len(data) < i + need:
        raise ImageError("%s is truncated (%d pixel bytes, want %d)"
                         % (label, max(0, len(data) - i), need))
    body = data[i:i + need]
    px = [(body[k], body[k + 1], body[k + 2], 255) for k in range(0, need, 3)]
    return (w, h, px)


_image_cache = {}


def load_image(path):
    """Decode an image by magic (PNG or P6 PPM); cached per path."""
    if path in _image_cache:
        return _image_cache[path]
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise ImageError("cannot read %s: %s" % (path, e.strerror))
    if data[:8] == _PNG_MAGIC:
        out = decode_png(data, path)
    elif data[:2] == b"P6":
        out = decode_ppm(data, path)
    else:
        raise ImageError(
            "%s is neither a PNG nor a P6 PPM — export the strip as 8-bit "
            "PNG or binary PPM" % path)
    _image_cache[path] = out
    return out


def load_document(path):
    """Read assets.json. IO problems exit 2; content problems exit 1."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        sys.stderr.write("arc1-assetc: cannot read %s: %s\n" % (path, e.strerror))
        sys.exit(2)
    try:
        doc = json.loads(text)
    except ValueError as e:
        sys.stderr.write("arc1-assetc: %s is not valid JSON: %s\n" % (path, e))
        sys.exit(2)
    return doc


def validate_document(doc):
    """Structural checks shared by every later stage."""
    if not isinstance(doc, dict):
        fail_asset("assets.json", 'top level must be an object with "format"')
        return
    fmt = doc.get("format")
    if fmt != FORMAT:
        fail_asset("assets.json",
                   'format %d expected (got %s) — set "format": %d'
                   % (FORMAT, json.dumps(fmt), FORMAT))
    if not isinstance(doc.get("palettes", {}), dict):
        fail_asset("assets.json", '"palettes" must be an object keyed by palette name')
    if not isinstance(doc.get("sprites", []), list):
        fail_asset("assets.json", '"sprites" must be an array')


def parse_palettes(doc):
    """Validate every named palette; returns {name: palette} (valid ones)."""
    out = {}
    for name in sorted(doc.get("palettes", {})):
        pal = build_palette(name, doc["palettes"][name])
        if pal is not None:
            out[name] = pal
    return out


def sprite_label(index, sprite):
    """How a sprite is addressed in errors: by name, else by position."""
    if isinstance(sprite, dict) and isinstance(sprite.get("name"), str) \
            and sprite["name"]:
        return sprite["name"]
    return "#%d" % (index + 1)


def validate_sprites(doc, palettes):
    """Palette-side sprite checks: the palette field, auto/bank pairing,
    and the reference to a defined palette (card 1.2 + ROADMAP_V3 4.2)."""
    for i, sprite in enumerate(doc.get("sprites", [])):
        where = sprite_label(i, sprite)
        if not isinstance(sprite, dict):
            fail_asset(where, "each sprite must be an object")
            continue
        quant = sprite.get("quantize", "exact")
        if quant not in ("exact", "nearest"):
            fail_asset(where,
                       '"quantize" must be "exact" or "nearest" (got %s)'
                       % json.dumps(sprite.get("quantize")))
            continue
        pal_ref = sprite.get("palette")
        if not isinstance(pal_ref, str) or not pal_ref:
            fail_asset(where,
                       'missing "palette" — name a palette or use "palette": "auto"')
            continue
        if pal_ref == "auto":
            bank = sprite.get("bank")
            if not isinstance(bank, int) or isinstance(bank, bool):
                fail_asset(where,
                           '"palette": "auto" requires "bank" (0..15)')
                continue
            if not 0 <= bank <= 15:
                fail_asset(where, "bank %d out of range 0..15" % bank)
                continue
        else:
            if "bank" in sprite:
                fail_asset(where,
                           '"bank" is only valid with "palette": "auto" — '
                           'remove it, or set "palette": "auto"')
                continue
            if pal_ref not in palettes:
                fail_asset(where,
                           "palette %s not defined — add it under "
                           '"palettes" or use "palette": "auto"'
                           % json.dumps(pal_ref))


def _c_ident(s):
    """C identifier (ASCII only — the header derives names from it)."""
    if not s or not (s[0].isalpha() or s[0] == "_"):
        return False
    return all(c.isascii() and (c.isalnum() or c == "_") for c in s)


def _nearest_index(pal, r, g, b):
    """Nearest drawable colour by squared RGB distance (8-bit expansion of
    each RGB565 entry); ties go to the lowest index (ROADMAP_V3 4.2)."""
    entries = pal["entries"]
    best, bestd = None, None
    for idx in range(1, pal["count"]):
        e = entries[idx]
        er, eg, eb = (e >> 11) & 31, (e >> 5) & 63, e & 31
        er8 = (er << 3) | (er >> 2)
        eg8 = (eg << 2) | (eg >> 4)
        eb8 = (eb << 3) | (eb >> 2)
        d = (r - er8) ** 2 + (g - eg8) ** 2 + (b - eb8) ** 2
        if bestd is None or d < bestd:
            best, bestd = idx, d
    return best, bestd


def compile_sprites(doc, palettes, base_dir):
    """Load every sprite image, quantize it, slice it into frames.
    Returns (sprites, auto_palettes); errors go through fail_asset."""
    sprites = {}
    autos = {}

    for i, sprite in enumerate(doc.get("sprites", [])):
        where = sprite_label(i, sprite)
        if where in _errored_assets:
            continue                    # already failed schema validation

        name = sprite.get("name")
        if not isinstance(name, str) or not _c_ident(name):
            fail_asset(where,
                       'missing or invalid "name" — the header needs a C '
                       "identifier (letters, digits, _)")
            continue
        path = sprite.get("file")
        if not isinstance(path, str) or not path:
            fail_asset(where,
                       'missing "file" — path to the strip (PNG or P6 PPM), '
                       "relative to assets.json")
            continue
        frames = sprite.get("frames")
        if not isinstance(frames, int) or isinstance(frames, bool):
            fail_asset(where,
                       'missing or non-integer "frames" — how many frames the '
                       "strip holds (width = frames x frame width)")
            continue
        if frames < 1:
            fail_asset(where, "frames %d — a strip needs at least 1 frame" % frames)
            continue

        full = path if os.path.isabs(path) else os.path.join(base_dir, path)
        try:
            w, h, px = load_image(full)
        except UnsupportedImage as e:
            fail_asset(where, str(e))
            continue
        # ImageError propagates: unreadable/corrupt input is IO (exit 2).

        if w < 1 or h < 1:
            fail_asset(where, "image %dx%d — a strip needs at least 1x1 pixel"
                       % (w, h))
            continue
        if w % frames:
            fail_asset(where,
                       "image width %d not divisible by frames %d — sheets "
                       "are horizontal strips (width = frames x frame width)"
                       % (w, frames))
            continue
        fw = w // frames
        if fw % 2:
            fail_asset(where,
                       "frame width %d is odd (image %d / %d frames) — the "
                       "4-bit blit needs an even width, odd draws nothing; "
                       "pad every frame by one pixel" % (fw, w, frames))
            continue

        # Resolve the palette (schema was validated by card 1.2).
        ref = sprite["palette"]
        if ref == "auto":
            if name in palettes:
                fail_asset(where,
                           'auto palette clashes with the declared palette '
                           '"%s" — rename one of them' % name)
                continue
            try:
                pal = collect_auto(name, (None if a == 0 else rgb565(r, g, b)
                                          for (r, g, b, a) in px))
            except AutoPaletteError as e:
                fail_asset(where, str(e))
                continue
            pal = dict(pal, bank=sprite["bank"])
            autos[name] = pal
            pal_ref = name
        else:
            pal = palettes[ref]
            pal_ref = ref

        # Quantize every pixel to a nibble: alpha 0 (or the declared key
        # color, colors[0]) -> 0 (transparent; the blit never draws it).
        mode = sprite.get("quantize", "exact")
        exact_map = {pal["entries"][k]: k for k in range(1, pal["count"])}
        indexed = []
        maxerr = 0
        npix = 0
        err = None
        for idx, (r, g, b, a) in enumerate(px):
            if a == 0 or (pal["key_rgb"] is not None and (r, g, b) == pal["key_rgb"]):
                indexed.append(0)
                continue
            if pal["count"] <= 1:
                err = ("pixel (%d,%d) #%02X%02X%02X not in palette '%s' — "
                       "no drawable colours; add at least one color after "
                       "the key" % (idx % w, idx // w, r, g, b, pal_ref))
                break
            v = rgb565(r, g, b)
            best, d = _nearest_index(pal, r, g, b)
            if mode == "exact" and v not in exact_map:
                err = ("pixel (%d,%d) #%02X%02X%02X not in palette '%s' "
                       "(nearest #%06X, d=%d) — fix the art or extend the "
                       "palette"
                       % (idx % w, idx // w, r, g, b, pal_ref,
                          _expand565(pal["entries"][best]), d))
                break
            if mode == "nearest":
                npix += 1
                if d > maxerr:
                    maxerr = d
                indexed.append(best)
            else:
                indexed.append(exact_map[v])
        if err is not None:
            fail_asset(where, err)
            continue

        # Slice the strip into frames; two pixels per byte, left in the
        # HIGH nibble (src/arcade1/video.c M27).
        stride = (fw // 2) * h
        data = bytearray()
        for f in range(frames):
            x0 = f * fw
            for y in range(h):
                row = indexed[y * w + x0:y * w + x0 + fw]
                for k in range(0, fw, 2):
                    data.append((row[k] << 4) | row[k + 1])
        assert len(data) == stride * frames

        sprites[name] = {"w": fw, "h": h, "frames": frames,
                         "stride": stride, "data": bytes(data),
                         "palette": pal_ref, "quantize": mode,
                         "maxerr": maxerr, "npix": npix}
    return sprites, autos


def _expand565(v):
    """RGB565 -> #RRGGBB the way the device expands it on scanout."""
    er, eg, eb = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (((er << 3) | (er >> 2)) << 16) | (((eg << 2) | (eg >> 4)) << 8) | \
        ((eb << 3) | (eb >> 2))


def check_banks(palettes, autos):
    """Every emitted palette needs its own device bank (a collision would
    silently overwrite entries at upload time)."""
    owner = {}
    for name, pal in sorted(palettes.items()):
        b = pal["bank"]
        if b in owner:
            fail_asset(name, 'bank %d is already used by "%s" — give each '
                             "palette its own bank" % (b, owner[b]))
        else:
            owner[b] = name
    for name, pal in sorted(autos.items()):
        b = pal["bank"]
        if b in owner:
            fail_asset(name, 'bank %d is already used by "%s" — give each '
                             "palette its own bank" % (b, owner[b]))
        else:
            owner[b] = name


def emit_header(sprites, palettes, out_path):
    """Write the generated header (card 1.4 contract): one banner comment
    listing every sprite with its palette, #defines per sprite (sorted),
    sheet arrays, then palette arrays — 16 hex values per line, no
    timestamps, ever."""
    banner = ["/* generated by tools/arc1_assetc.py (format 1) — DO NOT EDIT."]
    for name in sorted(sprites):
        s = sprites[name]
        pal = palettes[s["palette"]]
        banner.append(" * %s: %dx%d x%d frames, 4-bit; palette %s (bank %d)"
                      % (name, s["w"], s["h"], s["frames"],
                         s["palette"], pal["bank"]))
    if sprites:
        banner.append(" */")
        text = "\n".join(banner) + "\n\n"
    else:
        text = banner[0] + " */\n\n"

    for name in sorted(sprites):
        s = sprites[name]
        macro = name.upper()
        text += "#define %s_W %d\n" % (macro, s["w"])
        text += "#define %s_H %d\n" % (macro, s["h"])
        text += "#define %s_FRAMES %d\n" % (macro, s["frames"])
        text += "#define %s_STRIDE %d        /* (w/2)*h bytes per frame */\n" % (
            macro, s["stride"])
        text += "static const unsigned char %s_sheet[%d] = {\n" % (
            name, len(s["data"]))
        hexs = ["0x%02x" % b for b in s["data"]]
        for k in range(0, len(hexs), 16):
            text += "    " + ", ".join(hexs[k:k + 16]) + ",\n"
        text += "};\n\n"

    for name in sorted(palettes):
        p = palettes[name]
        text += "static const unsigned short %s_palette[16] = {\n" % name
        text += "    " + ", ".join("0x%04x" % v for v in p["entries"]) + ",\n"
        text += "};\n\n"

    try:
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(text)
    except OSError as e:
        raise ImageError("cannot write %s: %s" % (out_path, e.strerror))


def print_report(result):
    """Greppable KEY lines (card 1.4): SPRITE / QUANT / PALETTE / ROM."""
    sprites, palettes = result["sprites"], result["palettes"]
    total = 0
    for name in sorted(sprites):
        s = sprites[name]
        n = len(s["data"])
        total += n
        print("SPRITE %s %dx%d x%d = %d B"
              % (name, s["w"], s["h"], s["frames"], n))
        if s["quantize"] == "nearest":
            print("QUANT %s maxerr=%d pixels=%d"
                  % (name, s["maxerr"], s["npix"]))
    for name in sorted(palettes):
        total += 32
        print("PALETTE %s bank %d = 32 B" % (name, palettes[name]["bank"]))
    print("ROM assets %d / %d (%.1f %%)"
          % (total, ROM_BUDGET, 100.0 * total / ROM_BUDGET))


def compile_assets(doc, out_dir, report, base_dir, stem):
    """Validate then compile every asset to out_dir/<stem>.h."""
    validate_document(doc)
    if errors:
        return None                   # document-level errors: stop here
    palettes = parse_palettes(doc)
    validate_sprites(doc, palettes)
    if errors:
        return None
    sprites, autos = compile_sprites(doc, palettes, base_dir)
    if errors:
        return None
    all_pals = dict(palettes)
    all_pals.update(autos)
    check_banks(all_pals, {})
    if errors:
        return None
    total = sum(len(s["data"]) for s in sprites.values()) + 32 * len(all_pals)
    if total > ROM_BUDGET:
        fail_asset("assets.json",
                   "ROM assets %d / %d — over budget: reduce the art "
                   "(fewer frames or colours) or split the manifest"
                   % (total, ROM_BUDGET))
        return None
    emit_header(sprites, all_pals, os.path.join(out_dir, stem + ".h"))
    return {"sprites": sprites, "palettes": all_pals, "total": total}


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="arc1-assetc",
        description="compile assets.json (format 1) into a generated C header")
    ap.add_argument("--assets", required=True, metavar="FILE",
                    help="asset manifest, JSON, format 1")
    ap.add_argument("--out-dir", required=True, metavar="DIR",
                    help="directory for the generated header(s)")
    ap.add_argument("--report", action="store_true",
                    help="print greppable KEY lines (SPRITE/PALETTE/ROM/QUANT)")
    a = ap.parse_args(argv)          # usage errors exit 2

    doc = load_document(a.assets)    # IO errors exit 2
    base_dir = os.path.dirname(os.path.abspath(a.assets))
    stem = os.path.splitext(os.path.basename(a.assets))[0]
    try:
        result = compile_assets(doc, a.out_dir, a.report, base_dir, stem)
    except ImageError as e:           # unreadable/corrupt images: exit 2
        sys.stderr.write("arc1-assetc: %s\n" % e)
        return 2

    if errors:
        for e in errors:
            sys.stderr.write("arc1-assetc: %s\n" % e)
        return 1
    if a.report and result is not None:
        print_report(result)          # greppable KEY lines on stdout
    return 0


if __name__ == "__main__":
    sys.exit(main())
