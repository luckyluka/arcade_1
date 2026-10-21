"""rig — the rig animator (ROADMAP_V4 card 7.4).

A character is a list of **parts** and a **pose**. The parts say what
the body is made of; a pose says where every part is at that moment.
Animating is interpolating between poses.

    parts:  name, parent, pivot, shape, size/points, colour, z
    pose:   per part — an angle in whole degrees and an integer offset

The shapes are rasterised analytically (a pixel is inside the shape when
it is inside the shape in the part's own frame), so the pixels are crisp
and the rotation is exact at any angle — no resampling.

The output per state is one horizontal strip: every frame the same size,
the feet on the same row (the frames are aligned to the baseline), and
the horizontal anchor on the same column. The idle frame's opaque pixels
give the collision box, which is written into the assets.json fragment.

Interpolation is linear between the keyframes, and the result is rounded
to whole pixels with **round half away from zero** (`round_half_away`),
so a pose never lands on a banker's rounding surprise.

Deterministic by construction: no clock, no randomness, sorted iteration.
"""

import json
import math

from . import canvas as _canvas

__all__ = ["RigError", "Part", "Rig", "round_half_away", "build_rig",
           "render_frame", "state_frames", "collision_box", "write_strip",
           "write_contact_sheet"]


class RigError(Exception):
    """A teaching error: what is wrong and what to do about it."""


def round_half_away(x):
    """Round to the nearest integer, halves away from zero.

    Python's round() is banker's rounding (0.5 -> 0, 1.5 -> 2); a rig
    that interpolates poses would then breathe left and right. This is
    the rule the card asks for, and it is what every frame in the
    pipeline uses.
    """
    if x >= 0:
        return int(math.floor(x + 0.5))
    return int(math.ceil(x - 0.5))


# --------------------------------------------------------------------------
# The rig
# --------------------------------------------------------------------------

class Part:
    """One piece of the character."""

    def __init__(self, spec, index):
        self.name = spec.get("name")
        if not isinstance(self.name, str) or not self.name:
            raise RigError("a part has no name")
        self.parent = spec.get("parent", "root")
        self.pivot = tuple(spec.get("pivot", (0, 0)))
        self.shape = spec.get("shape")
        if self.shape not in ("capsule", "rect", "ellipse", "polygon", "root"):
            raise RigError("part %s: shape %r is not one of capsule, rect, "
                           "ellipse, polygon" % (self.name, self.shape))
        self.size = tuple(spec.get("size", (0, 0)))
        self.points = [tuple(p) for p in spec.get("points", [])]
        self.color = spec.get("color", 1)
        self.shade = spec.get("shade")      # an optional darker tone
        self.z = spec.get("z", index)
        self.spec = spec


class Rig:
    """The parts, and the poses that move them."""

    def __init__(self, spec):
        self.name = spec.get("name", "rig")
        self.width = spec.get("width", 24)
        self.height = spec.get("height", 32)
        self.origin = tuple(spec.get("origin", (self.width // 2,
                                                 self.height - 1)))
        self.baseline = spec.get("baseline", self.height - 1)
        self.outline = spec.get("outline", 0)
        raw = spec.get("parts", [])
        if not raw:
            raise RigError("the rig has no parts")
        self.parts = [Part(p, i) for i, p in enumerate(raw)]
        names = {p.name for p in self.parts}
        for part in self.parts:
            if part.parent != "root" and part.parent not in names:
                raise RigError("part %s: the parent %r is not a part"
                               % (part.name, part.parent))
        self.by_name = {p.name: p for p in self.parts}

    def hierarchy(self):
        """The parts in parent-first order (the transform chain). The draw
        order is the z order; the transform order is the tree."""
        order = []
        seen = set()

        def visit(part):
            if part.name in seen:
                return
            seen.add(part.name)
            if part.parent != "root" and part.parent in self.by_name:
                visit(self.by_name[part.parent])
            order.append(part)

        for part in sorted(self.parts, key=lambda p: (p.z, p.name)):
            visit(part)
        return order

    # -- the transforms ---------------------------------------------------
    def world(self, pose):
        """Every part's (origin, world angle) for one pose."""
        angles = {}
        origins = {}
        for part in self.hierarchy():
            angle = float(pose.get(part.name, {}).get("angle", 0))
            offset = pose.get(part.name, {}).get("offset", (0, 0))
            if part.parent == "root":
                px, py = self.origin
                parent_angle = 0.0
            else:
                px, py = origins[part.parent]
                parent_angle = angles[part.parent]
            # the pivot is in the parent's frame; the offset is in the
            # part's own (already rotated) frame
            rad = math.radians(parent_angle)
            vx, vy = part.pivot[0], part.pivot[1]
            rx = vx * math.cos(rad) - vy * math.sin(rad)
            ry = vx * math.sin(rad) + vy * math.cos(rad)
            rad2 = math.radians(angle)
            ox, oy = offset[0], offset[1]
            ox2 = ox * math.cos(rad2) - oy * math.sin(rad2)
            oy2 = ox * math.sin(rad2) + oy * math.cos(rad2)
            origins[part.name] = (px + rx + ox2, py + ry + oy2)
            angles[part.name] = parent_angle + angle
        return origins, angles

    def render(self, pose):
        """One frame of the rig, as a canvas."""
        c = _canvas.Canvas(self.width, self.height)
        origins, angles = self.world(pose)
        for part in sorted(self.parts, key=lambda p: (p.z, p.name)):
            ox, oy = origins[part.name]
            _draw_part(c, part, ox, oy, angles[part.name], part.color)
        if self.outline:
            # the outline pass runs last: the palette's outline colour
            # around every opaque pixel (the 4-neighbour rule)
            c.grow_outline(self.outline)
        return c

    def render_aligned(self, pose):
        """One frame, aligned to the baseline (the feet on one row)."""
        c = self.render(pose)
        lowest = lowest_opaque_row(c)
        if lowest is None:
            return c
        shift = self.baseline - lowest
        if shift:
            out = _canvas.Canvas(self.width, self.height)
            for y in range(self.height):
                ty = y + shift
                if 0 <= ty < self.height:
                    out.p[ty] = list(c.p[y])
            return out
        return c


def lowest_opaque_row(c):
    """The lowest row that holds an opaque pixel (None for an empty frame)."""
    for y in range(c.h - 1, -1, -1):
        if any(c.p[y]):
            return y
    return None


def _draw_part(c, part, ox, oy, angle, color):
    """Rasterise one part: a pixel is inside when it is inside in the
    part's own frame.

    A part may carry a "shade" tone: the shape is drawn twice — once in
    the shade colour and once, one pixel up and left (the light comes from
    the top left), in the main colour. What is left of the shade is the
    shadow side. It is the cheapest way to stop a flat shape from
    reading as a blob.
    """
    if part.shade:
        _draw_shape(c, part, ox, oy, angle, part.shade)
        _draw_shape(c, part, ox - 1, oy - 1, angle, color)
        return
    _draw_shape(c, part, ox, oy, angle, color)


def _draw_shape(c, part, ox, oy, angle, color):
    """The shape itself, rasterised analytically."""
    rad = math.radians(angle)
    cos_a, sin_a = math.cos(rad), math.sin(rad)

    def to_local(x, y):
        """A canvas pixel -> the part's local frame."""
        dx, dy = x - ox, y - oy
        return (dx * cos_a + dy * sin_a, -dx * sin_a + dy * cos_a)

    if part.shape == "root":
        return
    if part.shape == "capsule":
        if len(part.points) < 2:
            raise RigError("part %s: a capsule needs two points" % part.name)
        (x0, y0), (x1, y1) = part.points[0], part.points[1]
        r = part.size[0] / 2.0
        for y in range(c.h):
            for x in range(c.w):
                lx, ly = to_local(x, y)
                if _dist_to_segment(lx, ly, x0, y0, x1, y1) <= r + 0.25:
                    c.px(x, y, color)
    elif part.shape == "ellipse":
        rx, ry = part.size[0] / 2.0, part.size[1] / 2.0
        for y in range(c.h):
            for x in range(c.w):
                lx, ly = to_local(x, y)
                if rx <= 0 or ry <= 0:
                    continue
                if (lx / rx) ** 2 + (ly / ry) ** 2 <= 1.0:
                    c.px(x, y, color)
    elif part.shape == "rect":
        hw, hh = part.size[0] / 2.0, part.size[1] / 2.0
        for y in range(c.h):
            for x in range(c.w):
                lx, ly = to_local(x, y)
                if abs(lx) <= hw and abs(ly) <= hh:
                    c.px(x, y, color)
    elif part.shape == "polygon":
        if len(part.points) < 3:
            raise RigError("part %s: a polygon needs three points"
                           % part.name)
        for y in range(c.h):
            for x in range(c.w):
                lx, ly = to_local(x, y)
                if _point_in_polygon(lx, ly, part.points):
                    c.px(x, y, color)
    else:
        raise RigError("part %s: unknown shape %r" % (part.name, part.shape))


def _dist_to_segment(px, py, x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    if dx == 0 and dy == 0:
        return math.hypot(px - x0, py - y0)
    t = ((px - x0) * dx + (py - y0) * dy) / float(dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    return math.hypot(px - (x0 + t * dx), py - (y0 + t * dy))


def _point_in_polygon(px, py, points):
    """Even-odd test."""
    inside = False
    n = len(points)
    for i in range(n):
        x0, y0 = points[i]
        x1, y1 = points[(i + 1) % n]
        if (y0 <= py < y1) or (y1 <= py < y0):
            t = (py - y0) / float(y1 - y0)
            if px < x0 + (x1 - x0) * t:
                inside = not inside
    return inside


# --------------------------------------------------------------------------
# Poses and animations
# --------------------------------------------------------------------------

def _pose_of(keyframe):
    pose = keyframe.get("pose", {})
    for name, value in pose.items():
        if not isinstance(value, dict):
            raise RigError("the pose of %r is not an object" % name)
    return pose


def interpolate(pose_a, pose_b, t):
    """The pose between two poses at t (0..1), rounded to whole pixels."""
    out = {}
    names = sorted(set(pose_a) | set(pose_b))
    for name in names:
        a = pose_a.get(name, {})
        b = pose_b.get(name, {})
        angle = float(a.get("angle", 0)) + \
            (float(b.get("angle", 0)) - float(a.get("angle", 0))) * t
        oa = a.get("offset", (0, 0))
        ob = b.get("offset", (0, 0))
        ox = float(oa[0]) + (float(ob[0]) - float(oa[0])) * t
        oy = float(oa[1]) + (float(ob[1]) - float(oa[1])) * t
        out[name] = {"angle": round_half_away(angle),
                     "offset": (round_half_away(ox), round_half_away(oy))}
    return out


def state_frames(rig, anim):
    """The frames of one state: the keyframes sampled at n frames, the
    whole pixels rounded half away from zero."""
    keyframes = anim.get("keyframes", [])
    if len(keyframes) < 2:
        raise RigError("the animation %r needs at least two keyframes"
                       % anim.get("name"))
    n = int(anim.get("frames", 0))
    if n < 1:
        raise RigError("the animation %r has no frames" % anim.get("name"))
    times = [float(k.get("t", i / float(len(keyframes) - 1)))
             for i, k in enumerate(keyframes)]
    poses = [_pose_of(k) for k in keyframes]
    out = []
    for i in range(n):
        t = 0.0 if n == 1 else i / float(n - 1)
        # the keyframe segment t falls in
        j = 0
        while j < len(times) - 2 and t > times[j + 1]:
            j += 1
        span = times[j + 1] - times[j]
        local = 0.0 if span <= 0 else (t - times[j]) / span
        local = max(0.0, min(1.0, local))
        out.append(interpolate(poses[j], poses[j + 1], local))
    return out


def collision_box(frames):
    """The union of the opaque pixels of the first frame — the collision
    box the asset compiler writes into the fragment."""
    c = frames[0]
    minx, miny, maxx, maxy = c.w, c.h, -1, -1
    for y in range(c.h):
        for x in range(c.w):
            if c.p[y][x]:
                minx = min(minx, x)
                maxx = max(maxx, x)
                miny = min(miny, y)
                maxy = max(maxy, y)
    if maxx < 0:
        return (0, 0, 0, 0)
    return (minx, miny, maxx - minx + 1, maxy - miny + 1)


# --------------------------------------------------------------------------
# The output
# --------------------------------------------------------------------------

def write_strip(path, frames, palette):
    """Write the frames side by side (the strip the asset compiler wants)."""
    return _canvas.to_strip(frames, palette, path)


def write_contact_sheet(path, states, palette, scale=4, labels=None):
    """The review sheet: one ROW per state, every frame at 1x and at
    `scale`, with the frame numbers.

    The 1x row is what the machine shows; the scaled row is what a human
    needs to judge the art.
    """
    rows = [(name, frames) for name, frames in sorted(states.items())]
    if not rows:
        raise RigError("no states to sheet")
    w = max((f.w for _, frames in rows for f in frames), default=1)
    h = max((f.h for _, frames in rows for f in frames), default=1)
    gap = 2
    label_h = 6
    row_h = h * scale + gap + label_h + h + gap
    sheet_w = gap + max(sum(len(f) for _, f in rows) for _ in [0]) * (w * scale + gap)
    sheet_w = gap + sum(len(frames) for _, frames in rows) * (w * scale + gap)
    sheet_h = gap + len(rows) * row_h
    pixels = [[0] * sheet_w for _ in range(sheet_h)]

    def put(x, y, index):
        if 0 <= y < sheet_h and 0 <= x < sheet_w:
            pixels[y][x] = index

    y0 = gap
    for name, frames in rows:
        x = gap
        for number, frame in enumerate(frames, 1):
            for y in range(frame.h):
                for xx in range(frame.w):
                    v = frame.p[y][xx]
                    if not v:
                        continue
                    for sy in range(scale):
                        for sx in range(scale):
                            put(x + xx * scale + sx, y0 + y * scale + sy, v)
            # the 1x row, under the numbers
            for y in range(frame.h):
                for xx in range(frame.w):
                    v = frame.p[y][xx]
                    if v:
                        put(x + xx, y0 + h * scale + gap + label_h + y, v)
            _canvas_digits(pixels, x, y0 + h * scale + gap, number, 15)
            x += w * scale + gap
        y0 += row_h

    colours = _canvas.palette_rgb(palette["colors"][:16])
    raw = bytearray()
    for y in range(sheet_h):
        raw.append(0)
        for xx in range(sheet_w):
            v = pixels[y][xx]
            raw += bytes(colours[v] if v < len(colours) and colours[v]
                         else (0, 0, 0))
    return _canvas.write_indexed_png(path, raw, sheet_w, sheet_h)


def _canvas_digits(pixels, x, y, number, colour):
    from .pixmap import DIGITS
    for digit in str(number):
        glyph = DIGITS.get(digit, DIGITS[" "])
        for dy, bits in enumerate(glyph):
            for dx in range(3):
                if bits & (1 << (2 - dx)):
                    px, py = x + dx, y + dy
                    if 0 <= py < len(pixels) and 0 <= px < len(pixels[0]):
                        pixels[py][px] = colour
        x += 4


# --------------------------------------------------------------------------
# Building a rig from a preset
# --------------------------------------------------------------------------

def build_rig(preset, states=None):
    """Build every state of a rig preset: {state: [frames]}."""
    rig = Rig(preset)
    out = {}
    for name, anim in sorted(preset.get("animations", {}).items()):
        if states and name not in states:
            continue
        poses = state_frames(rig, anim)
        out[name] = [rig.render_aligned(pose) for pose in poses]
    return rig, out
