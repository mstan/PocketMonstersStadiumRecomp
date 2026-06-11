#!/usr/bin/env python3
"""Oracle test for the game's Yay0-style decompressor (FUN_80048010).

Replicates the exact algorithm decompiled from 0x80048010 against the real
ROM bytes that the boot path stages (audio module blob), to decide whether
the 3.5s runaway-write hang in the recompiled build is bad input data
(DMA/staging bug) or a miscompiled decompressor (recompiler bug).

Header (offsets relative to blob start, big-endian):
  +0x00 magic? (not checked by the game)
  +0x04 decompressed size
  +0x08 offset of link table (u16 entries)
  +0x0C offset of chunk bytes
  +0x10 bitstream start (32-bit words, MSB-first: 1=literal, 0=backref)
"""
import struct, sys

def decompress(blob, max_out=16 * 1024 * 1024):
    magic = blob[0:4]
    dec_size = struct.unpack_from(">I", blob, 4)[0]
    link_off = struct.unpack_from(">I", blob, 8)[0]
    chunk_off = struct.unpack_from(">I", blob, 0xC)[0]
    print(f"magic={magic!r} dec_size=0x{dec_size:X} link_off=0x{link_off:X} chunk_off=0x{chunk_off:X}")

    out = bytearray()
    bit_pos = 0x10        # current bitstream word offset
    link_pos = link_off
    chunk_pos = chunk_off
    bits_left = 0
    cur = 0
    steps = 0
    while len(out) != dec_size:
        if len(out) > dec_size or len(out) > max_out:
            print(f"OVERSHOOT at out_len=0x{len(out):X} (expected 0x{dec_size:X}) after {steps} ops")
            return None
        if bits_left == 0:
            cur = struct.unpack_from(">i", blob, bit_pos)[0]
            bit_pos += 4
            bits_left = 32
        if cur < 0:  # literal
            out.append(blob[chunk_pos]); chunk_pos += 1
        else:        # backref
            code = struct.unpack_from(">H", blob, link_pos)[0]
            link_pos += 2
            count = code >> 12
            dist = code & 0xFFF
            if count == 0:
                count = blob[chunk_pos] + 0x12
                chunk_pos += 1
            else:
                count += 2
            src = len(out) - dist
            for _ in range(count):
                out.append(out[src - 1])
                src += 1
        cur = (cur << 1) & 0xFFFFFFFF
        if cur >= 0x80000000:
            cur -= 0x100000000
        bits_left -= 1
        steps += 1
    print(f"OK: decompressed 0x{len(out):X} bytes in {steps} ops")
    return bytes(out)

if __name__ == "__main__":
    rom_path = sys.argv[1] if len(sys.argv) > 1 else "baserom.z64"
    rom_off = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0xB98090
    blob_len = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x2FC0
    rom = open(rom_path, "rb").read()
    blob = rom[rom_off:rom_off + blob_len + 0x40]  # slack in case len is tight
    result = decompress(blob)
    sys.exit(0 if result is not None else 1)
