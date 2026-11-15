# Build and run on Spike

🌐 [Português](../pt-br/BUILD.md) · [English](BUILD.md)

Port of `doomgeneric` (GPL-2.0) to `-march=rv32im -mabi=ilp32`, with no A, no C and no floating-point unit, running on Spike with the v1.9 shareware WAD embedded.

## 1. Getting the sources and the WAD

```bash
./fetch.sh
```

The script clones `github.com/ozkl/doomgeneric` into `doomgeneric/` at commit `e257a09527062bfb986b6e5e0780839b6cbd5b71` and downloads the shareware `doom1.wad` from `github.com/Doom-Utils/shareware-collection` (folder `Doom 1.9`) to `doom1.wad`. It then checks the WAD hashes.

| Item | Value |
| :--- | :--- |
| Size | 4196020 bytes |
| SHA-256 | `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771` |
| SHA-1 | `5b2e249b9c5133ec987b3ea77596381dc0d6bc1d` |

The WAD and doomgeneric are not versioned. The v1.8 WAD from `libsdl.org` does not work, because `demo1` is from version 1.9.

## 2. Building

```bash
./build.sh
```

The script runs in the `ghcr.io/insper-riscv/dev_tools:latest` container and produces `build/doom.elf`.

| Variable | Default | Meaning |
| :--- | :--- | :--- |
| `OPT` | `-O2` | Optimization level |
| `TARGET` | `spike` | `spike`, `layout` or `board` (see [BOARD.md](BOARD.md)) |
| `PLATFORM_DIR` | `Insper-RISC-V/src/cyclone-v/doom` | Folder of the `doom` platform, `board` target only |
| `OUT` | `build` (`build-$TARGET` for the other targets) | Output folder |
| `RAM_ORIGIN` | `0x40000000` | RAM origin |
| `RAM_LENGTH` | `0x04000000` | RAM length (64 MiB) |
| `DEV_TOOLS_IMAGE` | `ghcr.io/insper-riscv/dev_tools:latest` | Container image |

To run already inside a `dev_tools` container with the project folder at `/w`, set `DOOM_IN_CONTAINER=1`.

## 3. Running on Spike

```bash
./run.sh
```

The run takes about 30 seconds, executes `doom -iwad doom1.wad -timedemo demo1`, writes the output to `build/spike.out` and `build/spike.err`, and fails if the CRC-32 of the last frame differs from the golden value.

## 4. Reference result (D1 golden)

| Measure | Value |
| :--- | :--- |
| Termination | `-timedemo` prints `timed 5026 gametics in 5582 realtics` and exits |
| Frames delivered to `DG_DrawFrame` | 5065 |
| Retired instructions | 15994539968 |
| Instructions per frame (average between first and last) | 3147534 |
| CRC-32 of the last frame (320x200 32-bit XRGB, bytes in memory order) | `67371a4f` |
| Reproducibility | Two runs produce identical output |

The virtual clock counts 100000 instructions per millisecond (100 MHz at 1 instruction per cycle), so the `fps` printed by the game (31.5) holds only for that model.

## 5. Binary format and addresses

| Item | Value |
| :--- | :--- |
| Format | RISC-V ELF32, loaded by Spike |
| Single RAM (code, constants, WAD, data, bss, heap and stack) | origin `RAM_ORIGIN`, length `RAM_LENGTH` (`0x40000000` and 64 MiB) |
| `.text` / `.rodata` / `.wad` / `.data` / `.bss` | 241648 / 102952 / 4196020 / 59796 / 244056 bytes |
| Stack | 64 KiB at the top of RAM |
| Origin and length | symbols `RAM_ORIGIN` and `RAM_LENGTH` in `port/link.ld`, defined only there |

## 6. Port layout

| File | Role |
| :--- | :--- |
| `port/doom_platform.c` | The `DG_*` functions, the virtual clock, frame counting and the CRC-32 (single framebuffer conversion point) |
| `port/doom_wad_mem.c` | In-memory WAD backend using `memcpy`, with no `open`, `read` or `lseek` |
| `port/doom_runtime.c` | Console output over the Spike HTIF or the JTAG UART (`DOOM_CONSOLE_UART`), filesystem stubs, `_exit` with the summary and the result block, and `main` |
| `port/link.ld` | Single-RAM linker script, with the WAD and the heap at fixed addresses in the `layout` and `board` targets |
| `scripts/layout.sh` | Single source of the board SDRAM map |
