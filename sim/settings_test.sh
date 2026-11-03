#!/usr/bin/env bash
# Host test of the wear-levelled settings ring (simulated flash).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p sim/out
gcc -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter -Isim -Isim/include -Iinclude \
  sim/settings_test.c sim/mock.c src/control.c src/safety.c src/profiles.c -lm -o sim/out/settings_test
sim/out/settings_test
