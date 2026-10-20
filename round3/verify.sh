#!/bin/sh
# Build + verify flappy.arc1 from a clean shell.
#
#   ./verify.sh                    (from round3/)
#
# Needs: the arcade1 CLI (override with ARC=/path/to/arcade1) and
# riscv64-unknown-elf-gcc / -objcopy / -nm on PATH.
set -e
cd "$(dirname "$0")"
ARC="${ARC:-/home/luka/Documents/arcade1/build/arcade1}"
FRAMES=520

# 1. build (reproducible: same source -> byte-identical .arc1 + .syms)
"$ARC" build flappy.c flappy.arc1

# 2. determinism: same image + same input script -> identical per-frame hashes
A=$(mktemp) B=$(mktemp)
"$ARC" run flappy.arc1 --frames $FRAMES --input input.txt --strict > "$A"
"$ARC" run flappy.arc1 --frames $FRAMES --input input.txt --strict > "$B"
diff "$A" "$B" && echo "determinism: two runs byte-identical"
rm -f "$A" "$B"

# 3. golden regression: every frame hash must match flappy.golden.txt
"$ARC" test flappy.arc1 --golden flappy.golden.txt --input input.txt --frames $FRAMES
echo "golden: OK"
