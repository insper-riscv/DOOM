#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Cria, sem sudo, uma imagem de cartao FAT32 para os testes da forma sdcard_FatFs.

O `mkfs.fat` (dosfstools) formata o arquivo esparso; este script grava os arquivos de teste direto na imagem (cadeias de
clusters, diretorio raiz e a pasta MASTER), sem montar nada, e confere o resultado com `fsck.fat -n`. As WADs de teste tem
conteudo pseudoaleatorio deterministico (SHAKE-256 do caminho) (`wad_bytes`), entao quem le o cartao consegue regenerar o esperado.

Uso: python3 fat32img.py SAIDA.img [--spc 64] [--size-mib 4096] [--mbr] [--small]
"""

import argparse
import hashlib
import os
import struct
import subprocess
import sys

SECTOR = 512

# (caminho no cartao, tamanho em bytes, fragmentos). Os tamanhos e os nomes imitam o cartao do DOOM em escala menor.
FILES = [
    ("DOOM.WAD", 6 * 1024 * 1024 + 123, 3),
    ("DOOM2.WAD", 3 * 1024 * 1024 + 7, 1),
    ("PLUTONIA.WAD", 1024 * 1024 + 511, 1),
    ("TNT.WAD", 1000 * 1000, 2),
    ("MASTER/VIRGIL.WAD", 150 * 1000 + 3, 1),
    ("MASTER/ATTACK.WAD", 90 * 1000 + 1, 1),
    ("MASTER/TEETH.WAD", 70 * 1000, 1),
    ("MASTER/CANYON.WAD", 33 * 1000 + 5, 1),
]
MAP_TEXT = (
    b"attack.wad: MAP05\r\ncanyon.wad: MAP02\r\nteeth.wad: MAP31\r\n           MAP32\r\nvirgil.wad: MAP03\r\n"
)
FILES_MAP = [("MASTER/DOOM2.MAP", MAP_TEXT)]


def wad_bytes(path: str, size: int) -> bytes:
    """Conteudo de teste da WAD `path`: cabecalho IWAD/PWAD e bytes pseudoaleatorios fixos."""
    head = (b"IWAD" if "/" not in path else b"PWAD") + struct.pack("<II", 0, 12)
    return head + hashlib.shake_256(path.encode()).digest(size - len(head))  # estavel entre versoes do Python


class Fat32:
    """Escritor minimo de FAT32 sobre um arquivo ja formatado pelo mkfs.fat."""

    def __init__(self, path: str, base_sector: int) -> None:
        self.fd = os.open(path, os.O_RDWR)
        self.base = base_sector * SECTOR
        boot = os.pread(self.fd, SECTOR, self.base)
        self.spc = boot[13]
        self.reserved = struct.unpack_from("<H", boot, 14)[0]
        self.nfats = boot[16]
        self.fatsz = struct.unpack_from("<I", boot, 36)[0]
        self.total = struct.unpack_from("<I", boot, 32)[0]
        self.data_start = self.reserved + self.nfats * self.fatsz
        self.clusters = (self.total - self.data_start) // self.spc
        self.next_free = 3  # o cluster 2 e o diretorio raiz
        self.fat: dict[int, int] = {2: 0x0FFFFFFF}
        self.cluster_bytes = self.spc * SECTOR

    def cluster_offset(self, cluster: int) -> int:
        """Posicao em bytes do cluster no arquivo."""
        return self.base + (self.data_start + (cluster - 2) * self.spc) * SECTOR

    def alloc(self, count: int, fragments: int = 1, gap: int = 3) -> list[int]:
        """Reserva `count` clusters em `fragments` pedacos separados por `gap` clusters livres; devolve a lista."""
        clusters: list[int] = []
        per = -(-count // fragments)
        while len(clusters) < count:
            take = min(per, count - len(clusters))
            clusters += range(self.next_free, self.next_free + take)
            self.next_free += take
            if len(clusters) < count:
                self.next_free += gap
        if self.next_free - 2 > self.clusters:
            sys.exit("imagem pequena demais para os arquivos")
        for a, b in zip(clusters, clusters[1:] + [0x0FFFFFFF], strict=True):
            self.fat[a] = b
        return clusters

    def write_chain(self, clusters: list[int], data: bytes) -> None:
        """Grava `data` nos clusters, na ordem."""
        for i, c in enumerate(clusters):
            chunk = data[i * self.cluster_bytes : (i + 1) * self.cluster_bytes]
            os.pwrite(self.fd, chunk, self.cluster_offset(c))

    @staticmethod
    def entry(name: str, attr: int, cluster: int, size: int) -> bytes:
        """Entrada de diretorio 8.3 (sem nomes longos)."""
        if name in (".", ".."):
            short = name.ljust(11)
        else:
            base, _, ext = name.partition(".")
            short = base.upper().ljust(8)[:8] + ext.upper().ljust(3)[:3]
        date = ((2025 - 1980) << 9) | (1 << 5) | 1
        return struct.pack("<11sBBBHHHHHHHI", short.encode("ascii"), attr, 0, 0, 0, date, date, cluster >> 16, 0, date,
                           cluster & 0xFFFF, size)

    def flush_fat(self) -> None:
        """Escreve as entradas da FAT em todas as copias."""
        entries = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF, **self.fat}
        top = max(entries) + 1
        table = bytearray(top * 4 + (-top * 4 % SECTOR))
        for k, v in entries.items():
            struct.pack_into("<I", table, 4 * k, v)
        for n in range(self.nfats):
            os.pwrite(self.fd, bytes(table), self.base + (self.reserved + n * self.fatsz) * SECTOR)


def build(path: str, spc: int, size_mib: int, mbr: bool, files: list[tuple[str, int, int]], with_map: bool = True) -> None:
    """Formata e popula a imagem."""
    offset = 2048 if mbr else 0
    if os.path.exists(path):
        os.remove(path)
    subprocess.run(["mkfs.fat", "-C", "-F", "32", "-s", str(spc), "-S", str(SECTOR), *(["--offset", str(offset)] if offset else []), path, str(size_mib * 1024)],
                   check=True, stdout=subprocess.DEVNULL)
    fs = Fat32(path, offset)
    dirs: dict[str, list[bytes]] = {"": []}
    contents: list[tuple[str, bytes]] = [(p, wad_bytes(p, s)) for p, s, _ in files] + (FILES_MAP if with_map else [])
    frags = {p: f for p, _, f in files}
    # pastas
    for p, _ in contents:
        if "/" in p:
            d = p.split("/")[0]
            if d not in dirs:
                dirs[d] = []
    dir_cluster: dict[str, int] = {"": 2}
    for d in dirs:
        if d:
            dir_cluster[d] = fs.alloc(1)[0]
            dirs[""].append(Fat32.entry(d, 0x10, dir_cluster[d], 0))
            dirs[d] += [Fat32.entry(".", 0x10, dir_cluster[d], 0), Fat32.entry("..", 0x10, 0, 0)]
    for p, data in contents:
        d, _, name = p.rpartition("/")
        count = -(-len(data) // fs.cluster_bytes)
        clusters = fs.alloc(count, frags.get(p, 1))
        fs.write_chain(clusters, data)
        dirs[d].append(Fat32.entry(name, 0x20, clusters[0], len(data)))
    for d, entries in dirs.items():
        raw = b"".join(entries)
        if len(raw) > fs.cluster_bytes:
            sys.exit("diretorio maior que um cluster")
        os.pwrite(fs.fd, raw, fs.cluster_offset(dir_cluster[d]))
    fs.flush_fat()
    used = len(fs.fat)  # clusters ocupados (o do diretorio raiz inclusive)
    info = bytearray(os.pread(fs.fd, SECTOR, fs.base + SECTOR))
    struct.pack_into("<II", info, 488, fs.clusters - used, fs.next_free)  # FSInfo: livres e proximo livre
    os.pwrite(fs.fd, bytes(info), fs.base + SECTOR)
    if mbr:
        part = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0x0C, b"\xfe\xff\xff", offset, fs.total)
        os.pwrite(fs.fd, bytes(446) + part + bytes(48) + b"\x55\xaa", 0)
    os.close(fs.fd)
    if not mbr:
        subprocess.run(["fsck.fat", "-n", path], check=True)


def load_sectors(path: str) -> dict[int, bytes]:
    """Os setores nao nulos da imagem (usa os buracos do arquivo esparso para nao ler 4 GiB)."""
    sectors: dict[int, bytes] = {}
    fd = os.open(path, os.O_RDONLY)
    end = os.fstat(fd).st_size
    pos = 0
    while pos < end:
        try:
            data = os.lseek(fd, pos, os.SEEK_DATA)
        except OSError:  # sem dados ate o fim
            break
        hole = os.lseek(fd, data, os.SEEK_HOLE)
        at = data
        while at < hole:  # pread pode devolver menos que o pedido: repete ate cobrir a extensao
            block = os.pread(fd, min(1 << 20, hole - at), at)
            if not block:
                break
            for i in range(0, len(block), SECTOR):
                chunk = block[i : i + SECTOR].ljust(SECTOR, b"\0")
                if any(chunk):
                    sectors[(at + i) // SECTOR] = chunk
            at += len(block)
        pos = hole
    os.close(fd)
    return sectors


def main() -> None:
    """Linha de comando."""
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--spc", type=int, default=64, help="setores por cluster (64 = 32 KiB)")
    ap.add_argument("--size-mib", type=int, default=4096)
    ap.add_argument("--no-map", action="store_true", help="sem o MASTER/DOOM2.MAP (vale a tabela embutida no jogo)")
    ap.add_argument("--mbr", action="store_true", help="particao 1 a partir do setor 2048, com MBR")
    a = ap.parse_args()
    build(a.out, a.spc, a.size_mib, a.mbr, FILES, not a.no_map)


if __name__ == "__main__":
    main()
