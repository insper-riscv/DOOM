# DOOM on Insper-RISC-V

[Português](README.md)

A port of the original DOOM (shareware, `doom1.wad`) to the pipelined RV32IM processor of the [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V) project, on the DE0-CV board, with VGA output and PS/2 keyboard input.

## 1. What this repository contains

Only code written for this project, under Apache 2.0.

| Item | Purpose |
| :--- | :--- |
| `fetch.sh` | Downloads `doomgeneric` at a pinned commit and the shareware WAD, and checks the SHA-256 of each; with `--wad-dir FOLDER` it uses your own WADs in place of the shareware |
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

## 3. Choosing the games that go into the SDRAM

With the menu (`MENU=1`), the board loads the WADs you choose, and only those appear in the menu. The choice is the `WAD_SET` variable, a space-separated list of games, in the order they appear. The default is `doom doom2 plutonia master`.

| Name | Game | WAD looked up in the folder |
| :--- | :--- | :--- |
| `doom` | The Ultimate DOOM, 4 episodes | `doom.wad` |
| `doom2` | DOOM II | `doom2.wad` |
| `plutonia` | The Plutonia Experiment | `plutonia.wad` |
| `tnt` | TNT: Evilution | `tnt.wad` |
| `master` | Master Levels, 20 maps (uses DOOM II as its base) | The WADs of the `master` folder, with the `DOOM2.MAP` |

Point to the folder of your WADs once and build the image with the `WAD_SET` you want:

```bash
./fetch.sh --wad-dir /path/to/the/wads
WAD_SET="doom doom2 tnt master" MENU=1 VIDEO=1 INTERACTIVE=1 ./scripts/image.sh
```

`image.sh` joins the chosen WADs into `build-board/wads.bin`, lists each one with its address and SHA-256, and prints the `load-ram` that takes them to the SDRAM in one go. To change the set, run `image.sh` again with another `WAD_SET` and load the new image. Not every set fits on the board, as section 4 explains. The full use of the menu is in [docs/en/BOARD.md](docs/en/BOARD.md), section 13.

## 4. The board

The `board` target of `build.sh` produces the binary for the Insper-RISC-V `doom` platform. The console goes out over the JTAG UART, the clock comes from the platform `doom_clock.c`, and the WAD is loaded into SDRAM by `riscv-tools load-ram`. `VIDEO=1` turns on the VGA output and `INTERACTIVE=1` turns on the PS/2 keyboard and the full game; without them, the `-timedemo` leaves its result in a fixed SDRAM block that `dump-ram` reads. The SDRAM map, the commands and the comparison with Spike are in [docs/en/BOARD.md](docs/en/BOARD.md). To install the game on the board, from the `.sof` to the menu, follow [docs/en/INSTALL.md](docs/en/INSTALL.md).

```bash
./fetch.sh && ./scripts/image.sh
```

### 4.1. The 64 MB SDRAM and the WADs

The board has 64 MB of SDRAM, and the WADs share it with the game image, the framebuffer, a 7 MB heap and the stack. About 49.5 MB are left for the WADs. These are the sizes of the original v1.9 WADs:

| WAD | Size |
| :--- | ---: |
| `doom.wad` (Ultimate DOOM) | 12.4 MB |
| `doom2.wad` | 14.6 MB |
| `plutonia.wad` | 17.4 MB |
| `tnt.wad` | 18.2 MB |
| Master Levels (the 20 maps together) | 3.8 MB |

The four games together add up to 62.6 MB and do not fit. To put in three, write the three in `WAD_SET` and add the sizes: the total must stay below 49.5 MB. `scripts/wads.sh` does this sum and refuses a set that goes over, saying what to drop.

| Choice | Total |
| :--- | ---: |
| `doom doom2 plutonia master` (default) | 48.2 MB |
| `doom doom2 tnt master` | 49.0 MB |
| `doom plutonia tnt` | 48.0 MB |

Master Levels uses `doom2.wad` as its base, so `master` only goes in together with `doom2`. The default carries the Ultimate DOOM, DOOM II, Plutonia and the 20 maps of Master Levels.

### 4.2. The microSD card: two ways to read the WADs

With `SD=demand ONDEMAND=1` the WADs live only on the microSD card and the game reads them lump by lump, without copying them to the SDRAM. The `SDFS` variable chooses how the card is organized, and both forms work from `build.sh` and `scripts/image.sh`.

| `SDFS` | Name | Card | Prepared by |
| :--- | :--- | :--- | :--- |
| `raw` (default) | `sdcard_direct_mapped` | `wads.bin` written raw from a fixed sector, with no file system | `scripts/wads.sh` packs and `scripts/sdwrite.py` writes |
| `fatfs` | `sdcard_FatFs` | A regular FAT32, with the WADs as files | The operating system file manager, no script |

#### 4.2.1. Raw card (`SDFS=raw`)

```bash
PLATFORM_DIR=platform_doom WAD_DIR=/path/to/wads WAD_SET="doom doom2 plutonia tnt master" \
  MENU=1 VIDEO=1 INTERACTIVE=1 SD=demand ONDEMAND=1 sh scripts/image.sh
uv run --no-project scripts/sdwrite.py --target /dev/mmcblk0
```

`sdwrite.py` erases the card partition table, so the card stops being a FAT32. The card stays raw, and the game finds each WAD from the index written on it.

#### 4.2.2. FAT32 card with FatFs (`SDFS=fatfs`)

```bash
./fetch.sh
PLATFORM_DIR=platform_doom SDFS=fatfs MENU=1 VIDEO=1 INTERACTIVE=1 SD=demand ONDEMAND=1 sh scripts/image.sh
```

This form needs no `WAD_DIR`, `wads.sh` or `sdwrite.py`. `fetch.sh` downloads FatFs R0.15a from the author site (ChaN) with the SHA-256 checked, and `build.sh` compiles it with the game. Prepare the card like this:

1. Format the card as FAT32 (with or without an MBR partition table). Use a cluster of 32 KB or larger, the default for cards of 32 GB and up, which keeps the FAT short.
2. Copy the WADs you have to the root, with these names: `DOOM.WAD`, `DOOM2.WAD`, `PLUTONIA.WAD` and `TNT.WAD`.
3. For Master Levels, create the `MASTER` folder (or `MASTER/WADS`), copy the levels (`ATTACK.WAD`, `CANYON.WAD` and the others) and, if you like, the `DOOM2.MAP` of the package. Master Levels shows in the menu only together with `DOOM2.WAD`.

The menu shows only what it finds on the card, in the same style as before. Names are 8.3 and case-insensitive. The map each Master Levels level opens in DOOM II comes from the `DOOM2.MAP` on the card; without the file, the table built into the game applies, which is the one from the original package `DOOM2.MAP`.

#### 4.2.3. Advantages and cost

| | Raw | FatFs |
| :--- | :--- | :--- |
| Preparing the card | Pack and write with a script, whole card dedicated to the game | Copy files; the card stays usable for other things |
| Changing or adding a WAD | Rewrite the whole `wads.bin` | Copy or delete the file, no rebuild |
| Rebuild when the WADs change | Yes (the table is in the image) | No |
| Code in the image | Baseline | About 13 KB more (FatFs and the disk layer) |
| RAM (bss) | Baseline | About 2 KB less (the FatFs structures add 9.8 KB, while the 4 KB raw index and the 8-sector cache go away) |
| Blocks per lump | $\lceil n/512 \rceil$ plus one | The same, plus about one FAT block per open file every 128 clusters (cluster map) |
| Opening a WAD | No block | About 4 blocks for a 12 MB WAD with 32 KB clusters |
| Small reads alternating between distant sectors | 8-sector cache | One sector per open file (may reread more) |

FatFs is by ChaN and has its own permissive license, free for commercial and open projects, which only asks to keep the copyright notice in the source (text in `fatfs/LICENSE.txt` after `fetch.sh`). It is not stored in this repository. The game configuration is `port/ffconf.h`: read-only, FAT32 without exFAT, 8.3 names, one drive, no clock and `FF_USE_FASTSEEK` (the cluster map, which makes `f_lseek` independent of the WAD size).

## 5. The keys

### 5.1. Keys of the game choice menu

| Key | What it does |
| :--- | :--- |
| Up arrow or W | Previous item |
| Down arrow or S | Next item |
| Left arrow or A | In the Master Levels list, goes to the left column |
| Right arrow or D | In the Master Levels list, goes to the right column |
| Enter, E or space | Chooses the game; on "Master Levels", opens the list; in the list, opens the map |
| Esc or Backspace | In the Master Levels list, goes back to the game choice |

### 5.2. Keys of the game

They are the same in the three games and in Master Levels.

| Key | What it does |
| :--- | :--- |
| W | Moves forward |
| S | Moves backward |
| A | Strafes left |
| D | Strafes right |
| Left arrow | Turns left |
| Right arrow | Turns right |
| E | Uses and opens doors |
| Space or Ctrl | Fires |
| Shift | Runs |
| Alt with the left and right arrows | Strafes |
| 1 to 7 | Switches weapon |
| Tab | Shows the map |
| Esc | Opens the game menu |
| F10 and then Y | Quit Game: returns to the game choice menu |

In the menus of the game itself the four arrows navigate, and W S A D and E act as up, down, left, right and Enter, except when typing the name of a saved game, where they are letters. The up and down keys have no function during play.

## 6. Contributing

Every change enters through a Pull Request to `main`. The general rules of the organization are in [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.en.md).

## 7. Licenses

The code in this repository is Apache 2.0 (`LICENSE` file). `doomgeneric` and the DOOM code are GPL-2.0 and are not kept in this repository. `fetch.sh` downloads them at build time.

The resulting binary combines the game and the platform layer, so it is GPL-2.0. Anyone distributing it must include the GPL-2.0 text and the offer of the corresponding source code. Changes to `doomgeneric`, if needed, are kept as `.patch` files under GPL-2.0.

The shareware `doom1.wad` has its own license, which allows redistributing only the original unmodified package. It is not kept in this repository either.
