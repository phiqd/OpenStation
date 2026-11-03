#!/usr/bin/env bash
# Build without PlatformIO's scons, using the PlatformIO-installed WCH toolchain.
# Usage: tools/build.sh   -> build/openstation.{elf,bin,hex}
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIO="${PLATFORMIO_PACKAGES:-$HOME/.platformio/packages}"
F="$PIO/framework-wch-noneos-sdk"; T="$PIO/toolchain-riscv/bin"
CC="$T/riscv-wch-elf-gcc"; OUT="$ROOT/build"; OBJ="$OUT/obj"
rm -rf "$OBJ"; mkdir -p "$OBJ"
ARCH="-march=rv32imac_zicsr -mabi=ilp32 -msave-restore -msmall-data-limit=8"
CFLAGS="-std=gnu99 -Os -Wall -Wextra -Wno-unused-parameter -fsigned-char -ffunction-sections \
 -fdata-sections -fno-common $ARCH -DCH32X033F8 -DCH32X03X -DCH32X03x -DCH32X033 \
 -DSYSCLK_FREQ_48MHz_HSI=48000000 -DSYSCLK_FREQ_48MHZ_HSI=48000000 -I$ROOT/include \
 -I$F/Core/ch32x035 -I$F/Debug/ch32x035 -I$F/Peripheral/ch32x035/inc -I$F/System/ch32x035"
SRCS="$F/Core/ch32x035/core_riscv.c $F/System/ch32x035/system_ch32x035.c $F/Debug/ch32x035/debug.c \
 $F/Peripheral/ch32x035/src/*.c"
for s in $SRCS; do "$CC" $CFLAGS -w -c "$s" -o "$OBJ/sdk_$(basename "$s" .c).o"; done
for s in "$ROOT"/src/*.c; do "$CC" $CFLAGS -c "$s" -o "$OBJ/$(basename "$s" .c).o"; done
"$CC" $ARCH -c "$F/Startup/startup_ch32x035.S" -o "$OBJ/startup.o"
"$CC" $ARCH -nostartfiles -Wl,--gc-sections -Wl,-Map="$OUT/openstation.map" -T "$ROOT/ld/linker.ld" \
  "$OBJ"/*.o -o "$OUT/openstation.elf" --specs=nano.specs --specs=nosys.specs
"$T/riscv-wch-elf-objcopy" -O binary "$OUT/openstation.elf" "$OUT/openstation.bin"
"$T/riscv-wch-elf-objcopy" -O ihex "$OUT/openstation.elf" "$OUT/openstation.hex"
"$T/riscv-wch-elf-size" "$OUT/openstation.elf"
