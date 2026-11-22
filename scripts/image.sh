#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Monta a imagem da placa (build-board/doom.bin, TARGET=board) e imprime os comandos exatos
# do `riscv-tools` para carregar, acompanhar e conferir a execucao na DE0-CV. Nao toca a
# placa: so compila e escreve texto.
# Variaveis: INSPER_RISCV (raiz de um clone completo do Insper-RISC-V, que o riscv-tools usa
# como --root; padrao $HOME/Insper-RISC-V), OPT e PLATFORM_DIR (repassadas ao build.sh).
set -eu
cd "$(dirname "$0")/.."

. scripts/layout.sh

TARGET=board OUT=build-board sh build.sh

if [ ! -f doom1.wad ]; then
  echo "doom1.wad ausente: rode fetch.sh" >&2
  exit 1
fi
BIN_SIZE=$(wc -c < build-board/doom.bin)
if [ "$((RAM_ORIGIN + BIN_SIZE))" -gt "$((WAD_ADDR))" ]; then
  echo "o binario ($BIN_SIZE bytes) passa do endereco da WAD ($WAD_ADDR)" >&2
  exit 1
fi
WAD_BYTES=$(wc -c < doom1.wad)
if [ "$WAD_BYTES" -ne "$WAD_SIZE" ]; then
  echo "doom1.wad tem $WAD_BYTES bytes e o build espera $WAD_SIZE: rode fetch.sh" >&2
  exit 1
fi

RES_WORD=$(((RESULT_ADDR - RAM_ORIGIN) / 4))
FB_WORD=$(((FB_ADDR - RAM_ORIGIN) / 4))
FB_WORDS=$((FB_BYTES / 4))
RT='riscv-tools --root "$INSPER_RISCV" --config src/cyclone-v/doom/config.yaml'

cat <<EOF
Imagem: build-board/doom.bin ($BIN_SIZE bytes, a partir de $RAM_ORIGIN)
Mapa da SDRAM: WAD em $WAD_ADDR, framebuffer em $FB_ADDR, heap desde $HEAP_START,
resultado em $RESULT_ADDR (palavra $RES_WORD), campainha em $LAUNCH_ADDR.

Com a placa programada com o .sof da plataforma doom e o carregador gravado na banca A
(docs/pt-br/PLACA.md, secao 4), e INSPER_RISCV apontando para o clone do Insper-RISC-V:

1. Carregar a imagem e a WAD, conferir a leitura de volta e tocar a campainha:
   $RT load-ram build-board/doom.bin --wad doom1.wad --wad-address $WAD_ADDR

2. Acompanhar o console pela JTAG UART (o jogo imprime o progresso a cada 100 quadros e,
   no fim, repete o resumo a cada 2 s; Ctrl-C encerra):
   $RT console

3. Com o resumo na tela, ler o bloco de resultado e o framebuffer:
   $RT dump-ram build-board/result.mif --start-word $RES_WORD --words 16
   $RT dump-ram build-board/fb.mif --start-word $FB_WORD --words $FB_WORDS

4. Conferir contra o Spike (CRC-32 67371a4f) e gerar o PNG do ultimo quadro:
   uv run --no-project scripts/check_result.py build-board/result.mif --frame build-board/fb.mif --png build-board/frame.png
EOF
