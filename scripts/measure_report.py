"""Calcula o CPI por janela de quadros a partir do bloco de medicao lido da placa.

Uso (so a biblioteca padrao, rodar pelo uv):

    uv run --no-project scripts/measure_report.py MEAS.mif [--clock-mhz 76.923077]

MEAS.mif e o dump-ram das palavras do bloco de medicao (port/doom_platform.h): 16 de cabecalho
e 16 por instantaneo. Cada par de instantaneos consecutivos e uma janela; o relatorio traz
ciclos, instrucoes, CPI e, com o Zihpm, os eventos por 1000 instrucoes e os ciclos por quadro.
"""

import argparse
import re
import sys
from pathlib import Path

MAGIC = 0x5341454D
HEAD, SNAP = 16, 16
EVENT_NAMES = {
    1: "parada mul/div (ciclos)",
    2: "espera RAM externa (ciclos)",
    3: "desvio/salto tomado",
    4: "acesso desalinhado",
    5: "falha cache de instrucoes",
    6: "falha cache de dados",
    7: "espera FLASH (ciclos)",
    8: "parada load-use (ciclos)",
    9: "operacao mul/div fundida",
}
LINE = re.compile(r"^\s*([0-9A-Fa-f]+)\s*:\s*([0-9A-Fa-f]+)\s*;")


def read_mif(path: Path) -> dict[int, int]:
    """Le o .mif do dump-ram: indice de palavra para valor."""
    words: dict[int, int] = {}
    for line in path.read_text().splitlines():
        match = LINE.match(line)
        if match:
            words[int(match.group(1), 16)] = int(match.group(2), 16)
    return words


def main() -> int:
    """Imprime a tabela de janelas; devolve 1 se o bloco nao estiver completo."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mif", type=Path)
    parser.add_argument("--clock-mhz", type=float, default=76.923077)
    args = parser.parse_args()
    w = read_mif(args.mif)
    if w.get(0, 0) != MAGIC:
        print("magia ausente: o bloco de medicao nao foi gravado")
        return 1
    count, step, nhpm = w[1], w[2], w[3]
    events = [w.get(4 + k, 0) for k in range(nhpm)]
    snaps = []
    for i in range(count):
        base = HEAD + i * SNAP
        s = [w.get(base + j, 0) for j in range(14)]
        snaps.append(
            {
                "frame": s[0],
                "cyc": (s[2] << 32) | s[1],
                "ins": (s[4] << 32) | s[3],
                "hpm": s[5 : 5 + nhpm],
            }
        )
    print(f"{count} instantaneos, passo de {step} quadros, clock {args.clock_mhz} MHz")
    for a, b in zip(snaps, snaps[1:], strict=False):
        frames = b["frame"] - a["frame"]
        cyc = b["cyc"] - a["cyc"]
        ins = b["ins"] - a["ins"]
        fps = args.clock_mhz * 1e6 / (cyc / frames)
        print(
            f"\njanela quadros {a['frame']}..{b['frame']}: ciclos {cyc}, instr {ins}, CPI {cyc / ins:.4f}, "
            f"instr/quadro {ins / frames:.0f}, ciclos/quadro {cyc / frames:.0f}, fps {fps:.3f}"
        )
        for k in range(nhpm):
            d = (b["hpm"][k] - a["hpm"][k]) & 0xFFFFFFFF
            name = EVENT_NAMES.get(events[k], f"evento {events[k]}")
            print(f"  {name:32} {d:>12}  {d / ins * 1000:9.2f} /1000 instr  {d / cyc * 100:6.2f} % dos ciclos")
    if len(snaps) > 2:
        a, b = snaps[0], snaps[-1]
        cyc, ins = b["cyc"] - a["cyc"], b["ins"] - a["ins"]
        print(f"\ntotal {a['frame']}..{b['frame']}: CPI {cyc / ins:.4f}, ciclos {cyc}, instr {ins}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
