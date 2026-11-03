#!/usr/bin/env bash
# Build and run the host simulator; screenshots land in sim/out/*.png
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p sim/out
gcc -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isim/include -Iinclude \
  sim/sim.c sim/mock.c src/ui.c src/control.c src/safety.c src/profiles.c src/fonts_aa.c src/logo.c -lm -o sim/out/sim
sim/out/sim
python3 - <<'P'
import glob
from PIL import Image
for f in sorted(glob.glob("sim/out/*.ppm")):
    Image.open(f).resize((480, 384), Image.NEAREST).save(f[:-4] + ".png")
P
