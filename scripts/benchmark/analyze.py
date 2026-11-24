# SPDX-License-Identifier: Apache-2.0
"""Calcula fps por janela e por quadro a partir dos blocos de medicao lidos da placa.

Uso: python analyze.py MEDICAO.mif QUADROS.mif RESULTADO.mif [PRIMEIRO_QUADRO]

MEDICAO.mif   dump do bloco de medicao (instantaneos a cada 100 quadros; port/doom_platform.h)
QUADROS.mif   dump dos ciclos de cada quadro (a partir da palavra DOOM_MEAS_FRAME_OFF do bloco)
RESULTADO.mif dump do bloco de resultado (16 palavras)
PRIMEIRO_QUADRO primeiro quadro da analise por quadro (padrao 101, depois da carga pelo cartao); o
                ultimo e o numero de quadros do bloco de resultado
"""

import re
import sys

MEAS_MAGIC = 0x5341454D
RESULT_MAGIC = 0x53455244
HEAD_WORDS = 16
SNAP_WORDS = 16
CLOCK_HZ = 100e6
TARGET_FPS = 35.0


def read_mif(path):
    """Devolve {indice_da_palavra: valor} do conteudo de um .mif."""
    words = {}
    base = None
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*([0-9A-Fa-f]+)\s*:\s*([0-9A-Fa-f]{8});", line)
        if m:
            addr = int(m.group(1), 16)
            if base is None:
                base = addr
            words[addr - base] = int(m.group(2), 16)
    return words


def snapshots(words):
    """Lista (quadro, ciclos, instrucoes) dos instantaneos gravados."""
    if words.get(0) != MEAS_MAGIC:
        return []
    out = []
    for k in range(words.get(1, 0)):
        b = HEAD_WORDS + SNAP_WORDS * k
        out.append(
            (words[b], (words[b + 2] << 32) | words[b + 1], (words[b + 4] << 32) | words[b + 3])
        )
    return out


def report_windows(snaps):
    """Imprime o fps de cada janela entre instantaneos."""
    for a, b in zip(snaps, snaps[1:]):
        cyc, frames, ins = b[1] - a[1], b[0] - a[0], b[2] - a[2]
        print(
            f"  janela {a[0]}..{b[0]}: {frames * CLOCK_HZ / cyc:.2f} fps (CPI {cyc / ins:.2f})"
        )


def report_frames(words, first, last):
    """Imprime o minimo por quadro, as piores janelas curtas e a fracao abaixo de 35 fps."""
    cycles = [words[i] for i in sorted(words) if words[i] != 0]
    frames = cycles[first - 1 : last]
    if not frames:
        print("  sem dados por quadro")
        return
    fps = [CLOCK_HZ / c for c in frames]
    print(
        f"  por quadro ({first}..{first + len(frames) - 1}): minimo {min(fps):.1f} fps "
        f"(quadro mais lento {max(frames)} ciclos), media {len(frames) * CLOCK_HZ / sum(frames):.1f}"
    )
    for k in (5, 10, 25):
        if len(frames) >= k:
            worst = max(sum(frames[i : i + k]) for i in range(len(frames) - k + 1))
            print(f"  pior janela de {k} quadros: {k * CLOCK_HZ / worst:.1f} fps")
    below = sum(1 for f in fps if f < TARGET_FPS)
    print(f"  quadros abaixo de {TARGET_FPS:.0f} fps: {below} de {len(fps)}")


def report_result(words):
    """Imprime a magia, o CRC e a contagem de quadros do bloco de resultado."""
    magic = words.get(0, 0)
    state = "ok" if magic == RESULT_MAGIC else "execucao nao terminou"
    print(f"  resultado: magia {magic:#x} ({state}), crc {words.get(2, 0):#x}, quadros {words.get(3)}")


def main():
    """Le os tres dumps e imprime o relatorio."""
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    first = int(sys.argv[4]) if len(sys.argv) > 4 else 101
    report_windows(snapshots(read_mif(sys.argv[1])))
    result = read_mif(sys.argv[3])
    report_frames(read_mif(sys.argv[2]), first, result.get(3) or 10**9)
    report_result(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
