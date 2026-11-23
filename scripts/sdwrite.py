#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Grava o wads.bin e o cabecalho do DOOM num cartao microSD (cru, sem sistema de arquivos) ou num arquivo de imagem.

Formato (port/doom_sd.h): setor H (padrao 2048) com o cabecalho de 512 bytes, depois o indice (128 bytes por WAD)
e, a partir de um setor multiplo de 8, o payload, que e o wads.bin sem mudar um byte.

Uso (so biblioteca padrao; Python sempre por uv):

    uv run --no-project scripts/sdwrite.py --dry-run --target /dev/mmcblk0
    uv run --no-project scripts/sdwrite.py --target /dev/mmcblk0 --destroy-existing --confirm /dev/mmcblk0
    uv run --no-project scripts/sdwrite.py --target sd.img            # arquivo de imagem, sem travas de disco

Travas para dispositivo de bloco: o usuario nomeia o dispositivo na linha de comando (nada e adivinhado), tem de ser
um disco inteiro (nao particao) de cartao SD/MMC ou removivel, nao pode ter nenhuma particao montada nem ser usado
como swap, tem de caber o payload, e o nome e confirmado (digitado, ou --confirm com o mesmo nome). Se a regiao a gravar
cruza uma particao existente (um FAT32 recem formatado, por exemplo), recusa, a menos que --destroy-existing, que
tambem zera o setor 0 (tabela de particoes) para o cartao nao ficar com uma tabela que aponta para um FAT quebrado.
"""

from __future__ import annotations

import argparse
import json
import os
import stat
import struct
import subprocess
import sys
import zlib
from pathlib import Path

SECTOR = 512
MAGIC0 = 0x4D4F4F44  # 'DOOM'
MAGIC1 = 0x31304453  # 'SD01'
VERSION = 1
FLAG_PAYLOAD = 1
ENTRY_BYTES = 128
MAX_WADS = 32
CHUNK = 1 << 20
MAX_DISK_BYTES = 512 * 1024**3

TITLES = {
    "doom.wad": ("ULTIMATE DOOM", "4 EPISODES - 36 LEVELS"),
    "doom2.wad": ("DOOM II", "HELL ON EARTH - 32 LEVELS"),
    "plutonia.wad": ("PLUTONIA", "THE PLUTONIA EXPERIMENT - 32 LEVELS"),
    "tnt.wad": ("TNT: EVILUTION", "FINAL DOOM - 32 LEVELS"),
}


class Refusal(Exception):
    """Uma trava de seguranca recusou a gravacao."""


def crc32(data: bytes, crc: int = 0) -> int:
    """CRC-32 IEEE (o do zlib), continuando de crc."""
    return zlib.crc32(data, crc) & 0xFFFFFFFF


def fixed(text: str, size: int) -> bytes:
    """Texto ASCII em `size` bytes, terminado em zero e completado com zeros."""
    raw = text.encode("ascii")[: size - 1]
    return raw + bytes(size - len(raw))


def read_index(path: Path) -> list[dict[str, object]]:
    """Le o wads.index.tsv que o scripts/wads.sh escreve: tipo, nome, deslocamento, tamanho e mapa."""
    entries: list[dict[str, object]] = []
    for line in path.read_text(encoding="ascii").splitlines():
        if not line.strip():
            continue
        kind, name, offset, size, map_text = (line.split("\t") + [""])[:5]
        entries.append(
            {
                "kind": 0 if kind == "iwad" else 1,
                "name": name.lower(),
                "offset": int(offset),
                "size": int(size),
                "map": int(map_text) if map_text else 0,
            }
        )
    if not entries or len(entries) > MAX_WADS:
        raise Refusal(f"{path}: {len(entries)} WADs (1 a {MAX_WADS})")
    return entries


def build_image(wads: Path, index: Path, header_sector: int, dest_addr: int) -> tuple[bytes, int, int, int]:
    """Monta cabecalho e indice. Devolve (bytes de H ate o fim do indice, setor do payload, tamanho, CRC-32 do payload)."""
    payload = wads.read_bytes()
    entries = read_index(index)
    count = len(entries)
    index_sectors = (count + 3) // 4
    payload_sector = (header_sector + 1 + index_sectors + 7) // 8 * 8
    body = bytearray()
    for entry in entries:
        offset, size = int(entry["offset"]), int(entry["size"])  # type: ignore[call-overload]
        if offset % 4096 != 0 or offset + size > len(payload):
            raise Refusal(f"{entry['name']}: deslocamento {offset} ou tamanho {size} fora do wads.bin")
        name = str(entry["name"])
        if entry["kind"] == 0:
            title, subtitle = TITLES.get(name, (name.removesuffix(".wad").upper(), ""))
        else:
            title, subtitle = name.removesuffix(".wad").upper(), ""
        body += fixed(name, 16) + fixed(title, 32) + fixed(subtitle, 48)
        body += struct.pack(
            "<IIIBBHI",
            payload_sector + offset // SECTOR,
            size,
            crc32(payload[offset : offset + size]),
            int(entry["kind"]),  # type: ignore[call-overload]
            int(entry["map"]),  # type: ignore[call-overload]
            0,
            offset,
        )
        body += bytes(ENTRY_BYTES - len(body) % ENTRY_BYTES) if len(body) % ENTRY_BYTES else b""
    body += bytes(index_sectors * SECTOR - len(body))
    payload_crc = crc32(payload)
    head = struct.pack(
        "<IIHHIIIIIII",
        MAGIC0,
        MAGIC1,
        VERSION,
        FLAG_PAYLOAD,
        count,
        index_sectors,
        payload_sector,
        len(payload),
        dest_addr,
        payload_crc,
        crc32(bytes(body)),
    )
    head += bytes(508 - len(head))
    head += struct.pack("<I", crc32(head))
    return head + bytes(body), payload_sector, len(payload), payload_crc


def lsblk_tree(device: str) -> dict[str, object]:
    """Descreve o disco e as suas particoes pelo lsblk (tamanho, montagem, removivel, transporte, inicio)."""
    out = subprocess.run(
        ["lsblk", "-J", "-b", "-o", "NAME,PATH,TYPE,SIZE,RM,HOTPLUG,TRAN,MOUNTPOINTS,START,PKNAME", device],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    nodes = json.loads(out)["blockdevices"]
    if len(nodes) != 1:
        raise Refusal(f"lsblk nao achou {device}")
    return nodes[0]


def is_card(node: dict[str, object], name: str) -> bool:
    """Cartao SD/MMC (mmcblk) ou disco removivel/hotplug; nunca o disco do sistema."""
    sysfs = Path("/sys/block") / name
    removable = str(node.get("rm")) in ("1", "True", "true") or str(node.get("hotplug")) in ("1", "True", "true")
    return removable or name.startswith("mmcblk") and (sysfs / "device").exists()


def check_device(target: str, payload_end_bytes: int, header_sector: int, destroy: bool) -> list[str]:
    """Aplica as travas de um dispositivo de bloco; devolve as linhas do plano. Levanta Refusal se alguma falha."""
    real = os.path.realpath(target)
    if not real.startswith("/dev/"):
        raise Refusal(f"{target} nao e um dispositivo em /dev")
    node = lsblk_tree(real)
    if node.get("type") != "disk":
        raise Refusal(f"{real} e {node.get('type')}, nao um disco inteiro (nomeie /dev/mmcblk0, nao a particao)")
    name = Path(real).name
    if not is_card(node, name):
        raise Refusal(f"{real} nao e cartao SD/MMC nem disco removivel (RM/HOTPLUG = 0): recusado")
    size = int(node["size"])  # type: ignore[call-overload]
    if size > MAX_DISK_BYTES:
        raise Refusal(f"{real} tem {size} bytes: grande demais para ser um cartao")
    if size < payload_end_bytes:
        raise Refusal(f"{real} tem {size} bytes e a gravacao precisa de {payload_end_bytes}")
    mounted = [str(m) for n in [node, *node.get("children", [])] for m in (n.get("mountpoints") or []) if m]  # type: ignore[union-attr]
    if mounted:
        raise Refusal(f"{real} tem particoes montadas ({', '.join(mounted)}): desmonte primeiro")
    for line in Path("/proc/swaps").read_text().splitlines()[1:]:
        if line.split()[0].startswith(real):
            raise Refusal(f"{real} e usado como swap")
    lines = [f"dispositivo {real}: {size} bytes, tipo {node.get('type')}, TRAN={node.get('tran')}"]
    first = header_sector * SECTOR
    for child in node.get("children", []):  # type: ignore[union-attr]
        start = int(child.get("start") or 0) * SECTOR
        end = start + int(child["size"])
        if start < payload_end_bytes and end > first:
            if not destroy:
                raise Refusal(
                    f"a gravacao (setor {header_sector} em diante) cruza a particao {child['path']} "
                    f"(setores {start // SECTOR} a {end // SECTOR}): use --destroy-existing para apagar o que ha nela"
                )
            lines.append(f"APAGA a particao {child['path']} (e o setor 0, a tabela de particoes)")
    return lines


def confirm(target: str, given: str | None) -> None:
    """Pede para digitar o nome do dispositivo (ou confere --confirm)."""
    if given is not None:
        if given != target:
            raise Refusal(f"--confirm {given} difere do alvo {target}")
        return
    if not sys.stdin.isatty():
        raise Refusal("sem terminal para confirmar: passe --confirm IGUAL_AO_ALVO ou rode num terminal")
    typed = input(f"Digite {target} para gravar (qualquer outra coisa cancela): ")
    if typed.strip() != target:
        raise Refusal("cancelado")


def write_all(fd: int, offset: int, data: bytes) -> None:
    """pwrite completo."""
    view = memoryview(data)
    done = 0
    while done < len(view):
        done += os.pwrite(fd, view[done:], offset + done)


def main() -> int:
    """Ponto de entrada."""
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", required=True, help="dispositivo de bloco do cartao (disco inteiro) ou arquivo de imagem")
    ap.add_argument("--wads", type=Path, default=here / "build-board/wads.bin")
    ap.add_argument("--index", type=Path, default=here / "build-board/wads.index.tsv")
    ap.add_argument("--header-sector", type=int, default=2048)
    ap.add_argument("--dest-addr", type=lambda s: int(s, 0), default=0x40100000, help="destino na SDRAM (modo SD=load)")
    ap.add_argument("--dry-run", action="store_true", help="faz todas as conferencias e mostra o plano, sem escrever")
    ap.add_argument("--destroy-existing", action="store_true", help="aceita apagar as particoes que a regiao cruza")
    ap.add_argument("--confirm", help="repete o nome do alvo no lugar de digita-lo")
    args = ap.parse_args()

    image, payload_sector, size, payload_crc = build_image(args.wads, args.index, args.header_sector, args.dest_addr)
    end_bytes = (payload_sector * SECTOR) + (size + SECTOR - 1) // SECTOR * SECTOR
    plan = [
        f"payload {args.wads} ({size} bytes, CRC-32 {payload_crc:08x}) no setor {payload_sector}",
        f"cabecalho no setor {args.header_sector}, indice com {(len(image) // SECTOR) - 1} setor(es)",
        f"a gravacao ocupa os bytes {args.header_sector * SECTOR} a {end_bytes} ({end_bytes / 1e6:.1f} MB)",
    ]
    mode = os.stat(args.target).st_mode if os.path.exists(args.target) else None
    if mode is not None and stat.S_ISBLK(mode):
        plan += check_device(args.target, end_bytes, args.header_sector, args.destroy_existing)
        is_device = True
    elif mode is not None and not stat.S_ISREG(mode):
        raise Refusal(f"{args.target} nao e dispositivo de bloco nem arquivo comum")
    else:
        plan.append(f"arquivo de imagem {args.target} (criado ou estendido; sem travas de disco)")
        is_device = False
    print("\n".join(plan))
    if args.dry_run:
        print("ENSAIO: nada foi escrito.")
        return 0
    if is_device:
        confirm(args.target, args.confirm)
    flags = os.O_RDWR | (0 if is_device else os.O_CREAT)
    fd = os.open(args.target, flags | (os.O_SYNC if is_device else 0), 0o644)
    try:
        if is_device and args.destroy_existing:
            write_all(fd, 0, bytes(SECTOR))
        if not is_device:
            os.ftruncate(fd, max(os.fstat(fd).st_size, end_bytes))
        write_all(fd, args.header_sector * SECTOR, image)
        with args.wads.open("rb") as src:
            offset = payload_sector * SECTOR
            while chunk := src.read(CHUNK):
                write_all(fd, offset, chunk)
                offset += len(chunk)
                print(f"\r{(offset - payload_sector * SECTOR) >> 20} MiB", end="", flush=True)
        os.fsync(fd)
        print()
        # Conferencia: le de volta o cabecalho e o payload e compara os CRC-32
        if os.pread(fd, len(image), args.header_sector * SECTOR) != image:
            raise Refusal("o cabecalho lido de volta difere do gravado")
        crc, left, offset = 0, size, payload_sector * SECTOR
        while left:
            data = os.pread(fd, min(CHUNK, left), offset)
            crc, left, offset = crc32(data, crc), left - len(data), offset + len(data)
        if crc != payload_crc:
            raise Refusal(f"CRC-32 lido de volta {crc:08x} difere de {payload_crc:08x}")
    finally:
        os.close(fd)
    print(f"gravado e conferido: CRC-32 {payload_crc:08x}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refusal as error:
        print(f"RECUSADO: {error}", file=sys.stderr)
        sys.exit(2)
