"""Gera as imagens da simulacao GHDL do binario do DOOM na plataforma doom.

Roda da raiz do Insper-RISC-V (com PYTHONPATH=. e o python do riscv-tools, no container
dev_tools). Reaproveita `tests/python/gen_doom_images.py`: a BOOT_MEMORY e a banca A
(carregador) sao as de verdade, so a campainha `launch` muda de lugar, porque a imagem
da SDRAM do simulador e linear a partir da base e a campainha da placa (0x43FFFFF8) fica
64 MiB acima dela. A campainha do simulador fica em RAM_BASE + --launch-offset, dentro
do bss do jogo (abaixo da WAD), que o crt0 zera depois de o carregador consumi-la.

Saidas em OUT: boot_memory.hex, flash.hex e sdram.hex (binario do jogo, magia na
campainha e, com --wad, a WAD em RAM_BASE + --wad-offset).
"""

import argparse
from pathlib import Path

from tests.python import gen_doom_images as images
from tests.python.utils.rv32_asm import hex_lines


def main() -> None:
    """Escreve as tres imagens."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="doom.bin da placa")
    parser.add_argument("out", type=Path, help="pasta das imagens")
    parser.add_argument("--launch-offset", type=lambda s: int(s, 0), default=0x80000)
    parser.add_argument("--wad", type=Path, default=None)
    parser.add_argument("--wad-offset", type=lambda s: int(s, 0), default=0x100000)
    args = parser.parse_args()

    images.LAUNCH_OFFSET = args.launch_offset
    texts = images.build()
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "boot_memory.hex").write_text(texts["boot_memory.hex"])
    (args.out / "flash.hex").write_text(texts["flash.hex"])

    program = images.words_of(args.binary.read_bytes())
    launch_word = args.launch_offset // images.WORD
    if len(program) > launch_word:
        raise SystemExit("o binario passa da campainha: aumente --launch-offset")
    words = [0] * (launch_word + 1)
    words[: len(program)] = program
    words[launch_word] = images.LAUNCH_MAGIC
    if args.wad is not None:
        wad = images.words_of(args.wad.read_bytes())
        first = args.wad_offset // images.WORD
        words += [0] * (first - len(words)) + wad
    (args.out / "sdram.hex").write_text(hex_lines(words))
    print(f"imagens em {args.out}: {len(words)} palavras na SDRAM")


if __name__ == "__main__":
    main()
