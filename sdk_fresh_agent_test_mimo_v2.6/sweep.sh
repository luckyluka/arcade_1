#!/bin/sh
# sweep.sh "COLS ROWS TW TH [KEY=VAL ...]" ... — one probe9 build per geometry
cd /home/luka/m29
for g in "$@"; do
  set -- $g
  C=$1; R=$2; W=$3; H=$4
  shift 4
  EX=""
  for kv in "$@"; do EX="$EX
#define $(echo $kv | tr '=' ' ')"; done
  cat > probe9_cfg.c <<EOF
#define COLS $C
#define ROWS $R
#define TW $W
#define TH $H
$EX
#include "probe9.c"
EOF
  if ! ./arcade1 build probe9_cfg.c probe9_cfg.arc1 >/dev/null 2>build.log; then
    echo "cols=$C rows=$R tile=${W}x${4} -> BUILD FAIL"; grep -m2 error build.log; continue
  fi
  ./arcade1 run probe9_cfg.arc1 --frames 12 --capture p.ppm --quiet >/dev/null
  echo -n "cols=$C rows=$R tile=${W}x${H} $(echo $EX | tr -d '\n')  ->  "
  python3 sum.py p.ppm | sed 's/  | .*//'
  echo -n "             cols@y=$((20 + H / 2)): "
  python3 col.py p.ppm $((20 + H / 2)) $C $W
done
