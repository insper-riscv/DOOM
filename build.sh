#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Compila a doomgeneric para RV32IM (ilp32) e gera $OUT/doom.elf (e, na placa, $OUT/doom.bin).
# Reexecuta no container dev_tools, a menos que ja rode nele.
#
# TARGET escolhe o build:
#   spike  (padrao) WAD embutida no ELF, console HTIF, relogio virtual: o D0.
#   layout          mesmo mapa de memoria da placa (WAD em WAD_ADDR, framebuffer em
#                   FB_ADDR, bloco de resultado em RESULT_ADDR), mas console HTIF e
#                   relogio virtual: valida no Spike o codigo e o mapa da placa.
#   board           o binario da placa: console pela JTAG UART, DG_GetTicksMs do
#                   doom_clock.c da plataforma, sem a WAD (ela vai por `load-ram --wad`).
#
# Variaveis: TARGET, OPT (padrao -O2), OUT (padrao build, ou build-$TARGET),
# RAM_ORIGIN e RAM_LENGTH (ver port/link.ld), PLATFORM_DIR (pasta da plataforma doom, padrao
# Insper-RISC-V/src/cyclone-v/doom, baixada pelo fetch.sh; tem de ficar dentro desta pasta,
# que o container monta). O mapa da SDRAM esta em scripts/layout.sh.
set -eu
cd "$(dirname "$0")"

. scripts/container.sh
run_in_container build.sh "$@"
. scripts/layout.sh

TARGET=${TARGET:-spike}
OPT=${OPT:--O2}
CC=riscv32-unknown-elf-gcc
DG=doomgeneric/doomgeneric
PLATFORM_DIR=${PLATFORM_DIR:-Insper-RISC-V/src/cyclone-v/doom}
RT="$PLATFORM_DIR/runtime"

case "$TARGET" in
  spike)
    OUT=${OUT:-build}
    RAM_LENGTH=${RAM_LENGTH:-0x04000000}
    ;;
  layout | board)
    OUT=${OUT:-build-$TARGET}
    RAM_LENGTH=${RAM_LENGTH:-$BOARD_RAM_LENGTH}
    ;;
  *)
    echo "TARGET invalido: $TARGET (spike, layout ou board)" >&2
    exit 1
    ;;
esac

if [ ! -f "$DG/Makefile" ] || [ ! -f doom1.wad ]; then
  echo "doomgeneric/ ou doom1.wad ausente: rode fetch.sh" >&2
  exit 1
fi
if [ "$TARGET" = board ] && [ ! -f "$RT/doom_clock.c" ]; then
  echo "$RT/doom_clock.c ausente: rode fetch.sh (ou defina PLATFORM_DIR)" >&2
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
# Definicoes do codigo da porta que dependem do alvo
PORT_DEFS="-DDOOM_RESULT_ADDR=$RESULT_ADDR"
LINK_DEFS=""
case "$TARGET" in
  layout | board)
    PORT_DEFS="$PORT_DEFS -DDOOM_FB_ADDR=$FB_ADDR -DDOOM_WAD_ADDR=$WAD_ADDR -DDOOM_WAD_SIZE=$WAD_SIZE"
    LINK_DEFS="-Wl,--defsym=WAD_ADDR=$WAD_ADDR -Wl,--defsym=HEAP_START=$HEAP_START"
    ;;
esac
if [ "$TARGET" = board ]; then
  PORT_DEFS="$PORT_DEFS -DDOOM_CONSOLE_UART -DDOOM_PLATFORM_CLOCK -I$RT"
fi
# Medicao de CPI (MEASURE=1, so nos alvos layout e board): instantaneos de cycle e instret a cada
# 100 quadros no bloco de scripts/layout.sh; na placa, tambem os contadores de evento do Zihpm.
# MEASURE_STOP=N encerra o jogo ao entregar o quadro N (o _exit grava o resumo)
if [ "${MEASURE:-0}" = 1 ]; then
  if [ "$TARGET" = spike ]; then
    echo "MEASURE=1 pede TARGET=layout ou board (o mapa da SDRAM)" >&2
    exit 1
  fi
  PORT_DEFS="$PORT_DEFS -DDOOM_MEASURE -DDOOM_MEASURE_ADDR=$MEASURE_ADDR"
  if [ "$TARGET" = board ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_MEASURE_HPM"
  fi
  if [ -n "${MEASURE_STOP:-}" ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_MEASURE_STOP=$MEASURE_STOP"
  fi
fi
# Video indexado (VIDEO=1, so nos alvos layout e board): a doomgeneric entrega 320x200 bytes de indice e a
# paleta (CMAP256), sem a conversao para XRGB de 32 bits. No board, o quadro vai para a janela do VIDEO e o
# teclado PS/2 e lido do KBD (DOOM_VIDEO); no layout (Spike) so ficam o CRC dos indices e as estatisticas.
# VSYNC=0 tira a espera pelo apagamento vertical, VCRC_FRAMES=N muda quantos quadros guardam o CRC (padrao 32);
# com MEASURE=1 os dois valem 0 (a medicao de CPI nao pode incluir espera nem CRC), salvo se definidos.
# INTERACTIVE=1: titulo, menu e jogo (sem -timedemo), com o demo de atracao do DOOM
DOOM_SRC_PORT="doom_platform doom_runtime doom_wad_mem"
if [ "${VIDEO:-0}" = 1 ]; then
  if [ "$TARGET" = spike ]; then
    echo "VIDEO=1 pede TARGET=layout ou board (o mapa da SDRAM)" >&2
    exit 1
  fi
  DEFS="$DEFS -DCMAP256"
  PORT_DEFS="$PORT_DEFS -DDOOM_VSTAT_ADDR=$VSTAT_ADDR"
  DOOM_SRC_PORT="$DOOM_SRC_PORT doom_kbd doom_video"
  # W A S D movem o jogador (port/doom_kbd.c, __wrap_I_InitInput)
  LINK_DEFS="$LINK_DEFS -Wl,--wrap=I_InitInput"
  if [ "$TARGET" = board ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_VIDEO"
  fi
  if [ "${MEASURE:-0}" = 1 ]; then
    VSYNC=${VSYNC:-0}
    VCRC_FRAMES=${VCRC_FRAMES:-0}
  fi
  if [ "${VSYNC:-1}" = 0 ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_VIDEO_NOVSYNC"
  fi
  if [ -n "${VCRC_FRAMES:-}" ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_VCRC_FRAMES=${VCRC_FRAMES}u"
  fi
fi
if [ "${INTERACTIVE:-0}" = 1 ]; then
  # No modo interativo o Quit Game e o fim por erro reiniciam o programa (port/doom_runtime.c), com o .data
  # guardado em DOOM_DATA_BACKUP_ADDR
  PORT_DEFS="$PORT_DEFS -DDOOM_INTERACTIVE -DDOOM_DATA_BACKUP_ADDR=$DATA_BACKUP_ADDR"
fi
# TESTKEYS=1 (so no alvo layout, no Spike): injeta F10 e Y (Quit Game) para testar o reinicio sem teclado
if [ "${TESTKEYS:-0}" != 0 ]; then
  if [ "$TARGET" != layout ] || [ "${VIDEO:-0}" != 1 ]; then
    echo "TESTKEYS pede TARGET=layout e VIDEO=1" >&2
    exit 1
  fi
  PORT_DEFS="$PORT_DEFS -DDOOM_TEST_KEYS=$TESTKEYS"
  LINK_DEFS="$LINK_DEFS -Wl,--wrap=G_BuildTiccmd"
fi
# Codigo da doomgeneric e C89/C99 de terceiros: sem os avisos do projeto
GAME_FLAGS="-std=gnu99 -w -fcommon -ffunction-sections"

for f in $SRC; do
  $CC --specs=picolibc.specs $ARCH $OPT $GAME_FLAGS $DEFS -I"$DG" \
    -c "$DG/$f" -o "$OUT/obj/$(basename "${f%.c}").o"
done

# Codigo da porta: com os avisos estritos do projeto
for f in $DOOM_SRC_PORT; do
  $CC --specs=picolibc.specs $ARCH $OPT -std=gnu23 -Wall -Wextra -Werror $DEFS $PORT_DEFS \
    -I"$DG" -Iport -c "port/$f.c" -o "$OUT/obj/$f.o"
done

# Relogio da plataforma: a fonte e a do repositorio Insper-RISC-V, copiada pelo build
if [ "$TARGET" = board ]; then
  $CC --specs=picolibc.specs $ARCH $OPT -std=gnu23 -Wall -Wextra -Werror -I"$RT" \
    -c "$RT/doom_clock.c" -o "$OUT/obj/doom_clock.o"
fi

# WAD no ELF, so nos alvos do Spike. No spike, na secao .wad logo depois do codigo; os nomes
# do arquivo geram os simbolos _binary_doom1_wad_start e _binary_doom1_wad_end. No layout, na
# secao .wadfix, que o linker poe em WAD_ADDR (a mesma posicao em que a placa a recebe).
case "$TARGET" in
  spike) WAD_SECTION=.wad ;;
  layout) WAD_SECTION=.wadfix ;;
esac
if [ "$TARGET" != board ]; then
  riscv32-unknown-elf-objcopy -I binary -O elf32-littleriscv -B riscv:rv32 \
    --rename-section ".data=$WAD_SECTION,alloc,load,readonly,data,contents" \
    doom1.wad "$OUT/obj/wad.o"
fi

# shellcheck disable=SC2086
$CC --specs=picolibc.specs $ARCH $OPT -T port/link.ld \
  -Wl,--defsym=RAM_ORIGIN="$RAM_ORIGIN" -Wl,--defsym=RAM_LENGTH="$RAM_LENGTH" $LINK_DEFS \
  -Wl,--wrap=M_FileExists -Wl,--wrap=I_Quit -Wl,--gc-sections -Wl,-Map="$OUT/doom.map" \
  "$OUT"/obj/*.o -lm -o "$OUT/doom.elf"

riscv32-unknown-elf-size -A "$OUT/doom.elf"

# Binario da placa: so o codigo, as constantes e os dados iniciais, a partir de RAM_ORIGIN
if [ "$TARGET" = board ]; then
  riscv32-unknown-elf-objcopy -O binary "$OUT/doom.elf" "$OUT/doom.bin"
  ls -l "$OUT/doom.bin"
fi
