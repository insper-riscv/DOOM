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

The `board` target of `build.sh` produces the binary for the Insper-RISC-V `doom` platform. The console goes out over the JTAG UART, the clock comes from the platform `doom_clock.c`, and the WAD is loaded into SDRAM by `riscv-tools load-ram`. `VIDEO=1` turns on the VGA output and `INTERACTIVE=1` turns on the PS/2 keyboard and the full game; without them, the `-timedemo` leaves its result in a fixed SDRAM block that `dump-ram` reads. The SDRAM map, the commands and the comparison with Spike are in [docs/en/BOARD.md](docs/en/BOARD.md).

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

## 5. The keys and how the menu was made

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

### 5.3. How the menu was made

1. **Art from the WAD itself.** The menu carries no image. It reads from the first WAD of the list the background (the title screen, darkened by one of the light maps of the WAD), the skull, the small font of the game and the 256-color palette. The repository still holds no DOOM file.
2. **A 320x200 screen of indices.** Each screen is built in an 8-bit-per-pixel buffer, the same format as the game, and the text is drawn letter by letter with the cutouts of the font, enlarged 2 times in the titles. The chosen item uses the original colors of the font, which are reddish. The others swap each color for a gray of similar brightness, and the descriptions use a darker gray.
3. **Delivery to the video.** The buffer and the palette go straight to the video peripheral, the same way the game writes a frame. The skull blinks every 250 ms, and the screen is redrawn only when something changes.
4. **Keyboard.** The menu reads the events of the same PS/2 keyboard as the game, with no new driver.
5. **From the choice to the game.** The choice becomes the arguments of DOOM: the name of the base WAD and, in Master Levels, the WAD of the map and the map it enters. The WADs sit in a single image in the SDRAM, with a table of name, address and size, and the game opens each one by name.
6. **Back to the menu.** When the game ends, the program restores the initial data and starts over from the beginning. Since the menu is the first thing the program does, it reappears by itself, with nothing reloaded into the SDRAM.
7. **Checking without the board.** `scripts/menu_test.sh` runs the menu on Spike with a key script, checks the choice returned and saves each screen as a PNG in `build-layout/menu-png/`. The images are not kept in the repository because they show the DOOM art.

## 6. Contributing

Every change enters through a Pull Request to `main`. The general rules of the organization are in [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.en.md).

## 7. Licenses

The code in this repository is Apache 2.0 (`LICENSE` file). `doomgeneric` and the DOOM code are GPL-2.0 and are not kept in this repository. `fetch.sh` downloads them at build time.

The resulting binary combines the game and the platform layer, so it is GPL-2.0. Anyone distributing it must include the GPL-2.0 text and the offer of the corresponding source code. Changes to `doomgeneric`, if needed, are kept as `.patch` files under GPL-2.0.

The shareware `doom1.wad` has its own license, which allows redistributing only the original unmodified package. It is not kept in this repository either.
