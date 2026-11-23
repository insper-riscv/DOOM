# Build and the WAD file

## 1. Build time

Measured on the development workstation, with the GCC of the `dev_tools` container, with no object cache (the output folder is deleted before each run) and the container image already pulled.

| Item | Value |
| :--- | :--- |
| Processor | Intel Core i7-14700 (20 cores, 28 threads) |
| Memory | 31 GB |
| Compiler | `riscv32-unknown-elf-gcc` 16.1.0 (`ghcr.io/insper-riscv/dev_tools:latest`), target `rv32im`, `ilp32`, picolibc |
| Command | `TARGET=board MENU=1 VIDEO=1 INTERACTIVE=1 SD=demand ONDEMAND=1 sh build.sh` (the game with menu and SD card, plus packing the user's WADs) |

| Optimization | Time | Size of `doom.bin` |
| :--- | ---: | ---: |
| `-O3 -funroll-loops -flto` (the default of `image.sh`) | 16.0 s | 592,636 bytes |
| `-O2` | 11.2 s | 425,540 bytes |

The time covers the container start, the build of `doomgeneric`, of the port and of the platform runtime, the link and the binary generation. `-flto` runs serially (GCC prints `using serial compilation of 6 LTRANS jobs`), which contributes to it taking about 5 s more than `-O2`. The first run on a new computer adds `fetch.sh`, which downloads `doomgeneric` at a pinned commit and the platform runtime, and the pull of the container image.

## 2. What a WAD is

WAD stands for "Where's All the Data". It is the single file where DOOM keeps everything that is not code: maps, textures, sprites, sounds, music, palettes and demos. The engine reads the WAD by name, and almost everything the game shows or plays comes from it.

### 2.1. Format

| Part | Size | Content |
| :--- | ---: | :--- |
| Header | 12 bytes | Identification (`IWAD` or `PWAD`), number of lumps (32-bit little-endian integer) and offset of the lump table |
| Lumps | variable | The data, one after another |
| Lump table (directory) | 16 bytes per lump | Offset of the lump in the file, size and a name of up to 8 characters |

A **lump** is a named block of data. The main ones:

| Lump | Content |
| :--- | :--- |
| `PLAYPAL` | 14 palettes of 256 colors (3 bytes per color): the normal one and those for damage, item pickup and radiation |
| `COLORMAP` | 34 tables of 256 bytes that darken colors by distance and light |
| `TEXTURE1`, `TEXTURE2`, `PNAMES` | Assembly of the wall textures from patches |
| Patches and sprites | Column-based images, used by walls, monsters, HUD and menu |
| Floors and ceilings (flats) | 64x64 pixel blocks with no header |
| Map (`E1M1`, `MAP01` and others) | `THINGS`, `LINEDEFS`, `SIDEDEFS`, `VERTEXES`, `SEGS`, `SSECTORS`, `NODES`, `SECTORS`, `REJECT` and `BLOCKMAP` |
| `DS*` and `D_*` | Sound effects and music |
| `DEMO1` to `DEMO4` | Key recordings the game replays by itself |

### 2.2. IWAD and PWAD

| Type | Role |
| :--- | :--- |
| **IWAD** (base game) | Holds the whole game: `DOOM.WAD`, `DOOM2.WAD`, `PLUTONIA.WAD` and `TNT.WAD` |
| **PWAD** (patch) | Adds or replaces lumps of the IWAD, like the Master Levels maps |

With several files the engine builds a single list, and for a repeated name the lump of the file loaded last wins. That is how a Master Levels map replaces a DOOM II map: `doom -iwad doom2.wad -file attack.wad -warp 1`.

### 2.3. The files of this project

The commercial WADs are not in the repository: the user points to the folder that holds them (`fetch.sh --wad-dir`).

| File | Size (bytes) | Content |
| :--- | ---: | :--- |
| `DOOM.WAD` | 12,408,292 | The Ultimate DOOM, 4 episodes |
| `DOOM2.WAD` | 14,604,584 | DOOM II, 32 levels |
| `PLUTONIA.WAD` | 17,420,824 | The Plutonia Experiment |
| `TNT.WAD` | 18,195,736 | TNT: Evilution |
| Master Levels (20 PWADs) | 3,753,798 | Extra DOOM II levels, plus `DOOM2.MAP`, which says which map each one enters |
| `doom1.wad` (shareware) | 4,196,020 | Episode 1, used in the measurements with `demo1` |

The four IWADs plus the Master Levels add up to 66,428,128 bytes when packed with 4 KiB alignment, which does not fit the 64 MB of the SDRAM together with the game. That is why there are modes where the game reads from the card on demand.

## 3. How the port uses the WAD

| Mode | Where the WAD lives | How the engine reads |
| :--- | :--- | :--- |
| Memory (`load-ram`) | In the SDRAM at `0x40100000`, written over JTAG (`wads.sh` joins the chosen ones into `wads.bin` and generates the address table) | `memcpy` from a SDRAM region |
| `sdcard_direct_mapped` (`SDFS=raw`) | On the SD card, raw, from sector 2056, with the header at sector 2048 (`sdwrite.py`) | 512-byte block reads per lump, in `W_ReadLump` |
| `sdcard_FatFs` (`SDFS=fatfs`) | Files on a FAT32 card | `f_lseek` and `f_read` of FatFs |

In the card modes, `W_ReadLump` fetches only the requested lump, and the DOOM zone memory (the 7 MB heap) keeps what was already read, as the original game did with the disk. A level reads a few MB of textures and sprites as soon as it starts, and the loading screen shows the MB counter read from the card.
