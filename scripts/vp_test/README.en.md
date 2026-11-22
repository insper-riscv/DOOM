# Video and keyboard test of the doom platform

[Português](README.md)

## 1. Purpose

A bench program that exercises the VIDEO peripheral and the PS/2 keyboard (KBD) of the `doom` platform without the game, to tell a hardware, cable or keyboard fault from a fault of the DOOM port.

| Part | What it does |
| :--- | :--- |
| Video | Programs the 256-entry palette and draws, on the 320x200 indexed framebuffer, eight color bars, a gray ramp, a 1-pixel frame, a slowly moving square and four arrow boxes that light up while the matching key is pressed |
| Keyboard | Polls the KBD and prints on the JTAG UART, for each event, the set 2 byte sequence (make, `F0` break, `E0` extended), the key name and whether it was pressed or released |

The program uses no C library: it only accesses the registers of the VIDEO (`0x80100000`), the KBD (`0x80007000`) and the JTAG UART (`0xC0000000`), the addresses of the `platform.yaml` of the `doom` platform.

## 2. Files

| File | Purpose |
| :--- | :--- |
| `vp_test.c` | The test |
| `start.S` | Entry: sets the stack, clears the bss, calls `main` and, if it returns, parks the core in the BOOT_MEMORY loop |
| `link.ld` | Links code and data from `0x40000000`, with the stack below the last SDRAM page |
| `build.sh` | Builds and produces `out/vp_test.bin` |

## 3. Usage

```bash
docker run --rm -v "$PWD":/w -w /w ghcr.io/insper-riscv/dev_tools:latest sh scripts/vp_test/build.sh
riscv-tools --root "$INSPER_RISCV" --config src/cyclone-v/doom/config.yaml load-ram scripts/vp_test/out/vp_test.bin
```

The FLASH loader (bank A) waits for the `launch` word, runs `fence.i` and jumps to `0x40000000`. The keyboard output shows on the JTAG UART console.

---

Copyright 2026 Insper. Licensed under the [Apache License, Version 2.0](../../LICENSE).
