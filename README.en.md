# DOOM on Insper-RISC-V

[Português](README.md)

A port of the original DOOM (shareware, `doom1.wad`) to the pipelined RV32IM processor of the [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V) project, on the DE0-CV board, with VGA output and PS/2 keyboard input.

## 1. What this repository contains

Only code written for this project, under Apache 2.0.

| Item | Purpose |
| :--- | :--- |
| `fetch.sh` | Downloads `doomgeneric` at a pinned commit and the shareware WAD, and checks the SHA-256 of each |
| Platform layer | Video, keyboard, clock and in-memory WAD access, in place of an operating system |
| Linker and runtime | Memory map of the `doom` platform and start-up code |
| Build scripts | Compilation with the toolchain of the `dev_tools` container |

## 2. Licenses

The code in this repository is Apache 2.0 (`LICENSE` file). `doomgeneric` and the DOOM code are GPL-2.0 and are not kept in this repository. `fetch.sh` downloads them at build time.

The resulting binary combines the game and the platform layer, so it is GPL-2.0. Anyone distributing it must include the GPL-2.0 text and the offer of the corresponding source code. Changes to `doomgeneric`, if needed, are kept as `.patch` files under GPL-2.0.

The shareware `doom1.wad` has its own license, which allows redistributing only the original unmodified package. It is not kept in this repository either.

## 3. Contributing

Every change enters through a Pull Request to `main`. The general rules of the organization are in [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.en.md).
