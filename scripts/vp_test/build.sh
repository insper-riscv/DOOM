#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Compila o teste de video e teclado para rv32im, ligado em 0x40000000 (SDRAM), e gera out/vp_test.bin para o
# `riscv-tools load-ram`. Rodar no container dev_tools com a raiz do repositorio montada, por exemplo:
#   docker run --rm -v "$PWD":/w -w /w ghcr.io/insper-riscv/dev_tools:latest sh scripts/vp_test/build.sh
set -eu
cd "$(dirname "$0")"

O=out
mkdir -p "$O"
CC=riscv32-unknown-elf-gcc
FLAGS="-march=rv32im_zicsr_zifencei -mabi=ilp32 -std=gnu23 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
  -fno-tree-loop-distribute-patterns -nostdlib -nostartfiles -static"
$CC $FLAGS -T link.ld start.S vp_test.c -Wl,-Map="$O/vp_test.map" -o "$O/vp_test.elf"
riscv32-unknown-elf-objcopy -O binary "$O/vp_test.elf" "$O/vp_test.bin"
riscv32-unknown-elf-objdump -d "$O/vp_test.elf" > "$O/vp_test.dis"
riscv32-unknown-elf-size "$O/vp_test.elf"
ls -l "$O/vp_test.bin"
