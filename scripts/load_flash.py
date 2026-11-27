# SPDX-License-Identifier: Apache-2.0
"""Grava o `doom.bin` na janela FLASH da SDRAM da DE0-CV pela porta de depuração do Virtual JTAG.

Uso, dentro do container `dev_tools` com o Tools do commit `f7ea593` (ver docs/pt-br/INSTALAR.md):

    RV32_SDRAM_DEBUG_INSTANCE=3 python scripts/load_flash.py build-board/doom.bin "USB-Blaster [3-6]"

O primeiro argumento é a imagem e o segundo o nome do cabo que `jtagconfig` mostra com o IDCODE 02B050DD.
A imagem entra a partir de 0x1000, a base da janela FLASH, que fica no topo do chip de SDRAM (byte 0x3C01000).
A gravação relê tudo e termina com `flash: conferido` quando a leitura bate com a imagem.
"""

import sys
from pathlib import Path

from riscv_tools import ram_load
from riscv_tools.jtag import JtagLink

# Base da janela FLASH no mapa de endereços do núcleo, onde a BOOT salta.
FLASH_BASE = 0x1000
# Deslocamento em bytes da janela FLASH dentro do chip de SDRAM (4 MiB no topo do chip).
CHIP_BYTE = 0x3C01000
# Instância do Virtual JTAG que leva à porta de depuração da SDRAM.
SDRAM_INSTANCE = 3
# Nome do dispositivo na cadeia JTAG: a Cyclone V da DE0-CV, IDCODE 02B050DD.
DEVICE = "@1: 5CE(BA4|FA4) (0x02B050DD)"


def main(argv: list[str]) -> int:
    """Grava a imagem de `argv[1]` pelo cabo `argv[2]` e devolve o código de saída."""
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    link = JtagLink(hardware_name=argv[2], device_name=DEVICE, sdram_instance=SDRAM_INSTANCE)
    segment = ram_load.segment_from_file("flash", Path(argv[1]), FLASH_BASE)
    options = ram_load.LoadOptions(verify=True, chip_words=CHIP_BYTE // 4, report=print)
    ram_load.load_ram(link, [segment], FLASH_BASE, None, options)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
