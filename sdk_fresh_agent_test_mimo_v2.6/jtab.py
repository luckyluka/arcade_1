#!/usr/bin/env python3
"""jtab.py OFF N — decode an x86-64 int32 relative jump table at OFF in ./arcade1."""
import struct
import sys

d = open("arcade1", "rb").read()
e_phoff = struct.unpack_from("<Q", d, 0x20)[0]
e_phentsize = struct.unpack_from("<H", d, 0x36)[0]
e_phnum = struct.unpack_from("<H", d, 0x38)[0]
loads = []
for i in range(e_phnum):
    o = e_phoff + i * e_phentsize
    p_type = struct.unpack_from("<I", d, o)[0]
    if p_type != 1:
        continue
    p_offset, p_vaddr, p_filesz = struct.unpack_from("<QQQ", d, o + 8)
    loads.append((p_vaddr, p_offset, p_filesz))


def v2o(v):
    for va, off, sz in loads:
        if va <= v < va + sz:
            return off + (v - va)
    raise ValueError(hex(v))


off = int(sys.argv[1], 0)
n = int(sys.argv[2], 0)
for i in range(n):
    e = struct.unpack_from("<i", d, v2o(off + i * 4))[0]
    tgt = off + e
    print("%3d  entry=%+d  target=0x%x" % (i, e, tgt))
