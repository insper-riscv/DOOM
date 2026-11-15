"""Confere o resultado do DOOM lido da placa com o `dump-ram` contra o gabarito do Spike.

Uso (so a biblioteca padrao, rodar pelo uv):

    uv run --no-project scripts/check_result.py RESULT.mif [--frame FB.mif] [--png FB.png]

RESULT.mif e o dump das 16 palavras do bloco de resultado (port/doom_platform.h) e FB.mif o
dump das 64000 palavras do framebuffer. Termina com 0 so se a magia confere e o CRC-32 e
o do Spike. Com --frame, tambem recalcula o CRC-32 dos bytes lidos e o compara com o do
bloco; com --png, grava a imagem 320x200.
"""

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

# CRC-32 do ultimo quadro do -timedemo demo1 no Spike (320x200 XRGB de 32 bits)
EXPECTED_CRC32 = 0x67371A4F
RESULT_MAGIC = 0x53455244
WIDTH, HEIGHT = 320, 200
FIELDS = (
    "magic", "exit", "crc32", "frames", "instret_lo", "instret_hi", "per_frame",
    "fb_addr", "ms", "cycle_lo", "cycle_hi",
)
LINE = re.compile(r"^\s*([0-9A-Fa-f]+)\s*:\s*([0-9A-Fa-f]+)\s*;")


def read_mif(path: Path) -> dict[int, int]:
    """Le um .mif do dump-ram: indice de palavra para o valor de 32 bits."""
    words: dict[int, int] = {}
    for line in path.read_text().splitlines():
        match = LINE.match(line)
        if match:
            words[int(match.group(1), 16)] = int(match.group(2), 16)
    return words


def write_png(path: Path, pixels: list[int]) -> None:
    """Grava os pixels XRGB (um por palavra) como PNG RGB de 8 bits, so com zlib."""
    rows = bytearray()
    for y in range(HEIGHT):
        rows.append(0)
        for value in pixels[y * WIDTH : (y + 1) * WIDTH]:
            rows += bytes(((value >> 16) & 255, (value >> 8) & 255, value & 255))

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(rows))) + chunk(b"IEND", b"")
    )


def main() -> int:
    """Confere o bloco e, se dado, o framebuffer; devolve o codigo de saida."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result", type=Path)
    parser.add_argument("--frame", type=Path, default=None)
    parser.add_argument("--png", type=Path, default=None)
    args = parser.parse_args()

    block = read_mif(args.result)
    values = dict(zip(FIELDS, (block.get(i, 0) for i in range(len(FIELDS))), strict=True))
    for name, value in values.items():
        print(f"{name:11} = {value:#010x} ({value})")
    ok = True
    if values["magic"] != RESULT_MAGIC:
        print("magia ausente: o jogo ainda nao terminou (ou nao rodou)")
        return 1
    instret = (values["instret_hi"] << 32) | values["instret_lo"]
    cycles = (values["cycle_hi"] << 32) | values["cycle_lo"]
    print(f"instret = {instret}, ciclos = {cycles}, CPI = {cycles / max(instret, 1):.3f}")
    if values["crc32"] == EXPECTED_CRC32:
        print(f"CRC-32 do bloco confere com o Spike: {EXPECTED_CRC32:08x}")
    else:
        print(f"CRC-32 do bloco {values['crc32']:08x} difere do Spike {EXPECTED_CRC32:08x}")
        ok = False
    if args.frame is not None:
        frame = read_mif(args.frame)
        count = WIDTH * HEIGHT
        missing = [i for i in range(count) if i not in frame]
        if missing:
            print(f"faltam {len(missing)} palavras do framebuffer (a primeira: {missing[0]})")
            return 1
        pixels = [frame[i] for i in range(count)]
        crc = zlib.crc32(struct.pack(f"<{count}I", *pixels))
        if crc == values["crc32"]:
            print(f"CRC-32 do framebuffer lido pelo dump-ram confere: {crc:08x}")
        else:
            print(f"CRC-32 do framebuffer lido {crc:08x} difere do bloco {values['crc32']:08x}")
            ok = False
        if args.png is not None:
            write_png(args.png, pixels)
            print(f"imagem em {args.png}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
