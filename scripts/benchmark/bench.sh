#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Benchmark do DOOM na DE0-CV: roda demos de varios jogos pelo cartao SD e mede os ciclos por quadro,
# pela janela de 100 quadros e quadro a quadro. Mexe na placa (reprograma a FPGA e escreve na SDRAM).
#
# Variaveis obrigatorias:
#   INSPER_RISCV    raiz de um clone do Insper-RISC-V (o riscv-tools usa como --root)
#   TOOLS           clone do Tools (o riscv-tools e o poke.py rodam com `uv run --project $TOOLS`)
#   SOF             .sof do SoC DOOM a 100 MHz (a plataforma doom do Insper-RISC-V)
#   LOAD_FLASH      load_flash_nohold.py (carrega o doom.bin na janela de instrucoes da SDRAM)
#   WAD_DIR         pasta com as WADs (as mesmas de fetch.sh --wad-dir)
# Variaveis opcionais:
#   CABLE           cabo JTAG (RISCV_TOOLS_CABLE e o -c do quartus_pgm); padrao o primeiro
#   WAD_SET         jogos do menu (padrao "doom doom2 plutonia tnt master"); precisa casar com o cartao
#   DEMOS           lista "jogo:demo:nome:hud" separada por espacos; jogo e o indice no menu (0 Ultimate DOOM,
#                   1 DOOM II, 2 Plutonia, 3 TNT), demo e demo1 a demo3, hud e 0 ou force
#                   (padrao: as 6 medidas da tabela de docs/pt-br/BENCHMARK.md)
#   STOP            quadro em que o jogo encerra (padrao 330)
#   MAX_WAIT        maximo de esperas de 30 s por medida (padrao 16)
#   SDFS            raw (padrao) ou fatfs, igual ao cartao que esta na placa
#   OUT_DIR         onde ficam os dumps e o relatorio (padrao build-bench/resultado)
set -eu
cd "$(dirname "$0")/../.."

: "${INSPER_RISCV:?defina INSPER_RISCV}" "${TOOLS:?defina TOOLS}" "${SOF:?defina SOF}" "${LOAD_FLASH:?defina LOAD_FLASH}" "${WAD_DIR:?defina WAD_DIR}"
CABLE=${CABLE:-}
WAD_SET=${WAD_SET:-doom doom2 plutonia tnt master}
STOP=${STOP:-330}
MAX_WAIT=${MAX_WAIT:-16}
SDFS=${SDFS:-raw}
OUT_DIR=${OUT_DIR:-build-bench/resultado}
DEMOS=${DEMOS:-"0:demo1:ultimate_demo1:0 1:demo1:doom2_demo1:0 1:demo2:doom2_demo2:0 1:demo2:doom2_demo2_hud:force 1:demo3:doom2_demo3:0 2:demo1:plutonia_demo1:0 3:demo1:tnt_demo1:0"}
HERE=$(pwd)
RT="uv run --project $TOOLS riscv-tools --root $INSPER_RISCV --config src/cyclone-v/doom/config.yaml"
PY="uv run --project $TOOLS python"
[ -n "$CABLE" ] && export RISCV_TOOLS_CABLE="$CABLE"
PGM_CABLE=${CABLE:-1}
mkdir -p "$OUT_DIR"

# Enderecos da SDRAM (scripts/layout.sh e scripts/wads.sh): palavra do dump-ram = endereco / 4
MEASURE_WORD=$((0x43780000 / 4)) # bloco de medicao (instantaneos)
FRAME_WORD=$((MEASURE_WORD + 2048)) # ciclos de cada quadro (DOOM_MEAS_FRAME_OFF)
RESULT_WORD=$((0x43BFF000 / 4))      # bloco de resultado

zeros() {
  python3 -c "print(' '.join(['0'] * $1))"
}

program_fpga() {
  quartus_pgm -c "$PGM_CABLE" -m jtag -o "p;$SOF" > /dev/null
}

for cfg in $DEMOS; do
  game=${cfg%%:*}; rest=${cfg#*:}
  demo=${rest%%:*}; rest=${rest#*:}
  name=${rest%%:*}; hud=${rest#*:}
  echo "=== $name"

  # Imagem de medicao: sem menu manual, o jogo escolhido roda o demo ate o quadro STOP e para
  rm -rf build-bench/img
  OUT=build-bench/img TARGET=board OPT="-O3 -funroll-loops -flto" HUD="$hud" SDFS="$SDFS" \
    WAD_DIR="$WAD_DIR" WAD_SET="$WAD_SET" PLATFORM_DIR=platform_doom MENU=1 MENU_AUTO=1 \
    MENU_AUTO_GAME="$game" MENU_AUTO_DEMO="$demo" MEASURE=1 MEASURE_STOP="$STOP" VIDEO=1 \
    INTERACTIVE=1 SD=demand ONDEMAND=1 sh build.sh > "$OUT_DIR/$name.build.log" 2>&1

  # A SDRAM guarda a execucao anterior: zera os blocos antes de gravar
  (cd "$INSPER_RISCV" && $PY "$HERE/scripts/benchmark/poke.py" 0x43780000 $(zeros 3100) > /dev/null)
  (cd "$INSPER_RISCV" && $PY "$HERE/scripts/benchmark/poke.py" 0x43BFF000 $(zeros 16) > /dev/null)
  program_fpga
  (cd "$INSPER_RISCV" && $PY "$LOAD_FLASH" "$HERE/build-bench/img/doom.bin") | tail -1
  program_fpga

  # Espera o bloco de resultado ganhar a magia SERD (o jogo chegou ao quadro STOP)
  i=0
  while [ "$i" -lt "$MAX_WAIT" ]; do
    sleep 30
    $RT dump-ram "$OUT_DIR/$name.res.mif" --start-word "$RESULT_WORD" --words 16 > /dev/null
    grep -q "53455244;" "$OUT_DIR/$name.res.mif" && break
    i=$((i + 1))
  done
  $RT dump-ram "$OUT_DIR/$name.meas.mif" --start-word "$MEASURE_WORD" --words 1040 > /dev/null
  $RT dump-ram "$OUT_DIR/$name.frm.mif" --start-word "$FRAME_WORD" --words 1024 > /dev/null
  python3 -I scripts/benchmark/analyze.py "$OUT_DIR/$name.meas.mif" "$OUT_DIR/$name.frm.mif" \
    "$OUT_DIR/$name.res.mif" | tee "$OUT_DIR/$name.txt"
done
echo "Relatorios em $OUT_DIR/*.txt"
