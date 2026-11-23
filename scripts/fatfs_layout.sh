#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Forma sdcard_FatFs (SDFS=fatfs no build.sh): no lugar do scripts/wads.sh, que empacota as WADs, escreve so o que o
# build precisa sem olhar as WADs: uma tabela de WADs vazia (a lista de jogos vem do cartao em tempo de execucao,
# port/doom_fatfs.c) e o mapa da SDRAM do modo sob demanda, o mesmo que o wads.sh calcula com ONDEMAND=1.
#
# Uso: scripts/fatfs_layout.sh PASTA_DE_SAIDA
# Escreve em PASTA_DE_SAIDA: wad_table.h (tabelas vazias) e wads.env (WAD_ADDR, WAD_SIZE, FB_ADDR, HEAP_START, ...).
set -eu
cd "$(dirname "$0")/.."

OUT=${1:?uso: scripts/fatfs_layout.sh PASTA_DE_SAIDA}
mkdir -p "$OUT"

# A imagem do jogo (codigo, dados e bss, com os buffers do cartao e do FatFs) cabe em 2 MiB: o framebuffer vem depois
WAD_BASE=0x40200000
FB_ADDR=$WAD_BASE
HEAP_START=$((FB_ADDR + 0x40000))

cat > "$OUT/wad_table.h" <<'T'
/* Gerado por scripts/fatfs_layout.sh: nao edite. Tabelas vazias; o cartao FAT32 as preenche (SDFS=fatfs). */
#ifndef DOOM_WAD_TABLE_H
#define DOOM_WAD_TABLE_H
#include "doom_menu.h"
#define DOOM_WAD_COUNT 0
static const struct doom_wad_entry doom_wad_table[1] = { { "", 0U, 0U } };
static const struct doom_menu_game doom_menu_games[] = { { "", "", -1 } };
#define DOOM_MENU_GAME_COUNT 0
#define DOOM_MENU_MASTER_INDEX -1
#define DOOM_MENU_MASTER_IWAD -1
static const struct doom_menu_master doom_menu_masters[] = { { "", -1, 0 } };
#define DOOM_MENU_MASTER_COUNT 0
#endif
T

cat > "$OUT/wads.env" <<EOF2
# Gerado por scripts/fatfs_layout.sh: nao edite.
WAD_ADDR=$(printf '0x%08X' "$WAD_BASE")
WAD_SIZE=0
SD_ONDEMAND=1
SD_FATFS=1
WADS_BIN_SIZE=0
FB_ADDR=$(printf '0x%08X' "$FB_ADDR")
FB_BYTES=256000
HEAP_START=$(printf '0x%08X' "$HEAP_START")
MEASURE_ADDR=0x43780000
EOF2

echo "SDFS=fatfs: sem WADs no build; o cartao FAT32 traz DOOM.WAD, DOOM2.WAD, PLUTONIA.WAD, TNT.WAD e a pasta MASTER."
echo "framebuffer em $(printf '0x%08X' "$FB_ADDR"), heap desde $(printf '0x%08X' "$HEAP_START")"
