#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Monta a imagem da placa (build-board/doom.bin, TARGET=board) e imprime os comandos exatos
# do `riscv-tools` para carregar, acompanhar e conferir a execucao na DE0-CV. Nao toca a
# placa: so compila e escreve texto.
# Variaveis: INSPER_RISCV (raiz de um clone completo do Insper-RISC-V, que o riscv-tools usa
# como --root; padrao $HOME/Insper-RISC-V), OPT (padrao -O3 -funroll-loops) e PLATFORM_DIR (repassadas ao build.sh). MENU=1 (com VIDEO=1 e
# INTERACTIVE=1) monta o menu com as WADs da pasta de `fetch.sh --wad-dir` (WAD_SET escolhe os jogos).
set -eu
cd "$(dirname "$0")/.."

. scripts/layout.sh

# A imagem da placa sai com -O3 -funroll-loops por padrao: no Spike isso corta 6,4% das instrucoes por quadro do
# timedemo (de 1.722.158 para 1.612.385) com o mesmo CRC-32 dos quadros; OPT escolhe outro nivel.
OPT=${OPT:--O3 -funroll-loops -flto}
export OPT
TARGET=board OUT=build-board sh build.sh

if [ "${MENU:-0}" = 1 ]; then
  # Menu: as WADs do usuario, juntas em $OUT/wads.bin pelo scripts/wads.sh, e o mapa que ele calculou
  . build-board/wads.env
  WAD_FILE=build-board/wads.bin
  WAD_LABEL="wads.bin (menu)"
  if [ "${SDFS:-raw}" = fatfs ]; then
    # sdcard_FatFs: as WADs ficam num FAT32 comum no cartao; nao ha wads.bin
    WAD_FILE=/dev/null
    WAD_LABEL="cartao FAT32 (FatFs, sem wads.bin)"
  fi
else
  WAD_FILE=doom1.wad
  WAD_LABEL=doom1.wad
  if [ ! -f doom1.wad ]; then
    echo "doom1.wad ausente: rode fetch.sh" >&2
    exit 1
  fi
fi
BIN_SIZE=$(wc -c < build-board/doom.bin)
# A imagem vai para a FLASH (a janela de instrucoes da SDRAM, ligada a partir de 0x1000, 4 MiB menos 4 KiB)
FLASH_BASE=0x1000
if [ "$((FLASH_BASE + BIN_SIZE))" -gt "$((0x400000))" ]; then
  echo "o binario ($BIN_SIZE bytes) passa do fim da FLASH (0x400000)" >&2
  exit 1
fi
WAD_BYTES=$(wc -c < "$WAD_FILE")
if [ "${SD_ONDEMAND:-0}" = 1 ]; then
  : # WADs so no cartao: no modo cru, wads.bin tem $WADS_BIN_SIZE bytes e vai pelo scripts/sdwrite.py; com SDFS=fatfs, sao arquivos copiados no FAT32
elif [ "$WAD_BYTES" -ne "$WAD_SIZE" ]; then
  echo "$WAD_FILE tem $WAD_BYTES bytes e o build espera $WAD_SIZE: rode fetch.sh" >&2
  exit 1
fi

RES_WORD=$(((RESULT_ADDR - RAM_ORIGIN) / 4))
FB_WORD=$(((FB_ADDR - RAM_ORIGIN) / 4))
FB_WORDS=$((FB_BYTES / 4))
RT='riscv-tools --root "$INSPER_RISCV" --config src/cyclone-v/doom/config.yaml'

cat <<EOF
Imagem: build-board/doom.bin ($BIN_SIZE bytes, para a FLASH em $FLASH_BASE: codigo, constantes e valores iniciais de .data)
Mapa da SDRAM: $WAD_LABEL em $WAD_ADDR ($WAD_BYTES bytes), framebuffer em $FB_ADDR, heap desde $HEAP_START,
resultado em $RESULT_ADDR (palavra $RES_WORD do chip).

Com a placa programada com o .sof da plataforma doom (sem FLASH emulada nem carregador: a FLASH e uma
janela da SDRAM) e INSPER_RISCV apontando para o clone do Insper-RISC-V:

1. Carregar a WAD na RAM pela porta de depuracao e depois a imagem na FLASH, que prende o nucleo,
   grava, confere o CRC-32 na placa e solta o nucleo (a BOOT salta para 0x1000):
   $RT load-ram $WAD_FILE --address $WAD_ADDR --no-launch
   $RT load-flash --image build-board/doom.bin --address $FLASH_BASE

   Com SDFS=fatfs (sdcard_FatFs): o cartao e um FAT32 comum (cluster de 32 KB ou mais) com DOOM.WAD, DOOM2.WAD,
   PLUTONIA.WAD e TNT.WAD na raiz e as fases na pasta MASTER, copiados pelo explorador de arquivos, sem sdwrite.py;
   carregue so o jogo: $RT load-ram build-board/doom.bin

   Com o cartao microSD cru (SD=load ou SD=demand no build): grave o wads.bin no cartao (scripts/sdwrite.py, ensaio com
   --dry-run) e carregue so o jogo, sem --wad (o jogo copia ou le a WAD do cartao):
   $RT load-ram build-board/doom.bin

2. Acompanhar o console pela JTAG UART (o jogo imprime o progresso a cada 100 quadros e,
   no fim, repete o resumo a cada 2 s; Ctrl-C encerra):
   $RT console

3. Com o resumo na tela, ler o bloco de resultado e o framebuffer:
   $RT dump-ram build-board/result.mif --start-word $RES_WORD --words 16
   $RT dump-ram build-board/fb.mif --start-word $FB_WORD --words $FB_WORDS

4. Conferir contra o Spike (CRC-32 67371a4f) e gerar o PNG do ultimo quadro:
   uv run --no-project scripts/check_result.py build-board/result.mif --frame build-board/fb.mif --png build-board/frame.png
EOF
