# DOOM on Insper-RISC-V

[Português](README.md)

A port of the original DOOM (shareware, `doom1.wad`) to the pipelined RV32IM processor of the [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V) project, on the DE0-CV board, with VGA output and PS/2 keyboard input.

## 1. What this repository contains

Only code written for this project, under Apache 2.0.

| Item | Purpose |
| :--- | :--- |
| `fetch.sh` | Downloads `doomgeneric` at a pinned commit and the shareware WAD, and checks the SHA-256 of each |
| `build.sh` | Builds the port for `rv32im` with `ilp32` in the `dev_tools` container |
| `run.sh` | Runs `-timedemo demo1` on Spike and checks the CRC-32 of the last frame |
| `port/` | Platform layer (`DG_*`), in-memory WAD backend, runtime and linker script |
| `scripts/` | Functions shared by the scripts |
| `docs/` | Build and golden-result documentation, in `pt-br` and `en` |

## 2. Quick start

Host requirements: `git`, `curl`, `sha256sum` and `docker`.

```bash
./fetch.sh && ./build.sh && ./run.sh
```

`build.sh` and `run.sh` run inside the `ghcr.io/insper-riscv/dev_tools:latest` container. The run ends with `CRC-32 confere com o gabarito: 67371a4f`. Details are in [docs/en/BUILD.md](docs/en/BUILD.md).

## 3. Licenses

The code in this repository is Apache 2.0 (`LICENSE` file). `doomgeneric` and the DOOM code are GPL-2.0 and are not kept in this repository. `fetch.sh` downloads them at build time.

The resulting binary combines the game and the platform layer, so it is GPL-2.0. Anyone distributing it must include the GPL-2.0 text and the offer of the corresponding source code. Changes to `doomgeneric`, if needed, are kept as `.patch` files under GPL-2.0.

The shareware `doom1.wad` has its own license, which allows redistributing only the original unmodified package. It is not kept in this repository either.

## 4. What is missing for the board

This version runs only on Spike, with the WAD embedded in the ELF and the console over HTIF. Stage D1 of the plan still needs:

1. Console over the JTAG UART instead of HTIF.
2. WAD in SDRAM, above the binary, loaded by `riscv-tools load-ram`.
3. Clock through `DG_GetTicksMs` using the Core `time` CSR (`doom_clock.c`).
4. The Insper-RISC-V `doom` platform, with executable `RAM` in SDRAM.

## 5. Contributing

Every change enters through a Pull Request to `main`. The general rules of the organization are in [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.en.md).
