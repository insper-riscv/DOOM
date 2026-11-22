# Indexed video measurement on the DE0-CV

## 1. What changes in the cost of a frame

On `main` (the `spike` target and `layout` without `VIDEO`), doomgeneric converts each 320x200 frame of palette indices to 32-bit XRGB with `cmap_to_fb`, which accounts for 60% of the instructions of `-timedemo demo1`. With `VIDEO=1` the build defines `CMAP256`, the conversion is gone, and the index frame goes straight to the `VIDEO` peripheral window (a 256-entry RGB444 palette written only when DOOM changes it, a write-only framebuffer). The code is PR #4 and the per-window measurement is PR #3; this page only carries the numbers.

## 2. Spike reference

`TARGET=layout VIDEO=1` build run on Spike (`rv32im_zicsr_zicntr`), rebuilt from the PR #4 commit.

| Item | XRGB (no `VIDEO`) | Indexed (`VIDEO=1`) |
| :--- | ---: | ---: |
| CRC-32 of the last frame | `67371a4f` | `7b28220e` |
| Timedemo frames | 5065 | 5065 |
| Instructions | 15.94 G | 8.5729 G |
| Instructions per frame | 3,147,534 | 1,682,257 |

CRC-32 `67371a4f` holds only for the XRGB format and is superseded for the `VIDEO=1` build, whose reference becomes `7b28220e` (64,000 indices, no palette).

## 3. Result on the board

Full timedemo on the DE0-CV, `doom` platform, 76.923 MHz clock, `TARGET=board VIDEO=1 MEASURE=1` build (no vsync wait and no CRC, as the CPI measurement requires), with `cycle`, `instret` and the Zihpm counters read every 100 frames.

| Item | Board |
| :--- | ---: |
| CRC-32 of the last frame | `7b28220e`, equal to Spike |
| Timedemo frames | 5057 (Spike does 5065 and the earlier measurement 5041; the opening depends on the clock) |

## 4. Before and after

Frame windows 0 to 5000 of the same timedemo and clock. The baseline is the T10 measurement (XRGB, CRC-32 `67371a4f`).

| Window | Metric | Baseline (XRGB) | Indexed | Change |
| :--- | :--- | ---: | ---: | ---: |
| 0 to 5000 | Instructions per frame | 3,156,418 | 1,713,176 | -45.7% |
| 0 to 5000 | Cycles per frame | 9,570,668 | 7,154,461 | -25.2% |
| 0 to 5000 | CPI | 3.0321 | 4.1761 | +37.7% |
| 0 to 5000 | Average fps | 8.04 | 10.75 | +33.7% |
| 0 to 5000 | Cycles waiting on external RAM | 53.3% | 58.5% | +5.2 points |
| 0 to 100 | Instructions per frame | 3,323,669 | 1,716,598 | -48.4% |
| 0 to 100 | CPI | 3.1142 | 4.2799 | +37.4% |
| 0 to 100 | fps | 7.43 | 10.47 | +40.9% |
| 100 to 5000 | Instructions per frame | 3,153,005 | 1,713,106 | -45.7% |
| 100 to 5000 | CPI | 3.0304 | 4.1740 | +37.7% |
| 100 to 5000 | fps | 8.05 | 10.76 | +33.7% |

The RAM wait is the sum, window by window, of the external-RAM wait counter over the cycles (the 32-bit counter wraps over long windows, so each 100-frame window is summed separately).

## 5. Reading

| T11 study prediction | Measured |
| :--- | :--- |
| About 1.32 M instructions per frame | 1.71 M (Spike confirms 1.68 M, so the prediction was short, not the board) |
| About 19 fps by instruction ratio | 10.75 fps |

The game lost 46% of its instructions, but CPI rose from 3.03 to 4.18: what remains is rendering and game logic, which the wait counter shows more tied to SDRAM (58.5% of cycles against 53.3%). The time gain is therefore 25% of the cycles per frame, not 46%, and SDRAM already occupies 58% of the cycles. Lowering CPI takes fewer SDRAM accesses, not fewer instructions.

## 6. How it was measured

The board measurement was taken on 2026-11-18 with the `build-board-vm` binary (a `board` build with `VIDEO=1 MEASURE=1`, from PR #4 plus the 5 s report and the idle restart, both inactive in the timedemo), read with `scripts/measure_report.py` over the `dump-ram` of the block at `0x43000000`. The Spike reference was rebuilt and rerun for this document.
