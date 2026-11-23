#!/usr/bin/env python3
"""Confere uma imagem FAT32 de teste com o doom_fatfs.c e o FatFs no host (sem simulador): monta, lista e le."""
import ctypes, subprocess, sys, tempfile, importlib.util
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("fat32img", ROOT / "scripts/sd_test/fat32img.py")
t = importlib.util.module_from_spec(spec); spec.loader.exec_module(t)
tmp = Path(tempfile.mkdtemp())
objs = []
for name, src, w in (("stub", "scripts/sd_test/host_stub.c", ["-Wall"]), ("fatfs", "port/doom_fatfs.c", ["-Wall", "-Wextra", "-Werror"]),
                     ("diskio", "port/doom_diskio.c", ["-Wall", "-Wextra", "-Werror"]), ("ff", "fatfs/source/ff.c", ["-w"])):
    o = tmp / f"{name}.o"
    subprocess.run(["gcc", "-c", "-fPIC", "-O1", *w, "-DDOOM_SD_DEMAND", "-DDOOM_SD_FATFS", f"-I{ROOT/'port'}", f"-I{ROOT/'fatfs/source'}",
                    f"-I{ROOT/'scripts/sd_test'}", "-o", str(o), str(ROOT / src)], check=True)
    objs.append(str(o))
subprocess.run(["gcc", "-shared", "-o", str(tmp / "l.so"), *objs], check=True)
lib = ctypes.CDLL(str(tmp / "l.so"))
img = sys.argv[1]
lib.host_open(img.encode())
print("load_index", lib.doom_sd_load_index())
class W(ctypes.Structure): _fields_ = [("name", ctypes.c_char_p), ("addr", ctypes.c_uint32), ("size", ctypes.c_uint32)]
tab = (W * 32).in_dll(lib, "doom_dyn_wad_table")
n = ctypes.c_int.in_dll(lib, "doom_dyn")
paths = ["DOOM.WAD", "DOOM2.WAD", "PLUTONIA.WAD", "TNT.WAD"] + ["MASTER/" + x for x in ("ATTACK.WAD", "CANYON.WAD", "TEETH.WAD", "VIRGIL.WAD")]
bad = 0
for h, p in enumerate(paths):
    want = t.wad_bytes(p, next(s for q, s, _ in t.FILES if q == p))
    for off, ln in ((0, 12), (len(want)//2+3, 700), (len(want)-300, 300), (5, 40), (len(want)-10, 100), (2*1024*1024+100, 1000)):
        buf = (ctypes.c_uint8 * (ln + 8))()
        lib.doom_wad_read.restype = ctypes.c_size_t
        got = lib.doom_wad_read(ctypes.c_uint32(h), ctypes.c_uint32(off), buf, ctypes.c_size_t(ln))
        exp = want[off:off+ln]
        ok = got == len(exp) and bytes(buf)[:got] == exp
        if not ok:
            bad += 1
            first = next((i for i in range(min(got, len(exp))) if buf[i] != exp[i]), None)
            print("ERRO", p, off, ln, "got", got, "esperado", len(exp), "primeiro byte diferente", first)
print("ok" if not bad else f"{bad} erros")
