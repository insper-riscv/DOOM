#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Compila a doomgeneric para RV32IM (ilp32) com a WAD embutida (build do Spike)
# e gera $OUT/doom.elf. Reexecuta no container dev_tools, a menos que ja rode nele.
# Variaveis: OPT (padrao -O2), OUT (padrao build), RAM_ORIGIN e RAM_LENGTH (ver port/link.ld).
set -eu
cd "$(dirname "$0")"

. scripts/container.sh
run_in_container build.sh "$@"

OPT=${OPT:--O2}
RAM_ORIGIN=${RAM_ORIGIN:-0x40000000}
RAM_LENGTH=${RAM_LENGTH:-0x04000000}
CC=riscv32-unknown-elf-gcc
OUT=${OUT:-build}
DG=doomgeneric/doomgeneric

if [ ! -f "$DG/Makefile" ] || [ ! -f doom1.wad ]; then
  echo "doomgeneric/ ou doom1.wad ausente: rode fetch.sh" >&2
  exit 1
fi

mkdir -p "$OUT/obj"
rm -f "$OUT"/obj/*.o

# Fontes do jogo: a lista do Makefile da doomgeneric, sem o backend xlib e sem
# o w_file_stdc.c (substituido por port/doom_wad_mem.c)
SRC=$(sed -n 's/^SRC_DOOM = //p' "$DG/Makefile" | tr ' ' '\n' \
  | grep -v -e doomgeneric_xlib -e w_file_stdc -e '^$' | sed 's/\.o$/.c/')

ARCH="-march=rv32im_zicsr_zicntr -mabi=ilp32"
DEFS="-DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200"
# Codigo da doomgeneric e C89/C99 de terceiros: sem os avisos do projeto
GAME_FLAGS="-std=gnu99 -w -fcommon -ffunction-sections"

for f in $SRC; do
  $CC --specs=picolibc.specs $ARCH $OPT $GAME_FLAGS $DEFS -I"$DG" \
    -c "$DG/$f" -o "$OUT/obj/$(basename "${f%.c}").o"
done

# Codigo da porta: com os avisos estritos do projeto
for f in doom_platform doom_runtime doom_wad_mem; do
  $CC --specs=picolibc.specs $ARCH $OPT -std=gnu23 -Wall -Wextra -Werror $DEFS -I"$DG" -Iport \
    -c "port/$f.c" -o "$OUT/obj/$f.o"
done

# WAD embutida na secao .wad (so no Spike); o nome do arquivo gera os simbolos
# _binary_doom1_wad_start e _binary_doom1_wad_end
riscv32-unknown-elf-objcopy -I binary -O elf32-littleriscv -B riscv:rv32 \
  --rename-section .data=.wad,alloc,load,readonly,data,contents \
  doom1.wad "$OUT/obj/wad.o"

$CC --specs=picolibc.specs $ARCH $OPT -T port/link.ld \
  -Wl,--defsym=RAM_ORIGIN="$RAM_ORIGIN" -Wl,--defsym=RAM_LENGTH="$RAM_LENGTH" \
  -Wl,--wrap=M_FileExists -Wl,--gc-sections -Wl,-Map="$OUT/doom.map" \
  "$OUT"/obj/*.o -lm -o "$OUT/doom.elf"

riscv32-unknown-elf-size -A "$OUT/doom.elf"
