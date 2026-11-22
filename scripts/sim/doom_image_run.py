"""Roda o binario do DOOM da placa no topo de simulacao da plataforma doom.

Copie este arquivo para `tests/python/unittests/rtl/` do Insper-RISC-V e use a entrada
`DoomImageRun` do tests.json (ver docs/pt-br/PLACA.md). A BOOT_MEMORY e o carregador da
FLASH sao os de verdade; a SDRAM ja traz o binario e a campainha (gen_sim_images.py).

O teste deixa o nucleo rodar DOOM_RUN_CYCLES ciclos do clock base (o nucleo anda a um
terco dele), le a JTAG UART como o console da placa, grava o texto em DOOM_UART_OUT e
confere que DOOM_EXPECT (texto separado por "|") aparece nele. Com DOOM_RESULT_WORD
(indice de palavra da SDRAM do bloco de resultado) le tambem esse bloco e confere a magia.
O modulo e independente do testbench do C do Insper-RISC-V (que exige o riscv_tools).
"""

import os
from pathlib import Path
from typing import Any

import cocotb
from cocotb.triggers import ClockCycles, Timer

from tests.python.unittests.rtl.de0_cv_sim_top import sdram_word, start
from tests.python.utils.checks import check

RESULT_MAGIC = 0x53455244
HALF_TCK_NS = 40


def cases(_build_dir: Path) -> list[dict[str, Any]]:
    """One simulation without a waveform file (millions of cycles would fill the disk)."""
    return [{"wave": False}]


def uart_states(dut: Any, **states: bool) -> None:
    """Drive the scan states of the UART JTAG (absent flags are low)."""
    for name in ("cdr", "sdr", "udr", "uir"):
        getattr(dut, f"i_uart_state_{name}").value = int(states.get(name, False))


async def uart_tck(dut: Any, **states: bool) -> None:
    """Clock the Virtual JTAG of the UART with the given flags high."""
    uart_states(dut, **states)
    for level in (0, 1):
        dut.i_uart_tck.value = level
        await Timer(HALF_TCK_NS, unit="ns")
    dut.i_uart_tck.value = 0
    uart_states(dut)


async def uart_scan(dut: Any) -> list[int]:
    """Scan the UART register once and return the bytes the core sent."""
    await uart_tck(dut, cdr=True)
    out = 0
    for i in range(48):
        dut.i_uart_tdi.value = 0
        await Timer(1, unit="ns")
        out |= int(dut.o_uart_tdo.value) << i
        await uart_tck(dut, sdr=True)
    await uart_tck(dut, udr=True)
    count = (out >> 32) & 7
    return [(out >> (8 * i)) & 0xFF for i in range(count)]


async def uart_console(dut: Any, received: list[int], stop: list[bool]) -> None:
    """Scan the UART until told to stop, always finishing a scan."""
    dut.i_uart_ir_in.value = 1
    await uart_tck(dut, uir=True)
    while not stop[0]:
        received.extend(await uart_scan(dut))


@cocotb.test()
async def test_the_doom_image_boots_and_prints_on_the_uart(dut: Any) -> None:
    """The image jumps from the loader, runs and writes to the JTAG UART."""
    await start(dut)
    received: list[int] = []
    stop = [False]
    cocotb.start_soon(uart_console(dut, received, stop))
    await ClockCycles(dut.i_clk, int(os.environ.get("DOOM_RUN_CYCLES", "3000000")))
    stop[0] = True
    await ClockCycles(dut.i_clk, 2000)
    dut._log.info("pc of the instruction fetch: %s", dut.if_addr.value)
    for word in filter(None, os.environ.get("DOOM_PROBE_WORDS", "").split(",")):
        dut._log.info("SDRAM word %s = %#010x", word, await sdram_word(dut, int(word, 0)))
    text = bytes(received).decode("ascii", errors="replace")
    out = os.environ.get("DOOM_UART_OUT")
    if out:
        Path(out).write_text(text)
    dut._log.info("UART (%d bytes):\n%s", len(text), text)
    for needle in filter(None, os.environ.get("DOOM_EXPECT", "").split("|")):
        check(needle in text, f"{needle!r} not in the UART text")
    result_word = os.environ.get("DOOM_RESULT_WORD")
    if result_word:
        base = int(result_word, 0)
        words = [await sdram_word(dut, base + i) for i in range(11)]
        dut._log.info("result block: %s", [f"{w:08x}" for w in words])
        check(words[0] == RESULT_MAGIC, f"result magic {words[0]:#x}")
