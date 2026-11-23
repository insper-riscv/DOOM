# SPDX-License-Identifier: Apache-2.0
"""Converte as telas do menu que o Spike imprimiu (MENU_SCRIPT no build.sh) em arquivos PNG.

Uso: python3 scripts/menu_png.py SAIDA_DO_SPIKE PASTA [AMPLIACAO]

Cada bloco `MENUDUMP BEGIN` ... `MENUDUMP END` do console traz 200 linhas de 320 indices em hexadecimal e 256 linhas
com a paleta RGB444 (vermelho nos bits 11:8). O arquivo `menu-N.png` de cada bloco sai ampliado por AMPLIACAO (2
por padrao), com os 4 bits de cada canal levados a 8 bits como faz o conversor de video da DE0-CV.
"""

import struct
import sys
import zlib
from pathlib import Path

WIDTH = 320
HEIGHT = 200
PALETTE_SIZE = 256


def read_blocks(text: str) -> list[tuple[list[bytes], list[tuple[int, int, int]]]]:
    """Separa os blocos MENUDUMP do console em (linhas de indices, paleta RGB)."""
    blocks = []
    lines = [line.strip() for line in text.splitlines()]
    i = 0
    while i < len(lines):
        if lines[i] != "MENUDUMP BEGIN":
            i += 1
            continue
        rows = [bytes.fromhex(lines[i + 1 + y]) for y in range(HEIGHT)]
        palette = []
        for k in range(PALETTE_SIZE):
            value = int(lines[i + 1 + HEIGHT + k], 16)
            red, green, blue = (value >> 8) & 0xF, (value >> 4) & 0xF, value & 0xF
            palette.append((red * 17, green * 17, blue * 17))
        blocks.append((rows, palette))
        i += 1 + HEIGHT + PALETTE_SIZE + 1
    return blocks


def write_png(path: Path, rows: list[bytes], palette: list[tuple[int, int, int]], scale: int) -> None:
    """Escreve um PNG RGB de 8 bits, ampliado por scale."""
    raw = bytearray()
    for row in rows:
        line = bytearray()
        for index in row:
            line += bytes(palette[index]) * scale
        for _ in range(scale):
            raw += b"\x00" + line

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", WIDTH * scale, HEIGHT * scale, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    )


def main() -> int:
    """Le o console do Spike e escreve um PNG por tela."""
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    blocks = read_blocks(Path(sys.argv[1]).read_text(errors="replace"))
    for n, (rows, palette) in enumerate(blocks, start=1):
        write_png(out / f"menu-{n}.png", rows, palette, scale)
    print(f"{len(blocks)} telas em {out}")
    return 0 if blocks else 1


if __name__ == "__main__":
    raise SystemExit(main())
