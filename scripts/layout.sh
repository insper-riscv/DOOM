#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Biblioteca incluida (com ".") por build.sh, run.sh e image.sh; nao e executada sozinha.
# Fonte unica do mapa da SDRAM da plataforma doom que a porta usa. Todos os enderecos
# sao de byte, na SDRAM (base 0x40000000, 64 MB). Ver docs/pt-br/PLACA.md.
#
#   0x40000000  imagem (codigo, constantes, dados): cabe em ate 1 MiB com o bss
#   0x40100000  WAD (4.196.020 B), carregada por `load-ram --wad`
#   0x40580000  framebuffer 320x200 XRGB de 32 bits (256.000 B), fixo para o dump-ram
#   0x405C0000  inicio do heap (a zona do jogo, 6 MB, sai daqui)
#   0x43FEF000  base da pilha (64 KiB abaixo do topo livre)
#   0x43FFF000  bloco de resultado (64 B) na ultima pagina da SDRAM, a que a cache ignora

RAM_ORIGIN=${RAM_ORIGIN:-0x40000000}
WAD_ADDR=0x40100000
WAD_SIZE=4196020
FB_ADDR=0x40580000
FB_BYTES=256000
HEAP_START=0x405C0000
RESULT_ADDR=0x43FFF000
# Bloco de medicao de CPI (MEASURE=1 no build.sh): 64 B de cabecalho e 64 instantaneos de 64 B,
# de 0x43000000 a 0x43001040 (SDRAM comum, atras da cache de dados de escrita direta; cabe os 64 instantaneos)
MEASURE_ADDR=0x43000000
# Bloco de estatisticas do video indexado (VIDEO=1): 16 palavras de cabecalho e, de 0x43FFF140, o par
# (CRC dos indices, CRC da paleta) dos primeiros quadros; 0x43FFF100 a 0x43FFF240 (port/doom_video.h), na
# ultima pagina, acima do resultado (0x43FFF000, 64 B) e abaixo do stdout da plataforma (0x43FFFBE0)
VSTAT_ADDR=0x43FFF100
# Termina onde comeca a ultima pagina da SDRAM (0x43FFF000): o topo da pilha
BOARD_RAM_LENGTH=0x3FFF000
# Palavra da campainha (platform.yaml, reserved.launch) e magia do carregador
LAUNCH_ADDR=0x43FFFFF8
