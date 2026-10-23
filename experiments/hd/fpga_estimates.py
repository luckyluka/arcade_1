#!/usr/bin/env python3
"""HD_EXPERIMENT.md card H2, step 2 — what a higher-resolution ARCADE-1 would
cost if it were built out of FPGA-side hardware.

usage: experiments/hd/fpga_estimates.py [measured.csv] [out.md]
  defaults: docs/experiments/hd-measurements.csv
            docs/experiments/hd-fpga-estimates.md

Reads the CSV measure.sh wrote and prints tables. Everything this script
outputs is ESTIMATED from the formulas it prints — no FPGA was built, no
device was timed. The only measured inputs are the CSV's `px_max` and the
resolution, and they are labelled as such wherever they are used.

Deterministic apart from the CSV's own bench_ms_* columns, which this script
does not read. stdlib only.
"""

import csv
import os
import sys

# The decision thresholds are the ones HD_EXPERIMENT.md §6 pins. They are not
# tuned here: the point of estimating first is to check them honestly.
BRAM_MBITS = (2, 4, 8)


def load(path):
    """(all data rows in file order, the ks seen sorted numerically as strings
    suitable for interpolation). Raises SystemExit if the CSV has no rows."""
    rows = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            rows.append(row)
    if not rows:
        raise SystemExit("fpga_estimates: %s has no data rows" % path)
    ks = []
    for r in rows:
        if r["k"] not in ks:
            ks.append(r["k"])
    ks.sort(key=int)
    return rows, ks


def per_k_max(rows, k, column):
    """Worst case across the games measured at this k: the row that decides
    whether the machine fits. Games are measured with the same script and the
    same frame count, so this is an apples-to-apples maximum."""
    vals = [float(r[column]) for r in rows if r["k"] == k]
    return max(vals) if vals else 0.0


def fits(bits, budget_mbit):
    """Does `bits` fit in `budget_mbit` Mbit of block RAM? 1 Mbit = 1e6 bits,
    the convention used throughout this file."""
    return "yes" if bits <= budget_mbit * 1_000_000 else "NO"


def f_mhz(hz):
    return "%.2f" % (hz / 1e6)


def f_mbit(bits):
    return "%.3f" % (bits / 1e6)


def f_mbps(bytes_per_s):
    return "%.2f" % (bytes_per_s / 1e6)


def throughput_table(rows, ks):
    out = []
    out.append("### Pixel throughput and the clock it implies (estimated)\n")
    out.append("| k | resolution | px written / frame (max) | "
               "pixel throughput @60 fps | clock, 1 px/clk | "
               "clock, 4 px/clk | clock, 8 px/clk |")
    out.append("|---|---|---:|---:|---:|---:|---:|")
    for k in ks:
        px_max = per_k_max(rows, k, "px_max")
        res = [r for r in rows if r["k"] == k][0]["resolution"]
        thr = px_max * 60
        out.append("| %s | %s | %.0f | %.3f Mpx/s | %s MHz | %s MHz | %s MHz |"
                   % (k, res, px_max, thr / 1e6,
                      f_mhz(thr / 1), f_mhz(thr / 4), f_mhz(thr / 8)))
    out.append("")
    out.append("`pixels_written_max` is measured (`run --bench`, `WORK_PX_MAX`,")
    out.append("worst frame of the worst game). Everything to its right is")
    out.append("estimated: throughput = px_max * 60; clock = throughput /")
    out.append("pixels-per-clock.\n")
    return out


def memory_table(rows, ks):
    out = []
    out.append("### Framebuffer RAM (estimated)\n")
    out.append("| k | resolution | single buffered | double buffered | "
               "fits 2 Mbit | fits 4 Mbit | fits 8 Mbit |")
    out.append("|---|---|---:|---:|---|---|---|")
    for k in ks:
        row = [r for r in rows if r["k"] == k][0]
        w, h = (int(v) for v in row["resolution"].split("x"))
        bits = w * h * 16                       # RGB565: 16 bits per pixel
        out.append("| %s | %s | %s Mbit | %s Mbit | %s | %s | %s |"
                   % (k, row["resolution"], f_mbit(bits), f_mbit(bits * 2),
                      fits(bits, BRAM_MBITS[0]), fits(bits, BRAM_MBITS[1]),
                      fits(bits, BRAM_MBITS[2])))
    out.append("")
    out.append("framebuffer RAM = W * H * 16 bits (RGB565). Doubled for a")
    out.append("double-buffered design, where the CPU writes one buffer while")
    out.append("the display scans the other out. 1 Mbit = 1e6 bits. 8 Mbit is")
    out.append("the largest block-RAM budget HD_EXPERIMENT.md §6 considers.\n")
    return out


def bandwidth_table(rows, ks):
    out = []
    out.append("### Memory bandwidth (estimated)\n")
    out.append("| k | resolution | scan-out read | write (worst frame) | "
               "write : scan-out |")
    out.append("|---|---|---:|---:|---:|")
    for k in ks:
        px_max = per_k_max(rows, k, "px_max")
        res = [r for r in rows if r["k"] == k][0]
        w, h = (int(v) for v in res["resolution"].split("x"))
        scan = w * h * 2 * 60                  # bytes/s
        write = px_max * 2 * 60                # bytes/s
        out.append("| %s | %s | %s MB/s | %s MB/s | %.2fx |"
                   % (k, res["resolution"], f_mbps(scan), f_mbps(write),
                      write / scan))
    out.append("")
    out.append("scan-out read bandwidth = W * H * 2 * 60 bytes/s (the display")
    out.append("must re-read every pixel every frame). Write bandwidth =")
    out.append("pixels_written_max * 2 * 60 bytes/s. The ratio is the overdraw")
    out.append("the measured games actually have: the write side is dominated")
    out.append("by redrawn pixels, not by the resolution.\n")
    return out


# --- H2 step 3: what a native-HD asset set would need ----------------------
#
# Computed from the pixel formats the machine already has (4-bit paletted,
# the SPRITE4/TILEMAP4 format: 2 pixels per byte). No art is generated here —
# these are byte counts from dimensions, which is what the card asks for.
BPP4 = 4


def asset_table():
    def bytes_for(w, h):
        return (w * h * BPP4) // 8

    hero = [(96, 120), (192, 240)]
    tile = [(64, 64), (128, 128)]
    # A full-width parallax strip: one screen-wide layer, full screen height,
    # at each native density. A shorter band costs proportionally less; this
    # is the pessimistic reading and says so.
    strip = [(480, 270), (960, 540)]

    rom_budget = 256 * 1024
    ram_budget = 64 * 1024

    out = []
    out.append("### Resources a native-HD asset set would need (computed, "
               "not drawn)\n")
    out.append("| asset (4-bit, 2 pixels/byte) | today 480x270 | "
               "2x density 960x540 |")
    out.append("|---|---:|---:|")

    h0, h1 = bytes_for(*hero[0]), bytes_for(*hero[1])
    out.append("| hero frame %dx%d | %s B | — |"
               % (hero[0][0], hero[0][1], f(h0)))
    out.append("| hero frame %dx%d | — | %s B |"
               % (hero[1][0], hero[1][1], f(h1)))
    out.append("| hero, 8-frame cycle | %s B | %s B |" % (f(h0 * 8), f(h1 * 8)))

    t0, t1 = bytes_for(*tile[0]), bytes_for(*tile[1])
    out.append("| one tile %dx%d | %s B | — |" % (tile[0][0], tile[0][1], f(t0)))
    out.append("| one tile %dx%d | — | %s B |" % (tile[1][0], tile[1][1], f(t1)))

    s0, s1 = bytes_for(*strip[0]), bytes_for(*strip[1])
    out.append("| full-width parallax strip %dx%d | %s B | — |"
               % (strip[0][0], strip[0][1], f(s0)))
    out.append("| full-width parallax strip %dx%d | — | %s B |"
               % (strip[1][0], strip[1][1], f(s1)))

    out.append("")
    out.append("bytes = W * H * 4 / 8. Against today's budgets:\n")
    out.append("| | bytes | share of 256 KB ROM | share of 64 KB RAM |")
    out.append("|---|---:|---:|---:|")
    for label, b in (("today's hero cycle (8 frames)", h0 * 8),
                     ("HD hero cycle (8 frames)", h1 * 8),
                     ("today's full-width strip", s0),
                     ("HD full-width strip", s1),
                     ("HD hero frame", h1),
                     ("today's tile", t0),
                     ("HD tile", t1)):
        out.append("| %s | %s B | %.1f%% | %.1f%% |"
                   % (label, f(b), 100.0 * b / rom_budget,
                      100.0 * b / ram_budget))
    out.append("")
    out.append("Budgets are the machine's today: 256 KB ROM (%s B), 64 KB RAM "
               "(%s B)." % (f(rom_budget), f(ram_budget)))
    out.append("")
    out.append("Where each byte is charged (`programs/link.ld`): a")
    out.append("`static const` asset table lands in `.rodata` and so costs "
               "**ROM**")
    out.append("only — the video device draws it straight from that address,")
    out.append("which is what the golden games do today. An initialised table")
    out.append("in `.data` is charged **twice**: its stored image sits in ROM,")
    out.append("and crt0 copies it into RAM, where it lives from then on. So")
    out.append("the ROM column is the primary denominator for asset bytes; the")
    out.append("RAM column is what the same bytes cost *if* they also have to")
    out.append("be resident, out of a 64 KB that also carries `.bss` and the")
    out.append("stack. RAM is a ceiling, not a free budget.")
    out.append("")
    out.append("The framebuffer is in neither: SPEC.md section 5 has the device")
    out.append("drawing into its own RGB565 buffer, priced in the *Framebuffer")
    out.append("RAM* table above and absent from `arcade1 info`'s guest memory")
    out.append("map (rom 262144, ram 65536, mmio 4096).")
    return out


def f(n):
    """Thousands-separated integer."""
    return "{:,}".format(int(round(n)))


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    csv_path = argv[1] if len(argv) > 1 else os.path.join(
        root, "docs", "experiments", "hd-measurements.csv")
    out_path = argv[2] if len(argv) > 2 else os.path.join(
        root, "docs", "experiments", "hd-fpga-estimates.md")

    rows, ks = load(csv_path)
    games = sorted({r["game"] for r in rows})
    frame_counts = sorted({int(r["frames"]) for r in rows})
    cpus = sorted({r["bench_cpu"] for r in rows})

    doc = []
    doc.append("# ARCADE-1 — HD experiment: the FPGA-side bill (estimated)")
    doc.append("")
    doc.append("Generated by `experiments/hd/fpga_estimates.py` from")
    doc.append("`docs/experiments/hd-measurements.csv`.")
    doc.append("")
    doc.append("**Every number in this file below the inputs is ESTIMATED.**")
    doc.append("No FPGA was built and no device was timed. The only measured")
    doc.append("quantities are the resolutions (fixed by the experiment) and")
    doc.append("`pixels_written_max` (measured on the host, by")
    doc.append("`arcade1 run --bench`); both are labelled where they enter.")
    doc.append("")
    doc.append("Measured inputs (this file's only measured numbers):")
    doc.append("")
    doc.append("- host CPU: `%s`" % ", ".join(cpus))
    doc.append("- guests: %s" % ", ".join(games))
    doc.append("- %s frames per run, at k = %s"
               % (" and ".join(str(c) for c in frame_counts),
                  ", ".join(ks)))
    doc.append("- `W * H` per k, fixed by `-DARCADE1_HD_SCALE=k`")
    doc.append("- `pixels_written_max` per k: `run --bench`, `WORK_PX_MAX`,")
    doc.append("  the worst frame of the worst guest")
    doc.append("- `rom_bytes`: `arcade1 inspect`")
    doc.append("")
    doc.append("## Formulas")
    doc.append("")
    doc.append("```")
    doc.append("pixel throughput   = pixels_written_max * 60          px/s")
    doc.append("clock @ P px/clk   = (pixels_written_max * 60) / P    Hz")
    doc.append("framebuffer RAM    = W * H * 16                      bits")
    doc.append("  double buffered  = W * H * 16 * 2                  bits")
    doc.append("scan-out read      = W * H * 2 * 60                  B/s")
    doc.append("write bandwidth    = pixels_written_max * 2 * 60      B/s")
    doc.append("1 Mbit             = 1e6 bits")
    doc.append("```")
    doc.append("")
    doc.extend(throughput_table(rows, ks))
    doc.extend(memory_table(rows, ks))
    doc.extend(bandwidth_table(rows, ks))
    doc.extend(asset_table())

    text = "\n".join(doc) + "\n"
    with open(out_path, "w") as f_out:
        f_out.write(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
