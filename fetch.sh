#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Baixa a doomgeneric (commit fixado) em doomgeneric/, o runtime da plataforma doom
# (commit fixado do Insper-RISC-V) em Insper-RISC-V/ e, se voce nao indicar as suas WADs, a doom1.wad
# shareware v1.9 em doom1.wad, conferindo o SHA-256 e o SHA-1 da WAD.
# Precisa de git, curl e sha256sum no host.
#
# Uso: ./fetch.sh [--wad-dir PASTA]
#   --wad-dir PASTA  usa as WADs que estao em PASTA (doom.wad, doom2.wad, plutonia.wad, tnt.wad e a pasta do Master
#                    Levels, em qualquer nivel ate 4 abaixo, sem diferenciar maiusculas de minusculas) no lugar da
#                    shareware: nao baixa a doom1.wad, guarda a pasta em .wad-dir e lista as WADs achadas. O
#                    build com MENU=1 (build.sh) le essa pasta.
set -eu
cd "$(dirname "$0")"

WAD_DIR_ARG=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --wad-dir)
      [ "$#" -ge 2 ] || { echo "--wad-dir pede a pasta das WADs" >&2; exit 2; }
      WAD_DIR_ARG=$2
      shift 2
      ;;
    --wad-dir=*)
      WAD_DIR_ARG=${1#--wad-dir=}
      shift
      ;;
    -h | --help)
      sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "argumento desconhecido: $1 (use --wad-dir PASTA)" >&2
      exit 2
      ;;
  esac
done

# Commit da doomgeneric (GPL-2.0) usado na porta
DG_COMMIT=e257a09527062bfb986b6e5e0780839b6cbd5b71
# SHA-256 e SHA-1 da doom1.wad shareware v1.9, 4196020 bytes
WAD_SHA256=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771
WAD_SHA1=5b2e249b9c5133ec987b3ea77596381dc0d6bc1d
WAD_URL="https://raw.githubusercontent.com/Doom-Utils/shareware-collection/master/Doom%201.9/doom1.wad"

if [ ! -d doomgeneric/.git ]; then
  git clone -q https://github.com/ozkl/doomgeneric.git doomgeneric
fi
git -C doomgeneric checkout -q "$DG_COMMIT"

# Runtime da plataforma doom (Apache-2.0) do Insper-RISC-V, em commit fixado: so a pasta
# src/cyclone-v/doom/runtime, de onde o build copia o doom_clock.c e le o memory_map.h
IR_COMMIT=20e71cca6d3e25d7bbee49ace0a751dbf7ec3cd0
if [ ! -d Insper-RISC-V/.git ]; then
  git clone -q --filter=blob:none --no-checkout https://github.com/insper-riscv/Insper-RISC-V.git Insper-RISC-V
fi
git -C Insper-RISC-V sparse-checkout set --no-cone /src/cyclone-v/doom/runtime
git -C Insper-RISC-V checkout -q "$IR_COMMIT"

if [ -n "$WAD_DIR_ARG" ]; then
  # As WADs do usuario: confere que a pasta existe, guarda o caminho e lista o que achou
  if [ ! -d "$WAD_DIR_ARG" ]; then
    echo "$WAD_DIR_ARG nao e uma pasta" >&2
    exit 1
  fi
  WAD_DIR_ABS=$(cd "$WAD_DIR_ARG" && pwd)
  printf '%s\n' "$WAD_DIR_ABS" > .wad-dir
  echo "WADs em $WAD_DIR_ABS (a doom1.wad shareware nao foi baixada):"
  found=0
  for name in doom.wad doom2.wad plutonia.wad tnt.wad; do
    f=$(find -L "$WAD_DIR_ABS" -maxdepth 4 -type f -iname "$name" | sort | head -n 1)
    if [ -n "$f" ]; then
      found=$((found + 1))
      sum=$(sha256sum "$f" | cut -d' ' -f1)
      # Hashes de referencia: as WADs v1.9 da copia DOS do pacote DOOM + DOOM II; uma WAD de outra edicao avisa mas segue
      case "$name" in
        doom.wad) ref=6fdf361847b46228cfebd9f3af09cd844282ac75f3edbb61ca4cb27103ce2e7f ;;
        doom2.wad) ref=10d67824b11025ddd9198e8cfc87ca335ee6e2d3e63af4180fa9b8a471893255 ;;
        plutonia.wad) ref=a83b00c636fa3308286e76b1b3153fc14507caf994b0450770421260b08efed8 ;;
        tnt.wad) ref=c0a9c29d023af2737953663d0e03177d9b7b7b64146c158dcc2a07f9ec18f353 ;;
      esac
      if [ "$sum" = "$ref" ]; then
        echo "  $name: $f (SHA-256 confere com a referencia da copia DOS v1.9)"
      else
        echo "  $name: $f (SHA-256 $sum difere da referencia da copia DOS v1.9: confira a edicao; o gabarito do timedemo pode mudar)"
      fi
    fi
  done
  masters=$(find -L "$WAD_DIR_ABS" -maxdepth 4 -type f -iname '*.wad' -ipath '*master*' | wc -l)
  echo "  Master Levels: $masters WADs"
  if [ "$found" -eq 0 ]; then
    echo "nenhuma das WADs doom.wad, doom2.wad, plutonia.wad ou tnt.wad esta em $WAD_DIR_ABS" >&2
    exit 1
  fi
else
  rm -f .wad-dir
  if [ ! -f doom1.wad ]; then
    curl -fsSL -o doom1.wad "$WAD_URL"
  fi
  echo "$WAD_SHA256  doom1.wad" | sha256sum -c -
  echo "$WAD_SHA1  doom1.wad" | sha1sum -c -
fi
