#!/bin/sh
# ARCADE-1 — CHEATSHEET.md snippet test.
#
# Extracts every ```c fenced block from CHEATSHEET.md and compiles each one
# as a guest translation unit with the real guest toolchain (-c only: the
# snippets are API usage fragments, not full programs). Catches drift
# between the documented API and the real headers — a snippet that no
# longer compiles fails make check.
#
# usage: test_cheatsheet.sh <arcade1-cli> <rom-dir> <golden-dir>

set -eu

CLI=${1:?usage: test_cheatsheet.sh <arcade1-cli> <rom-dir> <golden-dir>}
ROM=${2:?}
GOLD=${3:?}

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

fail() { echo "test-cheatsheet: FAILED — $1" >&2; exit 1; }
note() { echo "  $1"; }

CHEAT=$(dirname "$0")/../CHEATSHEET.md
[ -f "$CHEAT" ] || fail "missing $CHEAT"

# --- extract the C snippets -------------------------------------------------
awk '/^```c$/{n++; f=1; next} /^```$/{f=0} f{print > ("'"$OUT"'/snippet_" n ".c")}' "$CHEAT"

NSNIP=$(ls "$OUT"/snippet_*.c 2>/dev/null | wc -l | tr -d ' ')
[ "$NSNIP" -ge 1 ] || fail "no C snippets found in CHEATSHEET.md"
note "extracted $NSNIP snippets from CHEATSHEET.md"

# --- compile each ------------------------------------------------------------
CC=${RISCV_CC:-riscv64-unknown-elf-gcc}
command -v "$CC" >/dev/null 2>&1 ||
    fail "guest toolchain $CC not on PATH (cannot compile snippets)"
i=0
for snip in "$OUT"/snippet_*.c; do
    i=$((i + 1))
    if ! "$CC" -march=rv32im -mabi=ilp32 -Os -ffreestanding -nostdlib \
            -nostartfiles -msmall-data-limit=0 -Wall -Wextra \
            -I"$(dirname "$0")/../programs" \
            -c "$snip" -o "$OUT/snippet.o" 2> "$OUT/err.txt"; then
        echo "  snippet $i does not compile:" >&2
        cat "$OUT/err.txt" >&2
        fail "CHEATSHEET.md snippet $i failed to compile"
    fi
done
note "all $NSNIP snippets compile clean with the guest flags (-Wall -Wextra)"

echo "test-cheatsheet: CHEATSHEET.md ok ($NSNIP snippets compile)"
