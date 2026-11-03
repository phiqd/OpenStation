#!/usr/bin/env bash
# Compare the original and the new temperature controller on the tip model.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p sim/out
SRC="sim/pid_bench.c sim/mock.c src/control.c src/safety.c src/profiles.c"
FLAGS="-std=gnu99 -O1 -Wall -Wextra -Wno-unused-parameter -Isim/include -Iinclude"
gcc $FLAGS -DPID_LEGACY $SRC -lm -o sim/out/pid_old
gcc $FLAGS $SRC -lm -o sim/out/pid_new
{ sim/out/pid_old; sim/out/pid_new | tail -n +2; } > sim/out/pid_bench.csv
python3 -c "import csv,sys;r=list(csv.reader(open('sim/out/pid_bench.csv')));w=[max(len(x[i]) for x in r) for i in range(len(r[0]))];[print('  '.join(c.rjust(w[i]) for i,c in enumerate(x))) for x in r]"
