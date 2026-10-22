#!/usr/bin/env python3
"""HD_EXPERIMENT.md card H1 — the pixels_written k^2 acceptance.

usage: px_check.py BASE_INSPECT SCALED_INSPECT K

BASE_INSPECT is `arcade1 inspect --commands` output from the k = 1 machine,
SCALED_INSPECT the same frame from the k = K machine. Verifies, exactly:

  * VIDEO_PIXELS_WRITTEN (the frame total) at K equals K^2 times the k = 1
    total;
  * every recorded command occupies the same position, has the same type,
    and its own px count is exactly K^2 times its k = 1 value — so the rule
    holds for EVERY command type seen, not just for the total;
  * the command stream itself (count and types) is identical, because the
    scale must not change what the guest does.

Exit 0 when all hold, 1 with the first violation otherwise.
Deterministic: no clock, no randomness, stdlib only.
"""

import re
import sys

TOTAL_RE = re.compile(r"^VIDEO_PIXELS_WRITTEN\s+(\d+)\s*$")
CMD_RE = re.compile(r"^CMD\s+(\d+)\s+(\S+).*?\bpx=(\d+)\b")


def fail(msg):
    sys.stderr.write("px_check: %s\n" % msg)
    return 1


def parse(path):
    total = None
    cmds = []
    with open(path, "r") as f:
        for line in f:
            line = line.rstrip("\n")
            m = TOTAL_RE.match(line)
            if m:
                total = int(m.group(1))
                continue
            m = CMD_RE.match(line)
            if m:
                cmds.append((int(m.group(1)), m.group(2), int(m.group(3))))
    if total is None:
        raise ValueError("%s: no VIDEO_PIXELS_WRITTEN line" % path)
    return total, cmds


def main(argv):
    if len(argv) != 4:
        return fail("usage: px_check.py BASE_INSPECT SCALED_INSPECT K")
    base_path, scaled_path = argv[1], argv[2]
    k = int(argv[3])
    if k < 1:
        return fail("K must be >= 1")
    k2 = k * k

    try:
        base_total, base_cmds = parse(base_path)
        scaled_total, scaled_cmds = parse(scaled_path)
    except (OSError, ValueError) as e:
        return fail(str(e))

    if scaled_total != k2 * base_total:
        return fail("%s: VIDEO_PIXELS_WRITTEN %d, expected %d x %d = %d"
                    % (scaled_path, scaled_total, k2, base_total,
                       k2 * base_total))

    if len(scaled_cmds) != len(base_cmds):
        return fail("%s: %d commands, but the k=1 frame had %d"
                    % (scaled_path, len(scaled_cmds), len(base_cmds)))

    types = set()
    for (bidx, btype, bpx), (sidx, stype, spx) in zip(base_cmds, scaled_cmds):
        if (bidx, btype) != (sidx, stype):
            return fail("command %d: k=1 says %s, k=%d says %s"
                        % (bidx, btype, k, stype))
        if spx != k2 * bpx:
            return fail("command %d (%s): px %d at k=%d, expected %d x %d = %d"
                        % (bidx, btype, spx, k, k2, bpx, k2 * bpx))
        types.add(btype)

    print("  ok: %d commands, %d types (%s), VIDEO_PIXELS_WRITTEN %d -> %d"
          " (= %d^2 x)"
          % (len(base_cmds), len(types), ",".join(sorted(types)),
             base_total, scaled_total, k))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
