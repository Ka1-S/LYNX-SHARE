"""Sets IMAGE_FILE_LARGE_ADDRESS_AWARE (0x20) in a PE file's COFF header (round 176a: LYNX-bridge32.exe; zig's linker has
no flag for it). A 32-bit process on 64-bit Windows then gets 4 GB of address space instead of 2 GB.
usage: pe_set_laa.py <exe>"""
import struct, sys

with open(sys.argv[1], "r+b") as f:
    f.seek(0x3C)
    pe = struct.unpack("<I", f.read(4))[0]
    f.seek(pe)
    if f.read(4) != b"PE\0\0":
        sys.exit("pe_set_laa: not a PE file")
    f.seek(pe + 22)
    flags = struct.unpack("<H", f.read(2))[0]
    f.seek(pe + 22)
    f.write(struct.pack("<H", flags | 0x20))
