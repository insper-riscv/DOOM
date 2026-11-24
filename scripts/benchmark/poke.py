# SPDX-License-Identifier: Apache-2.0
"""Escreve palavras na SDRAM da placa pelo JTAG e confere a escrita.

Uso (a partir da raiz do Insper-RISC-V, com o ambiente do riscv-tools):
    python poke.py ENDERECO PALAVRA [PALAVRA ...]

O endereco e o do nucleo (>= 0x40000000). O bench.sh o usa para zerar o bloco de medicao e o de
resultado antes de cada execucao, porque a SDRAM guarda os dados da execucao anterior mesmo depois de a
FPGA ser reconfigurada.
"""

import struct
import sys

from riscv_tools import ram_load
from riscv_tools.cli.common import jtag_link
from riscv_tools.settings import load_config

cfg = load_config("src/cyclone-v/doom/config.yaml")
link = jtag_link(cfg)
addr = int(sys.argv[1], 0)
data = b"".join(struct.pack("<I", int(w, 0)) for w in sys.argv[2:])
seg = ram_load.Segment("m", addr, data)
ram_load.load_ram(
    link, [seg], 0x40000000, None, ram_load.LoadOptions(verify=True, chip_words=0, report=print)
)
