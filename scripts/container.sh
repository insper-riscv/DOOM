#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Biblioteca incluida (com ".") por build.sh e run.sh; nao e executada sozinha.
# Define run_in_container, que reexecuta o script chamador dentro do container
# dev_tools, a menos que ele ja rode la (DOOM_IN_CONTAINER=1).
# Variaveis: DEV_TOOLS_IMAGE (imagem do container), OPT, OUT, RAM_ORIGIN,
# RAM_LENGTH, TARGET e PLATFORM_DIR (repassadas ao container quando definidas).

DEV_TOOLS_IMAGE=${DEV_TOOLS_IMAGE:-ghcr.io/insper-riscv/dev_tools:latest}

# Uso: run_in_container <script> [argumentos...]
# Executa <script> no container com a pasta do projeto montada em /w.
run_in_container() {
  script=$1
  shift
  if [ "${DOOM_IN_CONTAINER:-0}" = 1 ]; then
    return 0
  fi
  if ! command -v docker > /dev/null 2>&1; then
    echo "docker indisponivel: rode no container dev_tools com DOOM_IN_CONTAINER=1" >&2
    exit 1
  fi
  # As WADs do usuario (fetch.sh --wad-dir) ficam fora da pasta do projeto: monta a pasta delas, so para leitura
  wad_dir=${WAD_DIR:-}
  if [ -z "$wad_dir" ] && [ -f .wad-dir ]; then
    wad_dir=$(cat .wad-dir)
  fi
  wad_mount=""
  if [ -n "$wad_dir" ] && [ -d "$wad_dir" ]; then
    wad_mount="-v $wad_dir:$wad_dir:ro"
    export WAD_DIR=$wad_dir
  fi
  # shellcheck disable=SC2086
  exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/w/.home $wad_mount -e WAD_DIR -e WAD_SET -e MENU -e MENU_SCRIPT -e MENU_STOP \
    -e DOOM_IN_CONTAINER=1 -e OPT -e OUT -e RAM_ORIGIN -e RAM_LENGTH -e TARGET -e PLATFORM_DIR -e MEASURE -e MEASURE_STOP \
    -e VIDEO -e INTERACTIVE -e TESTKEYS -e IDLE_MS -e PROFILE -e VSYNC -e VCRC_FRAMES -e EXPECTED_CRC32 \
    -v "$PWD":/w -w /w "$DEV_TOOLS_IMAGE" sh "/w/$script" "$@"
}
