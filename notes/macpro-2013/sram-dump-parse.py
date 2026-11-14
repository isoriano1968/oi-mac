#!/usr/bin/python3
"""Rebuild the SMC SRAM dumps of patch 0026b from /var/adm/messages lines.

  sram-dump-parse.py <log> [tahiti_smc.bin]

The log holds the "illumos: SMC SRAM dump <n> 0x<addr>: w0 .. w7" lines
(two dumps per GPU: 1 = first read, idle; 2 = under load). For each GPU
it checks that both dumps are complete (384 lines, 0x1d000-0x1ffff) and
prints:
  - the words that differ between dump 1 and dump 2 (what follows load);
  - with the firmware image (loaded at SMC address 0x10000, big-endian
    words, as amdgpu_si_copy_bytes_to_smc writes it): every word inside
    the image range whose value in dump 1 differs from the image, i.e.
    what the driver or the firmware changed after the upload.
  - the two GPUs' dump 1 side by side where they differ.
Read-only; writes nothing.
"""
import re
import struct
import sys

LO, HI = 0x1d000, 0x20000
UCODE = 0x10000
LINE = re.compile(r"(amdgpu\d+): \[drm\] illumos: SMC SRAM dump (\d+) "
                  r"0x([0-9a-f]{5}):((?: [0-9a-f]{8}){8})")


def load(path):
    dumps = {}
    for text in open(path, errors="replace"):
        m = LINE.search(text)
        if not m:
            continue
        gpu, n, addr = m.group(1), int(m.group(2)), int(m.group(3), 16)
        words = [int(x, 16) for x in m.group(4).split()]
        d = dumps.setdefault((gpu, n), {})
        for i, w in enumerate(words):
            d[addr + 4 * i] = w
    return dumps


def main():
    dumps = load(sys.argv[1])
    image = {}
    if len(sys.argv) > 2:
        data = open(sys.argv[2], "rb").read()
        # amdgpu firmware header (little-endian): size_bytes, ...,
        # ucode_size_bytes at 20, ucode_array_offset_bytes at 24 and the
        # SMC v1 ucode_start_addr at 32; the ucode itself is big-endian.
        start, off0, size = UCODE, 0, len(data)
        if struct.unpack("<I", data[0:4])[0] == len(data):
            size, off0 = struct.unpack("<II", data[20:28])
            start = struct.unpack("<I", data[32:36])[0]
        for off in range(0, size - 3, 4):
            image[start + off] = struct.unpack(
                ">I", data[off0 + off:off0 + off + 4])[0]
    gpus = sorted({g for g, _ in dumps})
    for gpu in gpus:
        for n in (1, 2):
            d = dumps.get((gpu, n), {})
            missing = [a for a in range(LO, HI, 4) if a not in d]
            print(f"{gpu} dump {n}: {len(d)} of {(HI - LO) // 4} words"
                  + (f", missing from 0x{missing[0]:05x}" if missing else ""))
        d1, d2 = dumps.get((gpu, 1), {}), dumps.get((gpu, 2), {})
        print(f"\n{gpu}: dump 1 -> dump 2 (idle -> load)")
        for a in range(LO, HI, 4):
            if a in d1 and a in d2 and d1[a] != d2[a]:
                print(f"  0x{a:05x}: 0x{d1[a]:08x} -> 0x{d2[a]:08x}")
        if image:
            print(f"\n{gpu}: dump 1 vs firmware image (inside the image)")
            for a in range(LO, HI, 4):
                if a in image and a in d1 and image[a] != d1[a]:
                    same = " (same in dump 2)" if d2.get(a) == d1[a] else ""
                    print(f"  0x{a:05x}: image 0x{image[a]:08x}, "
                          f"SRAM 0x{d1[a]:08x}{same}")
        print()
    if len(gpus) == 2:
        a1, b1 = dumps.get((gpus[0], 1), {}), dumps.get((gpus[1], 1), {})
        print(f"dump 1, {gpus[0]} vs {gpus[1]} where they differ")
        for a in range(LO, HI, 4):
            if a in a1 and a in b1 and a1[a] != b1[a]:
                print(f"  0x{a:05x}: 0x{a1[a]:08x}  0x{b1[a]:08x}")


if __name__ == "__main__":
    main()
