# DOOM benchmark on the DE0-CV

This document defines how DOOM performance is measured on the board and records the results of the current state: fps, CPI and frequency. The numbers were measured on the DE0-CV with the SoC of the Insper-RISC-V `doom` platform, reading the WADs from the SD card.

## 1. Frequency and relation between the quantities

The RISC-V core runs at **100 MHz**, and instruction fetch (the instruction cache) runs on the same clock and phase. The external SDRAM and its controller run at **125 MHz** in a separate domain. Timing closed on the `.sof` used for the measurements:

| Domain | Frequency | Setup slack (worst corner) |
| :--- | ---: | ---: |
| Core and fetch | 100 MHz | +0.090 ns |
| Memory (SDRAM) | 125 MHz | +0.806 ns |

Hold is positive in every temperature and voltage corner. The quantities are related by

$$\text{fps} = \frac{f}{\text{CPI} \times I_f}$$

where $f = 100$ MHz, $I_f$ is the number of instructions per frame and CPI is the average number of cycles per instruction. CPI comes from the `mcycle` and `minstret` counters read every 100 frames.

## 2. What is measured

| Measurement | How it is obtained | Purpose |
| :--- | :--- | :--- |
| fps of a 100-frame window | Cycles between two counter snapshots (every 100 frames) | Average fps and CPI of the window |
| Cycles of each frame | `mcycle` difference between two `DG_DrawFrame` calls | Slowest frame and short windows |
| Slowest frame, worst 5-, 10- and 25-frame window | Computed over the cycles of each frame | Fast fps drops (what the player feels as lag) |
| Frames below 35 fps | Count over frames 101 to the last | Fraction of time below the original DOOM rate |
| CRC-32 of the last frame and frame count | Result block in the SDRAM | Checks the image is the same as in Spike |

The measured fps is the `-timedemo` one: the game runs the demo without waiting for the clock, so the value is what the hardware can do and not what the screen shows. In normal play the engine caps at 35 frames per second.

The measurements skip the first 100 frames, which include the initial WAD read from the card and run at 12 to 17 fps. The FPS HUD is off during measurements, because drawing in the frame changes the counted instructions.

## 3. How to run

`scripts/benchmark/bench.sh` builds the measurement image of each demo (the chosen game runs the demo and stops at frame 330), zeroes the measurement blocks, writes the image to the board, waits for the end and prints the report. It reprograms the FPGA and writes to the board SDRAM.

| Variable | Meaning |
| :--- | :--- |
| `INSPER_RISCV` | Root of an Insper-RISC-V clone |
| `TOOLS` | Tools clone (runs `riscv-tools` with `uv run --project`) |
| `SOF` | `.sof` of the DOOM SoC at 100 MHz |
| `LOAD_FLASH` | The platform `load_flash_nohold.py` |
| `WAD_DIR` and `WAD_SET` | WADs and menu games, equal to those on the card in the board |
| `SDFS` | `raw` (default) or `fatfs`, equal to the card in the board |
| `DEMOS` | List `game:demo:name:hud` (default: the measurements of section 4) |
| `CABLE` | JTAG cable, if there is more than one board |

```bash
INSPER_RISCV=~/Insper-RISC-V TOOLS=~/Tools SOF=doom_100MHz.sof \
LOAD_FLASH=~/Insper-RISC-V/load_flash_nohold.py WAD_DIR=~/doom/base \
  sh scripts/benchmark/bench.sh
```

Each measurement takes about 15 minutes (the first-frame load from the card takes about 40 seconds). Dumps and reports go to `build-bench/resultado/`. `scripts/benchmark/analyze.py` also runs on its own over the dumps. The SDRAM addresses are those of `scripts/layout.sh` and `port/doom_platform.h`: measurement at `0x43780000`, per-frame cycles 8 KiB after it, result at `0x43BFF000`.

## 4. Results of the current state

Measured at 100 MHz, with the raw SD card, the HUD off and the final image (`-O3 -funroll-loops -flto`). The "window" columns are the fps of the 100-frame windows, and the others use the cycles of each frame, from 101 to 329.

### 4.1. fps and CPI per 100-frame window

| Game and demo | fps (101 to 200) | fps (201 to 300) | CPI (101 to 200) | CPI (201 to 300) |
| :--- | ---: | ---: | ---: | ---: |
| Ultimate DOOM `demo1` | 42.2 | 46.4 | 2.61 | 2.67 |
| DOOM II `demo1` | 40.8 | 39.7 | 2.90 | 2.86 |
| DOOM II `demo2` | 39.2 | 38.3 | 2.73 | 2.75 |
| Plutonia `demo1` | 39.2 | 33.7 | 2.83 | 2.79 |
| TNT `demo1` | 38.4 | 39.4 | 2.77 | 2.81 |
| DOOM II `demo3` | no data (empty block) | no data | | |

The CPI of DOOM II `demo3` was not read (the snapshot block came back empty in that run). From Spike, `demo3` executes 1,053,737 instructions per frame, which with the 3.15 million cycles per frame of the board gives a CPI of about 2.98.

### 4.2. fps per frame

| Game and demo | Average | Slowest frame | Worst 25-frame window | Frames below 35 fps |
| :--- | ---: | ---: | ---: | ---: |
| DOOM II `demo2` | 38.5 | 31.5 | 36.3 | 15 of 229 (7 %) |
| DOOM II `demo2`, FPS HUD on | 38.4 | 31.5 | 36.3 | 16 of 229 (7 %) |
| TNT `demo1` | 39.3 | 21.1 | 30.9 | 58 of 229 (25 %) |
| Plutonia `demo1` | 36.1 | 19.2 | 31.4 | 122 of 229 (53 %) |
| DOOM II `demo3` | 31.8 | 24.8 | 29.3 | 194 of 229 (85 %) |

Ultimate DOOM `demo1` and DOOM II `demo1` were measured by window only, before the per-frame measurement existed.

### 4.3. Reading the results

1. In light scenes (Ultimate DOOM, DOOM II `demo1`) the game exceeds 35 fps with margin at 100 MHz.
2. CPI is between 2.6 and 3.0, and the wait for the external SDRAM is most of the cycles. The core frequency (100 MHz) sets the ceiling, and CPI and instructions per frame decide how much of the ceiling is used.
3. In heavy scenes (DOOM II `demo3`, Plutonia) there are stretches below 35 fps, with short drops down to 19 to 25 fps. `demo3` draws more wall columns per frame, which is the most expensive loop of the renderer.
4. The FPS counter drawn straight into the video costs about 0.1 % of the cycles.
5. The Spike numbers for instructions per frame and the CRC of the last frame match those of the board, which confirms the image is the original.

The measurements used the 100 MHz `.sof` files of the `doom` platform (the one with +0.514 ns core slack and the final one with +0.090 ns, which differs only in the video borders), and the core RTL is the same.
