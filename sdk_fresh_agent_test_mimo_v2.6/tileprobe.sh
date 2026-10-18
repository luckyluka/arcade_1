#!/bin/sh
# tileprobe.sh COLS ROWS TW TH — build probe9 with one geometry, capture, scan.
set -e
cd /home/luka/m29
C=$1; R=$2; W=$3; H=$4
cat > probe9_cfg.c <<EOF
#define COLS $C
#define ROWS $R
#define TW $W
#define TH $H
#include "probe9.c"
EOF
./arcade1 build probe9_cfg.c probe9_cfg.arc1 >/dev/null
./arcade1 run probe9_cfg.arc1 --frames 12 --capture p.ppm --quiet
echo "cols=$C rows=$R tile=${W}x${H}"
python3 scan.py p.ppm
