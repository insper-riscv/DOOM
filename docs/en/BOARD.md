# DOOM on the board (stage D1)

🌐 [Português](../pt-br/PLACA.md) · [English](BOARD.md)

Stage D1 runs the same `-timedemo demo1` as Spike on the `doom` platform of [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V), on the DE0-CV, with no screen. The game runs from SDRAM, the console goes out over the JTAG UART, and the last frame is read back with `dump-ram` and compared with the Spike CRC-32 `67371a4f`.

## 1. Build targets

| Target | Console | Clock | WAD | Use |
| :--- | :--- | :--- | :--- | :--- |
| `spike` (default) | HTIF | Virtual, from `instret` | Embedded in the ELF | Stage D0 golden |
| `layout` | HTIF | Virtual, from `instret` | In the ELF, at `WAD_ADDR` | Validates the board code and memory map on Spike |
| `board` | JTAG UART | The platform `doom_clock.c` | Outside the binary, through `load-ram --wad` | The board binary |

The `layout` and `board` targets compile the same port code with the same address definitions, and differ only in the console, the clock and where the WAD enters. The `board` target copies, at build time, the platform `doom_clock.c` (`Insper-RISC-V/src/cyclone-v/doom/runtime/`, fetched by `fetch.sh` at a pinned commit) and uses its `memory_map.h` for the UART address. Nothing of the platform is duplicated in this repository. Another copy of the platform works through `PLATFORM_DIR`, as long as it is inside the project folder, which the container mounts.

## 2. SDRAM map

Every address comes from `scripts/layout.sh`, which `build.sh`, `run.sh` and `image.sh` read. The SDRAM starts at `0x40000000` and is 64 MiB.

| Region | Address | Size | Content |
| :--- | :--- | ---: | :--- |
| Image | `0x40000000` | 404,660 B of binary, about 650 KiB with the bss | Code, constants and initial data, and the bss right after |
| WAD | `0x40100000` | 4,196,020 B | `doom1.wad`, loaded by `load-ram --wad --wad-address 0x40100000` |
| Framebuffer | `0x40580000` | 256,000 B | 320x200 32-bit XRGB, fixed for `dump-ram` |
| Heap | `0x405C0000` | up to the stack | The game zone and `malloc` |
| Stack | `0x43FEF000` to `0x43FFF000` | 64 KiB | Free top of the SDRAM |
| Result | `0x43FFF000` | 64 B | Result block (section 4) |
| Platform words | `0x43FFFBE0` to `0x43FFFFFF` | | `stdout`, `tohost`, `fromhost` and `launch`, from `platform.yaml` |

The WAD sits at `0x40100000` because the image with its bss ends before `0x40100000` (the linker checks it) and because the address matches the example in the platform documentation. The framebuffer and the heap sit above the end of the WAD (`0x404001B4`), with room for a WAD up to 4.5 MiB. The last SDRAM page (`0x43FFF000`) is the one the data cache ignores, so the result block and the doorbell always reflect the real memory.

The game does not use the platform `stdout` buffer (1 KiB, linear, which would truncate the long output of the game). The console is the UART.

## 3. Console and clock

The UART is the platform JTAG UART (window `0xC0000000`, `RV32_UART_BASE`), with the protocol of the platform `runtime/stdio.c`. The status register is at `+8`, and the program only sends after a host has scanned the UART (bit 17), waiting for queue room for at most 200,000 reads. The console must therefore be open (`riscv-tools console`) for text to appear, and the result itself does not depend on it. The game prints progress every 100 frames and, at the end, repeats the summary every 2 s.

`DG_GetTicksMs` is the one in the platform `doom_clock.c`, from the `time` CSR, at 1,000 ticks per millisecond. The `-timedemo` result does not depend on it, because the demo runs without wall-clock time.

## 4. Result block

At the end of the `-timedemo` the game writes 11 32-bit words starting at `0x43FFF000`, the magic last. It clears the magic at start-up, so an old result never counts.

| Word | Field | Meaning |
| ---: | :--- | :--- |
| 0 | `magic` | `0x53455244` (bytes `DRES`) when the block is complete |
| 1 | `exit` | Game exit code (`-1` at the end of the `-timedemo`) |
| 2 | `crc32` | CRC-32 (IEEE) of the framebuffer, golden `67371a4f` |
| 3 | `frames` | Frames delivered (5065 on Spike) |
| 4 and 5 | `instret` | Retired instructions, low and high part |
| 6 | `per_frame` | Instructions per frame (3,147,534 on Spike) |
| 7 | `fb_addr` | Framebuffer address |
| 8 | `ms` | Milliseconds of `DG_GetTicksMs` at the end |
| 9 and 10 | `cycle` | Cycles, low and high part (with `instret`, gives the CPI) |

## 5. Preparing the board

These steps belong to the platform and are in [`docs/en/platform/doom.md`](https://github.com/insper-riscv/Insper-RISC-V/blob/main/docs/en/platform/doom.md) of Insper-RISC-V. Summary.

1. Build and program the `doom` platform `.sof` on the DE0-CV (`riscv-tools board` of Insper-RISC-V).
2. Write the loader `runtime/doom_loader.S` to bank A of the FLASH (section 4.1 of that document). It waits for the magic `0x4D4F4F44` in `launch` (`0x43FFFFF8`) and jumps to `0x40000000`.

## 6. Commands

`scripts/image.sh` builds the `board` target, checks that the binary fits before the WAD, and prints the commands with the values of this map. The full run is as follows, with `INSPER_RISC_V` pointing to an Insper-RISC-V clone.

```bash
./fetch.sh
./scripts/image.sh
RT="riscv-tools --root $INSPER_RISC_V --config src/cyclone-v/doom/config.yaml"

$RT load-ram build-board/doom.bin --wad doom1.wad --wad-address 0x40100000
$RT console
$RT dump-ram build-board/result.mif --start-word 16776192 --words 16
$RT dump-ram build-board/fb.mif --start-word 1441792 --words 64000
uv run --no-project scripts/check_result.py build-board/result.mif --frame build-board/fb.mif --png build-board/frame.png
```

`load-ram` clears `launch`, writes the binary and the WAD through the SDRAM debug port, reads each back, and only then writes the magic. `console` and `dump-ram` share the same JTAG cable and do not run at the same time. Leave the console open until the line `DOOM-D0: exit=-1 frames=... frame_crc32=...` appears, stop it with Ctrl-C, and read the dumps. `check_result.py` checks the magic and the block `crc32` against Spike and, with the framebuffer, recomputes the CRC-32 of the bytes read and writes the PNG of the last frame.

The `--start-word` of `dump-ram` is the distance to the SDRAM base in words, `(address - 0x40000000) / 4`, in decimal: 16776192 for the result and 1441792 for the framebuffer, which has 64,000 words. Reading the framebuffer over JTAG takes a few minutes.

## 7. What was verified without the board

| Check | Result |
| :--- | :--- |
| `spike` target | CRC-32 `67371a4f`, 5065 frames, equal to the golden |
| `layout` target on Spike | Same code and board map (WAD at `0x40100000`, framebuffer at `0x40580000`, heap at `0x405C0000`, stack up to `0x43FFF000`): CRC-32 `67371a4f`, 5065 frames |
| `board` target | Builds with `-Wall -Wextra -Werror` in `dev_tools` (GCC 16.1, picolibc), 404,660 bytes, no `div` in the clock |
| GHDL simulation of the `doom` platform | See section 8 |

## 8. GHDL simulation

The `doom_sim_test` top runs the `board` binary with the real BOOT_MEMORY and the real bank A loader. The `scripts/sim/doom_image_run.py` test reads the JTAG UART like the board console and is independent of the C program testbench of Insper-RISC-V.

| Step | Command, at the Insper-RISC-V root, in the `dev_tools` container |
| :--- | :--- |
| Images (BOOT_MEMORY, FLASH with the loader, SDRAM with the binary and the doorbell at `0x40080000`) | `PYTHONPATH=. uv run --no-project --python /opt/riscv-tools/riscv-tools/bin/python ../DOOM/scripts/sim/gen_sim_images.py ../DOOM/build-board/doom.bin ../sim/nowad` |
| Test | Copy `doom_image_run.py` to `tests/python/unittests/rtl/`, create the `DoomImageRun` entry in `tests.json` (`cases` equal to `tests.python.unittests.rtl.doom_image_run:cases`, the three files above as `BOOT_MEMORY_FILE`, `ROM_FILE` and `SDRAM_FILE`) and run `uv run python tests/python/runner.py DoomImageRun`, with `DOOM_RUN_CYCLES` and `DOOM_EXPECT` in the environment |

Result obtained, with the Insper-RISC-V submodules at `main` and `doom_sim_top.vhd` ported to the new format (see section 9):

| Measure | Value |
| :--- | :--- |
| Speed | About 24,000 base-clock cycles per second, or 8,000 core cycles (the core runs at a third of the base clock) |
| Loader | The BOOT_MEMORY checks bank A, the loader consumes the doorbell (word cleared) and jumps to `0x40000000` |
| First image instruction | `auipc sp` sets `sp` to `0x43FFF000`, the stack top of the map |
| `.data` copy and bss clear | The picolibc crt0 `memset` is a byte loop, with one byte store per iteration out to SDRAM, and takes over 8 million core cycles (over 5 minutes of GHDL), with the `pc` still in it at both sample points (4 and 8 million cycles) |
| UART text | None in that window: the game banner comes after the bss clear, out of practical reach of GHDL |

The simulation thus proves the chain BOOT_MEMORY, loader, jump and start of the real binary image, but reaches neither the console nor the end of the `-timedemo`, which are board proofs. On the board the bss clear takes about 0.1 s.

## 9. Proposals for Insper-RISC-V

1. Reserve the result block in `platform.yaml` (`reserved.result: {base: 0x43FFF000, size: 64}`) and generate it in `memory_map.h` (`RV32_DOOM_RESULT_ADDR`), instead of a fixed address in `scripts/layout.sh`.
2. Bring `doom_sim_top.vhd` to the new configuration format (the one in PR #152 still uses `ROM_EXTERNAL`, `RAM_EXTERNAL` and `PERIPH_SLOTS`, which the generated package no longer has) and update the `sim.vhdl_sources` list of the platform and of `DoomSimTop` (`Peripherals/src/GPIO/soc/gpio_pkg.vhd` is missing). The submodule pointers of `main` are also behind the submodules (the `main` core of Insper-RISC-V has no `i_ext_error`, which the simulation top uses).
3. Optional, for the D4 measurement, a command that waits for the result magic (`wait-ram`), so the console need not stay open during the run.

## 10. Risks of the first run

| # | Risk | Effect | How to recognise it |
| :--- | :--- | :--- | :--- |
| 1 | Duration: about 16 billion instructions | At a CPI between 2 and 4 at 77 MHz, 7 to 14 minutes or more | Progress every 100 frames on the console |
| 2 | Frame count different from Spike | The opening screen wipe depends on the clock, so `frames` may differ from 5065 without changing the CRC-32 of the last frame | Compare only `crc32` |
| 3 | Corrupted image in SDRAM or on read | Different CRC-32 | `load-ram` checks the read-back; a different CRC with a correct read-back points to a core bug |
| 4 | Console never scanned | No text on screen | The result block still holds |
| 5 | Instruction cache | The loader runs `fence.i` before the jump | A hang right at the start, with no text |

## 11. CPI measurement by frame windows

With `MEASURE=1` at build time (`layout` and `board` targets), `DG_DrawFrame` stores, every 100 delivered frames, a snapshot of `cycle`, `instret` and, on the board, the nine Zihpm event counters (indexes 3 to 11: mul/div stall, external RAM wait, taken branch, misaligned access, instruction cache miss, data cache miss, FLASH wait, load-use stall and fused operation) in the block of `scripts/layout.sh` (`MEASURE_ADDR`, 0x43FFF100, 64 B header and 40 snapshots of 64 B). `MEASURE_STOP=N` ends the game when frame N is delivered. A build without `MEASURE` is identical to the previous one.

```bash
MEASURE=1 MEASURE_STOP=400 sh scripts/image.sh
# after load-ram and the run:
riscv-tools --root "$INSPER_RISCV" --config src/cyclone-v/doom/config.yaml dump-ram build-board/meas.mif --start-word 16776256 --words 656
uv run --no-project scripts/measure_report.py build-board/meas.mif
```

On Spike (`TARGET=layout MEASURE=1`) the CRC-32 of the last frame stays `67371a4f` and the instructions per frame stay 3,147,534.
