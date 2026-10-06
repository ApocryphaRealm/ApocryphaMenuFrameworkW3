"""Read-only reader for The Witcher 3 (Remastered 5.0) .bundle archives.

Format as found in content\\content0\\bundles\\r4gui.bundle (game 5.0, Steam, 2026-10-05):

  header, 0x20 bytes
    0x00 char[8]  "POTATO70"
    0x08 u32      bundle file size
    0x0C u32      0 (dummy size)
    0x10 u32      TOC size in bytes  (entry count = tocSize / 0x130)
    0x14 ...      12 bytes, not needed (u16 5, u32 = data start, padding)
  TOC at 0x20, entries of 0x130 bytes
    0x000 char[0x100] path, NUL-padded, backslashes ("gameplay\\gui_new\\swf\\...")
    0x100 u8[16]      hash (MD5 of the data, not needed)
    0x110 u32         data offset (absolute, 16-byte aligned)
    0x114 u32         0
    0x118 u32         uncompressed size
    0x11C u32         compressed (stored) size
    0x120 u32         CRC32 (not needed)
    0x124 u32         compression: 0 none, 1 zlib, 2 snappy, 3 doboz, 4/5 lz4
    0x128 u8[8]       0

Only open files read-only. Nothing here writes to the game folder.
"""
import os
import struct
import sys
import zlib

MAGIC = b"POTATO70"
HEADER_SIZE = 0x20
ENTRY_SIZE = 0x130
COMP_NAMES = {0: "none", 1: "zlib", 2: "snappy", 3: "doboz", 4: "lz4", 5: "lz4hc"}


class BundleEntry:
    __slots__ = ("name", "offset", "size", "zsize", "crc", "comp")

    def __init__(self, name, offset, size, zsize, crc, comp):
        self.name, self.offset, self.size, self.zsize, self.crc, self.comp = (
            name, offset, size, zsize, crc, comp)


class BundleError(Exception):
    pass


def read_toc(path):
    with open(path, "rb") as f:
        hdr = f.read(HEADER_SIZE)
        if len(hdr) < HEADER_SIZE or hdr[:8] != MAGIC:
            raise BundleError("not a POTATO70 bundle: %r" % hdr[:8])
        bundle_size, _dummy, toc_size = struct.unpack_from("<III", hdr, 8)
        if toc_size % ENTRY_SIZE:
            raise BundleError("TOC size %d is not a multiple of 0x130 - layout changed" % toc_size)
        toc = f.read(toc_size)
    entries = []
    for i in range(toc_size // ENTRY_SIZE):
        e = toc[i * ENTRY_SIZE:(i + 1) * ENTRY_SIZE]
        name = e[:0x100].split(b"\0", 1)[0].decode("ascii", "replace")
        off, _z, size, zsize, crc, comp = struct.unpack_from("<IIIIII", e, 0x110)
        entries.append(BundleEntry(name, off, size, zsize, crc, comp))
    return entries


def decompress(data, comp, size):
    if comp == 0:
        return data
    if comp == 1:
        return zlib.decompress(data)
    if comp in (4, 5):
        import lz4.block
        return lz4.block.decompress(data, uncompressed_size=size)
    if comp == 2:
        import snappy  # python-snappy
        return snappy.uncompress(data)
    raise BundleError("compression %s (%d) not supported" % (COMP_NAMES.get(comp, "?"), comp))


def read_entry(path, entry):
    with open(path, "rb") as f:
        f.seek(entry.offset)
        data = f.read(entry.zsize)
    out = decompress(data, entry.comp, entry.size)
    if len(out) != entry.size:
        raise BundleError("%s: got %d bytes, TOC says %d" % (entry.name, len(out), entry.size))
    if (zlib.crc32(out) & 0xFFFFFFFF) != entry.crc:
        # CRC is over the UNCOMPRESSED data in 5.0 (checked); report, don't fail
        sys.stderr.write("warning: CRC mismatch on %s\n" % entry.name)
    return out


def find(path, name):
    lname = name.lower()
    for e in read_toc(path):
        if e.name.lower() == lname or e.name.lower().endswith("\\" + lname):
            return e
    return None


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="List / extract W3 bundle entries (read-only)")
    ap.add_argument("bundle")
    ap.add_argument("--grep", default="")
    ap.add_argument("--extract", help="entry name (or tail) to extract")
    ap.add_argument("--out", help="output folder for --extract")
    a = ap.parse_args()
    if a.extract:
        e = find(a.bundle, a.extract)
        if not e:
            sys.exit("not found: " + a.extract)
        data = read_entry(a.bundle, e)
        os.makedirs(a.out, exist_ok=True)
        dst = os.path.join(a.out, e.name.split("\\")[-1])
        with open(dst, "wb") as f:
            f.write(data)
        print("wrote", dst, len(data))
    else:
        for e in read_toc(a.bundle):
            if a.grep.lower() in e.name.lower():
                print("%-70s off=0x%08X size=%9d z=%9d %s" % (
                    e.name, e.offset, e.size, e.zsize, COMP_NAMES.get(e.comp, e.comp)))
