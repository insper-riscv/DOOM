# DOOM on Insper-RISC-V

[Português](README.md)

A port of the original DOOM (shareware, `doom1.wad`) to the pipelined RV32IM processor of the [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V) project, on the DE0-CV board, with VGA output and PS/2 keyboard input.

## 1. What this repository contains

Only code written for this project, under Apache 2.0.

| Item | Purpose |
| :--- | :--- |
| `fetch.sh` | Downloads `doomgeneric` at a pinned commit and the shareware WAD, and checks the SHA-256 of each |
| `build.sh` | Builds the port for `rv32im` with `ilp32` in the `dev_tools` container, for the `spike`, `layout` and `board` targets |
| `run.sh` | Runs `-timedemo demo1` on Spike and checks the CRC-32 of the last frame |
| `port/` | Platform layer (`DG_*`), in-memory WAD backend, runtime and linker script |
| `scripts/` | Shared functions, the SDRAM map (`layout.sh`), the board image (`image.sh`), the result check (`check_result.py`) and the GHDL simulation (`sim/`) |
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

## 4. The board

The `board` target of `build.sh` produces the binary for the Insper-RISC-V `doom` platform. The console goes out over the JTAG UART, the clock comes from the platform `doom_clock.c`, and the WAD is loaded into SDRAM by `riscv-tools load-ram`. `VIDEO=1` turns on the VGA output and `INTERACTIVE=1` turns on the PS/2 keyboard and the full game; without them, the `-timedemo` leaves its result in a fixed SDRAM block that `dump-ram` reads. The SDRAM map, the commands and the comparison with Spike are in [docs/en/BOARD.md](docs/en/BOARD.md).

```bash
./fetch.sh && ./scripts/image.sh
```

## 5. Contributing

Every change enters through a Pull Request to `main`. The general rules of the organization are in [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.en.md).
