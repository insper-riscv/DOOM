#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Roda $OUT/doom.elf no Spike (rv32im, sem A, sem C, sem F), salva a saida em
# $OUT/spike.out e confere o CRC-32 do ultimo quadro contra o gabarito.
# Reexecuta no container dev_tools, a menos que ja rode nele.
# Variaveis: OUT (padrao build), RAM_ORIGIN e RAM_LENGTH (iguais as do build.sh).
# Termina com erro se o CRC-32 diferir do gabarito.
set -eu
cd "$(dirname "$0")"

. scripts/container.sh
run_in_container run.sh "$@"

OUT=${OUT:-build}
RAM_ORIGIN=${RAM_ORIGIN:-0x40000000}
RAM_LENGTH=${RAM_LENGTH:-0x04000000}
# CRC-32 do ultimo quadro do -timedemo demo1 (320x200 XRGB de 32 bits)
EXPECTED_CRC32=67371a4f

spike --disable-dtb --isa=rv32im_zicsr_zicntr -m"$RAM_ORIGIN:$RAM_LENGTH" \
  "$OUT/doom.elf" > "$OUT/spike.out" 2> "$OUT/spike.err" || echo "spike rc=$?"
tail -n 3 "$OUT/spike.out"
tail -n 5 "$OUT/spike.err"

if grep -q "frame_crc32=$EXPECTED_CRC32\$" "$OUT/spike.out"; then
  echo "CRC-32 confere com o gabarito: $EXPECTED_CRC32"
else
  echo "CRC-32 diferente do gabarito $EXPECTED_CRC32" >&2
  exit 1
fi
