#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Confere o menu de escolha do jogo no Spike, sem a placa: compila o alvo layout com MENU=1 e um roteiro de teclas,
# roda no Spike e confere a escolha que o menu devolveu; as telas ficam em build-layout/menu-png/menu-N.png.
# Precisa das suas WADs (fetch.sh --wad-dir PASTA, ou WAD_DIR) e roda no container dev_tools.
# Variaveis: WAD_SET (jogos do menu), MENU_SCRIPT (teclas), EXPECT (linha esperada do console).
#   Roteiro padrao: desce ate o Master Levels, entra, vai a segunda coluna e escolhe MINOS (DOOM II, mapa 5).
set -eu
cd "$(dirname "$0")/.."

. scripts/container.sh
run_in_container scripts/menu_test.sh "$@"

MENU_SCRIPT=${MENU_SCRIPT:-pdpdpdpepdprpe}
EXPECT=${EXPECT:-"[DOOM] menu: doom2.wad + minos.wad"}
OUT=build-layout

MENU=1 MENU_SCRIPT="$MENU_SCRIPT" TARGET=layout VIDEO=1 INTERACTIVE=1 OUT="$OUT" sh build.sh > "$OUT.menu.log" 2>&1 || {
  tail -n 20 "$OUT.menu.log" >&2
  exit 1
}
spike --disable-dtb --isa=rv32im_zicsr_zicntr -m0x40000000:0x04000000 "$OUT/doom.elf" > "$OUT/menu.out" 2> "$OUT/menu.err" ||
  echo "spike rc=$?"
if ! grep -a -q -F "$EXPECT" "$OUT/menu.out"; then
  echo "o menu nao devolveu a escolha esperada: $EXPECT" >&2
  grep -a "^\[DOOM\]" "$OUT/menu.out" >&2 || true
  exit 1
fi
python3 scripts/menu_png.py "$OUT/menu.out" "$OUT/menu-png"
echo "menu confere: $EXPECT"
