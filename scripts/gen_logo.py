#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Gera port/doom_logo.h: a palavra INSPER em letras pesadas, vermelho sangue, com gotas escorrendo, no estilo do DOOM.

Uso (so biblioteca padrao mais Pillow, rodado por uv):

    uv run --no-project --with pillow scripts/gen_logo.py [--png preview.png]

O bitmap tem 5 valores por pixel: 0 transparente, 1 contorno escuro, 2 sangue escuro, 3 sangue, 4 brilho. O desenho em
tempo de execucao (port/doom_logo.c) troca cada valor pela entrada da paleta do jogo mais proxima da cor.
A semente fixa das gotas faz a saida ser sempre a mesma.
"""

from __future__ import annotations

import argparse
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TEXT = "INSPER"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
SIZE = 19
SEED = 1993


def render() -> list[list[int]]:
    font = ImageFont.truetype(FONT, SIZE)
    left, top, right, bottom = font.getbbox(TEXT)
    w, h = right - left + 4, bottom - top + 14
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((2 - left, 2 - top), TEXT, font=font, fill=255)
    mask = [[1 if img.getpixel((x, y)) > 110 else 0 for x in range(w)] for y in range(h)]

    # gotas: em colunas escolhidas, a partir do ultimo pixel da letra, com comprimentos diferentes
    rng = random.Random(SEED)
    body_bottom = max(y for y in range(h) for x in range(w) if mask[y][x])
    x = 3
    while x < w - 3:
        col = [y for y in range(h) if mask[y][x]]
        if col and rng.random() < 0.32:
            y0 = max(col)
            if y0 >= body_bottom - 2:
                length = rng.randint(3, 7)
                width = 2
                for dx in range(width):
                    for dy in range(1, length + 1):
                        if y0 + dy < h and x + dx < w:
                            mask[y0 + dy][x + dx] = 1
                if y0 + length + 1 < h:  # gota redonda no fim
                    for dx in range(width):
                        mask[y0 + length + 1][x + dx] = 1
                x += 6
                continue
        x += 1

    # valores: contorno, sangue escuro (baixo), sangue (meio), brilho (topo da letra)
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if mask[y][x]:
                up = mask[y - 1][x] if y > 0 else 0
                if not up:
                    out[y][x] = 4
                elif y > body_bottom - 4:
                    out[y][x] = 2
                else:
                    out[y][x] = 3
    # contorno de um pixel em volta
    for y in range(h):
        for x in range(w):
            if out[y][x] == 0:
                for dy, dx in ((-1, 0), (0, -1)):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < h and 0 <= xx < w and mask[yy][xx]:
                        out[y][x] = 1
                        break
    return out


def write_header(bm: list[list[int]], path: Path) -> None:
    h, w = len(bm), len(bm[0])
    lines = [
        "/* Gerado por scripts/gen_logo.py: nao edite. */",
        "#ifndef DOOM_LOGO_DATA_GUARD",
        "#define DOOM_LOGO_DATA_GUARD",
        "#include <stdint.h>",
        f"#define DOOM_LOGO_PX_W {w}",
        f"#define DOOM_LOGO_PX_H {h}",
        "static const uint8_t doom_logo_bits[DOOM_LOGO_PX_W * DOOM_LOGO_PX_H] =",
        "{",
    ]
    for row in bm:
        lines.append("    " + ",".join(str(v) for v in row) + ",")
    lines += ["};", "#endif", ""]
    path.write_text("\n".join(lines), encoding="utf-8")


def write_png(bm: list[list[int]], path: Path) -> None:
    pal = {0: (40, 40, 40), 1: (20, 0, 0), 2: (110, 0, 0), 3: (190, 10, 10), 4: (255, 70, 40)}
    h, w = len(bm), len(bm[0])
    img = Image.new("RGB", (w, h))
    for y in range(h):
        for x in range(w):
            img.putpixel((x, y), pal[bm[y][x]])
    img.resize((w * 6, h * 6), Image.NEAREST).save(path)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", type=Path)
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "port" / "doom_logo_data.h")
    args = ap.parse_args()
    bm = render()
    write_header(bm, args.out)
    if args.png:
        write_png(bm, args.png)
    print(f"{len(bm[0])}x{len(bm)} em {args.out}")


if __name__ == "__main__":
    main()
