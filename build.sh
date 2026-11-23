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

# MENU=1: menu de escolha do jogo antes do DOOM (port/doom_menu.c) com varias WADs na SDRAM, as suas, de uma pasta
# (WAD_DIR, ou a que o `fetch.sh --wad-dir` guardou), no lugar da doom1.wad shareware. scripts/wads.sh junta as WADs em
# $OUT/wads.bin, escreve a tabela $OUT/wad_table.h e o mapa da SDRAM $OUT/wads.env, que passa a valer no lugar do
# padrao do scripts/layout.sh. WAD_SET escolhe os jogos (padrao "doom doom2 plutonia master"). Pede VIDEO=1 e
# INTERACTIVE=1, porque o menu usa o video e o teclado e o jogo volta a ele ao reiniciar.
WAD_FILE=doom1.wad
# SD=load (copia o payload do cartao microSD para a SDRAM no inicio) ou SD=demand (le os blocos do cartao a cada
# W_ReadLump, pede MENU=1): so no alvo board; sem cartao o jogo cai no caminho da JTAG (SD=load) ou avisa (SD=demand)
case "${SD:-}" in
  "" | load) ;;
  demand)
    if [ "${MENU:-0}" != 1 ]; then
      echo "SD=demand pede MENU=1 (o indice do cartao alimenta o menu)" >&2
      exit 1
    fi
    ;;
  *)
    echo "SD invalido: ${SD} (load ou demand)" >&2
    exit 1
    ;;
esac
# SDFS=raw (padrao, sdcard_direct_mapped): o cartao guarda o wads.bin cru, gravado pelo scripts/sdwrite.py, e o jogo o le
# por setores. SDFS=fatfs (sdcard_FatFs): o cartao e um FAT32 comum com os arquivos das WADs (DOOM.WAD, DOOM2.WAD,
# PLUTONIA.WAD, TNT.WAD e a pasta MASTER), lido pelo FatFs (fatfs/, baixado pelo fetch.sh): sem wads.sh nem sdwrite.py.
# Pede SD=demand e MENU=1; as WADs ficam so no cartao (como ONDEMAND=1).
SDFS=${SDFS:-raw}
case "$SDFS" in
  raw) ;;
  fatfs)
    if [ "${SD:-}" != demand ] || [ "${MENU:-0}" != 1 ]; then
      echo "SDFS=fatfs pede SD=demand e MENU=1" >&2
      exit 1
    fi
    ONDEMAND=1
    export ONDEMAND
    ;;
  *)
    echo "SDFS invalido: $SDFS (raw ou fatfs)" >&2
    exit 1
    ;;
esac
if [ -n "${SD:-}" ] && [ "$TARGET" != board ]; then
  echo "SD=${SD} pede TARGET=board (o periferico SD so existe na placa)" >&2
  exit 1
fi
if [ "${MENU:-0}" = 1 ]; then
  if [ "$TARGET" = spike ]; then
    echo "MENU=1 pede TARGET=layout ou board (o mapa da SDRAM)" >&2
    exit 1
  fi
  if [ "${VIDEO:-0}" != 1 ] || [ "${INTERACTIVE:-0}" != 1 ]; then
    echo "MENU=1 pede VIDEO=1 e INTERACTIVE=1" >&2
    exit 1
  fi
  mkdir -p "$OUT"
  # ONDEMAND=1 (com SD=demand): as WADs ficam so no cartao, todas, sem o limite da SDRAM; wads.sh empacota com os
  # deslocamentos e nao ha como cair na JTAG. Sem ONDEMAND, o conjunto tem de caber na SDRAM e o fallback vale.
  if [ "${ONDEMAND:-0}" = 1 ] && [ "${SD:-}" != demand ]; then
    echo "ONDEMAND=1 pede SD=demand" >&2
    exit 1
  fi
  export ONDEMAND
  if [ "$SDFS" = fatfs ]; then
    # Sem WADs no build: o mapa da SDRAM e a tabela vazia vem do scripts/fatfs_layout.sh, a lista de jogos so do cartao
    sh scripts/fatfs_layout.sh "$OUT"
    . "$OUT/wads.env"
    WAD_FILE=/dev/null
  else
    sh scripts/wads.sh "$OUT"
    . "$OUT/wads.env"
    WAD_FILE="$OUT/wads.bin"
  fi
fi

if [ ! -f "$DG/Makefile" ] || [ ! -e "$WAD_FILE" ]; then
  echo "doomgeneric/ ou $WAD_FILE ausente: rode fetch.sh" >&2
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
DOOM_SRC_PORT="doom_platform doom_runtime doom_wad_mem doom_fastmem doom_logo"
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
  # PROFILE=1: kciclos e kinstrucoes por quadro por regiao na linha [PERFIL] de 5 s (port/doom_video.c)
  if [ "${PROFILE:-0}" = 1 ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_PROFILE"
    LINK_DEFS="$LINK_DEFS -Wl,--wrap=I_FinishUpdate"
  fi
  if [ "${VSYNC:-1}" = 0 ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_VIDEO_NOVSYNC"
  fi
  if [ -n "${VCRC_FRAMES:-}" ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_VCRC_FRAMES=${VCRC_FRAMES}u"
  fi
fi
case "${SD:-}" in
  load)
    PORT_DEFS="$PORT_DEFS -DDOOM_SD_LOAD"
    DOOM_SRC_PORT="$DOOM_SRC_PORT doom_sd"
    ;;
  demand)
    PORT_DEFS="$PORT_DEFS -DDOOM_SD_DEMAND"
    if [ "${ONDEMAND:-0}" = 1 ]; then
      PORT_DEFS="$PORT_DEFS -DDOOM_SD_ONLY"
    fi
    DOOM_SRC_PORT="$DOOM_SRC_PORT doom_sd"
    if [ "$SDFS" = fatfs ]; then
      PORT_DEFS="$PORT_DEFS -DDOOM_SD_FATFS -Ifatfs/source"
      DOOM_SRC_PORT="$DOOM_SRC_PORT doom_fatfs doom_diskio"
    fi
    ;;
esac
if [ "${MENU:-0}" = 1 ]; then
  PORT_DEFS="$PORT_DEFS -DDOOM_MENU -I$OUT"
  # MENU_AUTO=1: sem escolha no menu, abre o primeiro jogo e roda o -timedemo demo1 (medicao do fps pelo cartao)
  if [ "${MENU_AUTO:-0}" = 1 ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_MENU_AUTO -DDOOM_MENU_AUTO_GAME=${MENU_AUTO_GAME:-0} -DDOOM_MENU_AUTO_DEMO=\"${MENU_AUTO_DEMO:-demo1}\""
  fi
  DOOM_SRC_PORT="$DOOM_SRC_PORT doom_menu"
  # MENU_SCRIPT roda um roteiro de teclas no lugar do teclado (u d l r mudam a escolha, e escolhe, b volta, p guarda
  # a tela) e termina na escolha: so para conferir o menu no Spike (TARGET=layout), com as telas em $OUT/spike.out
  if [ -n "${MENU_SCRIPT:-}" ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_MENU_SCRIPT=\"$MENU_SCRIPT\" -DDOOM_MENU_DUMP"
    # MENU_STOP=0 segue para o jogo depois da escolha, para ver no Spike o DOOM abrir a WAD escolhida
    if [ "${MENU_STOP:-1}" != 0 ]; then
      PORT_DEFS="$PORT_DEFS -DDOOM_MENU_STOP"
    fi
  fi
fi
if [ "${INTERACTIVE:-0}" = 1 ]; then
  # No modo interativo o Quit Game e o fim por erro reiniciam o programa (port/doom_runtime.c), com o .data
  # guardado em DOOM_DATA_BACKUP_ADDR
  PORT_DEFS="$PORT_DEFS -DDOOM_INTERACTIVE -DDOOM_DATA_BACKUP_ADDR=$DATA_BACKUP_ADDR"
  # IDLE_MS: ms sem evento de teclado ate voltar ao titulo (padrao 900000, 15 min; port/doom_video.h)
  if [ -n "${IDLE_MS:-}" ]; then
    PORT_DEFS="$PORT_DEFS -DDOOM_IDLE_RESET_MS=${IDLE_MS}u"
  fi
fi
# HUD=1 desenha FPS e MAX no canto superior direito (port/doom_video.c); padrao ligado no modo interativo com video,
# e desligado nas medicoes, que nao podem ter o desenho no quadro nem nas instrucoes
if [ "${VIDEO:-0}" = 1 ] && { [ "${HUD:-${INTERACTIVE:-0}}" = force ] || { [ "${HUD:-${INTERACTIVE:-0}}" = 1 ] && [ "${MEASURE:-0}" != 1 ]; }; }; then
  PORT_DEFS="$PORT_DEFS -DDOOM_FPS_HUD"
  # o HUD escreve direto no DG_ScreenBuffer, que e o proprio buffer do jogo (patch i_video.c): sem copia do quadro
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

# FatFs (SDFS=fatfs): so o ff.c; o ffconf.h e o da porta (port/ffconf.h) e o fetch.sh nao copia o do FatFs. Codigo de
# terceiros: sem os avisos do projeto
if [ "$SDFS" = fatfs ]; then
  if [ ! -f fatfs/source/ff.c ]; then
    echo "fatfs/source/ff.c ausente: rode fetch.sh" >&2
    exit 1
  fi
  $CC --specs=picolibc.specs $ARCH $OPT -std=gnu99 -w -ffunction-sections -Iport -Ifatfs/source \
    -c fatfs/source/ff.c -o "$OUT/obj/ff.o"
fi

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
    "$WAD_FILE" "$OUT/obj/wad.o"
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
