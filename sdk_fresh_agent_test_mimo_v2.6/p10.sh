#!/bin/sh
# p10.sh "B0 B1 B2 B3 B4 B5" ... — probe10 per bank pattern
cd /home/luka/m29
for g in "$@"; do
  set -- $g
  cat > probe10_cfg.c <<EOF
#define B0 $1
#define B1 $2
#define B2 $3
#define B3 $4
#define B4 $5
#define B5 $6
#include "probe10.c"
EOF
  if ! ./arcade1 build probe10_cfg.c probe10_cfg.arc1 >/dev/null 2>build.log; then
    echo "banks $g -> BUILD FAIL"; continue
  fi
  ./arcade1 run probe10_cfg.arc1 --frames 12 --capture p.ppm --quiet >/dev/null
  echo -n "banks $g  ->  "
  python3 col.py p.ppm 26 6
done
