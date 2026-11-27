# Installing DOOM on the DE0-CV

🌐 [Português](../pt-br/INSTALAR.md) · [English](INSTALL.md)

This guide takes the DE0-CV, with the microSD card prepared, to running DOOM from a Linux computer with Docker. The game is the `SDFS=fatfs` form, which reads the WADs as files of a FAT32 card. The platform is the Insper-RISC-V `doom` platform, with the core at 100 MHz, the SDRAM at 125 MHz and the FLASH as a window of the SDRAM.

## 1. What is produced

| File | How to get it | Section |
| :--- | :--- | :--- |
| `doom.bin` | `scripts/image.sh` of this repository | 3 |
| `doom_de0_cv.sof` | `riscv-tools program --compile-only` in Insper-RISC-V | 4 |
| `scripts/load_flash.py` | Already in this repository | 6 |

The `.sof` and the `doom.bin` form a pair and are not mixed with files of another platform version. The `doom.bin` of the raw card form (`SDFS=raw`) has another format and does not work here.

## 2. Preparing the computer

| Item | Requirement |
| :--- | :--- |
| Docker | Working, with the `default` context |
| Image | `ghcr.io/insper-riscv/dev_tools:latest`, which brings `uv`, Python 3.14 and the RISC-V GCC |
| Quartus Prime Lite 25.1 | Installed in `/opt/altera_lite`, with `quartus_pgm` and `jtagconfig` on the `PATH` |
| USB-Blaster | udev rule installed and the cable reconnected after it |
| Network | Access to `github.com`, because the write downloads the Tools at commit `f7ea593` |

The USB-Blaster udev rule is loaded with this command.

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The computer's Python is not used. Every Python command runs inside the container.

## 3. Building `doom.bin`

With the repository cloned, at its root:

```bash
./fetch.sh
PLATFORM_DIR=platform_doom SDFS=fatfs MENU=1 VIDEO=1 INTERACTIVE=1 SD=demand ONDEMAND=1 sh scripts/image.sh
```

| Variable | Effect |
| :--- | :--- |
| `SDFS=fatfs` | The game reads the WADs from a plain FAT32 on the card, with no `wads.bin` |
| `SD=demand ONDEMAND=1` | The WAD stays only on the card and the game reads each lump when it needs it |
| `MENU=1` | The board shows the game selection menu before DOOM |
| `VIDEO=1` | Turns on the VGA output and the indexed framebuffer |
| `INTERACTIVE=1` | Turns on the PS/2 keyboard and the full game |
| `PLATFORM_DIR=platform_doom` | Uses the platform runtime of this repository |

The image comes out as `build-board/doom.bin`. `image.sh` also prints the commands of the older load form, with `load-ram`, which this guide does not use.

## 4. Compiling the `.sof`

In the Insper-RISC-V clone, with the `doom` platform whose FLASH is a window of the SDRAM, the compile runs in the container with Quartus mounted.

```bash
docker -c default run --rm --network host \
  -v /opt/altera_lite:/opt/altera_lite:ro -v "$PWD":/w -w /w \
  -e HOME=/tmp -u "$(id -u):$(id -g)" ghcr.io/insper-riscv/dev_tools:latest sh -c \
  'export PATH="$PATH:/opt/altera_lite/25.1std/quartus/bin"; \
   riscv-tools --root . --config src/cyclone-v/doom/config.yaml program --compile-only --seed 6'
```

The `.sof` comes out as `build/doom-de0-cv/output_files/doom_de0_cv.sof`, after 7 to 10 minutes. The reports in the same folder say whether timing closed; `doom_de0_cv.sta.summary` lists the setup slack of each clock, and all of them must be positive.

## 5. Preparing the microSD card

The card is FAT32 with a cluster of 32 KB or larger, and the names follow the 8.3 format.

| On the card | Size |
| :--- | ---: |
| `DOOM.WAD` | 12,408,292 bytes |
| `DOOM2.WAD` | 14,604,584 bytes |
| `PLUTONIA.WAD` | 17,420,824 bytes |
| `TNT.WAD` | 18,195,736 bytes |
| `MASTER/` with the 20 levels and `DOOM2.MAP` | shows in the menu only together with `DOOM2.WAD` |

Formatting erases the whole card.

```bash
sudo mkfs.vfat -F 32 -s 64 -n DOOM /dev/sdX1
```

The game reads the card read-only and understands only FAT12, FAT16 and FAT32. An exFAT card shows no game in the menu. The full card guide, in Portuguese, is [CARTAO-FATFS.md](../pt-br/CARTAO-FATFS.md). Eject the card through the system before taking it to the board.

## 6. Writing the game to the board

1. Connect the power supply, the USB-Blaster cable, the VGA monitor, the PS/2 keyboard and the microSD card, and turn the board on.
2. List the cables.

```bash
jtagconfig
```

The DE0-CV shows up with IDCODE `02B050DD` (`5CE(BA4|FA4)`). Keep the cable name from that line, for example `USB-Blaster [3-6]`, because the number changes between computers and after a restart. The DE2-115 has IDCODE `020F70DD` and never receives this `.sof`.

The next three steps use the same cable name. Only one computer programs the board at a time.

3. Program the FPGA.

```bash
quartus_pgm -c "USB-Blaster [3-6]" -m jtag -o "p;build/doom-de0-cv/output_files/doom_de0_cv.sof@1"
```

4. Write `doom.bin` to the FLASH window of the SDRAM, starting at `0x1000`, through the debug port (Virtual JTAG instance 3). The write reads everything back to check it, takes about 5 minutes and ends with `flash: conferido`. At the root of this repository:

```bash
docker -c default run --rm --network host \
  -v /opt/altera_lite:/opt/altera_lite:ro -v "$PWD":/r:ro -w /tmp \
  -e HOME=/tmp -e UV_CACHE_DIR=/tmp/uvc -e RV32_SDRAM_DEBUG_INSTANCE=3 \
  -u "$(id -u):$(id -g)" ghcr.io/insper-riscv/dev_tools:latest sh -c \
  'export PATH="$PATH:/opt/altera_lite/25.1std/quartus/bin"; \
   uv run --no-project --python 3.14 \
     --with "git+https://github.com/insper-riscv/Tools.git@f7ea593" \
     python /r/scripts/load_flash.py /r/build-board/doom.bin "USB-Blaster [3-6]"'
```

| Part of the command | Effect |
| :--- | :--- |
| `--network host` | The container reaches the computer's `jtagd` |
| `-v /opt/altera_lite:...:ro` | Gives the container the `quartus_stp` that the write uses |
| `RV32_SDRAM_DEBUG_INSTANCE=3` | Points to the SDRAM debug port; without it the write does not find the port |
| `--python 3.14` | Without it `uv` picks Python 3.15 and the cocotb build fails |
| `@f7ea593` | Tools version whose debug port matches this `.sof` |

5. Program the `.sof` again, with the same command as step 3. The second programming resets the core, and the BOOT jumps to `0x1000`, where the game is already written.

The DOOM menu shows on the monitor. Pick the game with the arrows or with `W` and `S`, and confirm with `Enter`, `E` or space. The keys are in [BOARD.md](BOARD.md), section 12.2.

## 7. Checking that the board is alive

Reading one SDRAM word only answers if the FPGA is configured.

```bash
docker -c default run --rm --network host -v /opt/altera_lite:/opt/altera_lite:ro \
  -e HOME=/tmp -e RV32_SDRAM_DEBUG_INSTANCE=3 -u "$(id -u):$(id -g)" \
  ghcr.io/insper-riscv/dev_tools:latest sh -c \
  'export PATH="$PATH:/opt/altera_lite/25.1std/quartus/bin"; \
   quartus_stp -t /opt/riscv-tools/riscv-tools/lib/python3.14/site-packages/riscv_tools/jtag/tcl/sdram_dbg.tcl \
   "USB-Blaster [3-6]" "@1: 5CE(BA4|FA4) (0x02B050DD)" read 15860736 1'
```

If the command fails, the FPGA lost its configuration and steps 3 to 5 of section 6 are repeated. The configuration disappears at every power-off, and the SDRAM with it.

## 8. If something does not work

| Symptom | Likely cause | What to do |
| :--- | :--- | :--- |
| `jtagconfig` does not list the board | udev rule missing or loose cable | Section 2 and reconnect the cable |
| `load-flash: this bitstream has no hold and CRC` | The write was made with `riscv-tools load-flash`, which asks for an `INFO` word that the debug port of this `.sof` does not have | Write with `scripts/load_flash.py`, as in section 6 |
| Error `cocotb ... maximum Python version of 3.14` | `uv` picked Python 3.15 | Keep `--python 3.14` |
| `the SDRAM is not initialized` | Wrong Virtual JTAG instance | Export `RV32_SDRAM_DEBUG_INSTANCE=3` |
| Black screen after step 5 | Image not written or a check error | Repeat step 4 and read the check message |
| Menu with no game | The card is not FAT32 or was not detected | Section 5 |
| Menu missing a game | Name outside 8.3 or `MASTER/` in the wrong place | Section 5 |
| Game hangs right after the menu | `.sof` and `doom.bin` from different versions | Redo sections 3 and 4 from the same commit |

## 9. Precautions

1. Before using the board, take its lock, so that no other process programs it in the middle of the steps. The lock is one file per board, such as `/tmp/de0cv-<machine>.lock`.

```bash
exec 9>/tmp/de0cv-laptop.lock && flock -w 60 9
```

2. Do not run someone else's `quartus_pgm` or `quartus_stp` at the same time.
3. A service that reprograms the FPGA by itself must be stopped, or have the cable disconnected from it, before the cable is used on another computer.
