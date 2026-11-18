# DOOM cache study over Spike traces

[Português](README.md)

## 1. Purpose

Two C tools to estimate, without the board, the effect of cache and SDRAM wait configurations on DOOM running on the `doom` platform. Spike runs `-timedemo demo1` and `filter` reduces the `--log-commits` output to a compact binary trace; `sim` replays that trace over a cache and SDRAM model and computes the CPI and frames per second of each configuration.

| Tool | Input | Output |
| :--- | :--- | :--- |
| `filter.c` | Spike `--log-commits` output (standard input) | One binary trace per frame window (fetch, load and store, one `uint32` per record) and, on `stderr`, the instruction class counts per window |
| `sim.c` | The `filter` traces and a set of `name:key=value:...` configurations | A TSV table with CPI, frames per second, fills, write-backs and M10K blocks of each configuration |

## 2. Limits of the model

| Item | Value |
| :--- | :--- |
| SDRAM regions | The limits of `region_of` and of the palette copy are fixed in the code for the `doom.elf` of the `layout` target of one specific build; another build needs those limits checked with `nm` |
| Model | Each configuration has an instruction and a data cache (line, LRU associativity, write policy), cacheable regions, a fixed internal memory and an optional write buffer; time counts in core cycles and the SDRAM is a single in-order resource |
| Data | The traces (several GB) and the result tables of the original study are not part of the repository |

## 3. Usage

```bash
gcc -O2 -Wall -o filter scripts/cache_estudo/filter.c
gcc -O2 -Wall -Wno-unused -o sim scripts/cache_estudo/sim.c -lm
DPC=$(riscv32-unknown-elf-nm build-layout/doom.elf | grep " T DG_DrawFrame$" | cut -d' ' -f1)
spike --disable-dtb --isa=rv32im_zicsr_zicntr -m0x40000000:0x04000000 --log-commits build-layout/doom.elf 2>&1 >/dev/null \
  | ./filter "$DPC" 98:400:full.bin 2> extract.log
./sim -c "base" full.bin
```

The `98:400` window counts frames by the entries of `DG_DrawFrame`, like the event counter of the board. The configuration keys are in `parse_cfg` of `sim.c`.

---

Copyright 2026 Insper. Licensed under the [Apache License, Version 2.0](../../LICENSE).
