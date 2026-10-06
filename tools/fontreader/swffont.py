"""Strip CDPR's .redswf (CR2W CSwfResource) wrapper and parse the Scaleform GFX movie's fonts.

Wrapper, as found in 5.0's gameplay\\gui_new\\swf\\witcher3\\*.redswf:
  "CR2W" resource holding one CSwfResource (linkageName, fonts: array of SSwfFontDesc{fontName,
  numGlyphs, italic}, ...). The movie itself is a raw byte buffer at the end:
      u32 movieSize   (bytes of the movie; the file ends 4 bytes after it)
      "CFX" u8 version(15) u32 uncompressedLength   then a zlib stream
  "CFX" is Scaleform's compressed GFX = SWF "CWS" with a different signature. "GFX" would be the
  uncompressed form, "FWS"/"CWS" plain SWF; all four are accepted. The movie is located by scanning
  for the signature and validating the u32 length prefix in front of it, not by fixed offset.

Inside: ordinary SWF tags. Fonts are DefineFont3 (tag 75). The other tags seen: 1000 Scaleform
ExporterInfo, 69 FileAttributes, 9 SetBackgroundColor, 86 SceneAndFrameLabels, 37 DefineEditText,
74 CSMTextSettings, 26 PlaceObject2, 82 DoABC, 76 SymbolClass, 1 ShowFrame, 0 End.
"""
import struct
import zlib

SIGS = (b"CFX", b"GFX", b"CWS", b"FWS")


class SwfError(Exception):
    pass


def find_movie(redswf):
    """Return (offset, movie body without the 8-byte header, version, signature, declared length).

    The movie is found by its signature, a u32 length prefix in front of it that fits inside the
    file, a plausible version byte, and (for the compressed forms) a zlib stream that inflates to at
    least the declared length. Font libraries end 4 bytes after the movie; menu movies such as
    panel_mainmenu.redswf carry more data after it, so the prefix is not checked against the file end.
    """
    if redswf[:4] != b"CR2W":
        raise SwfError("not a CR2W resource")
    for sig in SIGS:
        start = 0
        while True:
            o = redswf.find(sig, start)
            if o < 0:
                break
            start = o + 1
            if o < 4:
                continue
            pre = struct.unpack_from("<I", redswf, o - 4)[0]
            if not (8 < pre <= len(redswf) - o):
                continue
            ver = redswf[o + 3]
            if not (6 <= ver <= 40):
                continue
            declared = struct.unpack_from("<I", redswf, o + 4)[0]
            if sig in (b"CFX", b"CWS"):
                try:
                    body = zlib.decompressobj().decompress(redswf[o + 8:o + pre])
                except zlib.error:
                    continue
            else:
                body = redswf[o + 8:o + min(pre, declared)]
            if len(body) < declared - 8:
                continue
            return o, body[:declared - 8], ver, sig.decode(), declared
    raise SwfError("no GFX/SWF movie with a valid length prefix found")


class Bits:
    __slots__ = ("d", "pos")

    def __init__(self, d, byte_pos):
        self.d = d
        self.pos = byte_pos * 8

    def u(self, n):
        v = 0
        d = self.d
        for _ in range(n):
            v = (v << 1) | ((d[self.pos >> 3] >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def s(self, n):
        if n == 0:
            return 0
        v = self.u(n)
        if v & (1 << (n - 1)):
            v -= 1 << n
        return v

    def align(self):
        self.pos = (self.pos + 7) & ~7

    @property
    def byte(self):
        return (self.pos + 7) >> 3


def iter_tags(body):
    b = Bits(body, 0)
    nb = b.u(5)
    b.u(nb * 4)
    p = b.byte + 4  # frame rate, frame count
    while p + 2 <= len(body):
        cl = struct.unpack_from("<H", body, p)[0]
        p += 2
        code, ln = cl >> 6, cl & 0x3F
        if ln == 0x3F:
            ln = struct.unpack_from("<I", body, p)[0]
            p += 4
        yield code, p, ln
        p += ln
        if code == 0:
            break


def parse_shape(d, pos, end):
    """SWF SHAPE for a glyph -> list of contours [(fill_side, [ (kind, pts) ... ], start)].

    Each contour: dict(start=(x,y), segs=[('L',(x,y)) | ('Q',(cx,cy),(x,y))], fill0, fill1).
    Coordinates are absolute, in DefineFont3 units (EM = 20480), SWF y-down.
    """
    b = Bits(d, pos)
    nfill = b.u(4)
    nline = b.u(4)
    x = y = 0
    fill0 = fill1 = 0
    contours = []
    cur = None
    while True:
        if b.u(1) == 0:  # non-edge
            flags = b.u(5)
            if flags == 0:
                break
            if flags & 0x01:
                n = b.u(5)
                x = b.s(n)
                y = b.s(n)
            if flags & 0x02:
                fill0 = b.u(nfill)
            if flags & 0x04:
                fill1 = b.u(nfill)
            if flags & 0x08:
                b.u(nline)
            if flags & 0x10:
                raise SwfError("NewStyles in a glyph shape")
            if flags & 0x01 or cur is None:
                # only a MoveTo starts a contour; a bare style change continues the pen path
                cur = {"start": (x, y), "segs": [], "fill0": fill0, "fill1": fill1}
                contours.append(cur)
            else:
                cur["fill0"] = cur["fill0"] or fill0
                cur["fill1"] = cur["fill1"] or fill1
        else:
            if cur is None:
                cur = {"start": (x, y), "segs": [], "fill0": fill0, "fill1": fill1}
                contours.append(cur)
            straight = b.u(1)
            n = b.u(4) + 2
            if straight:
                if b.u(1):  # general line
                    x += b.s(n)
                    y += b.s(n)
                elif b.u(1):  # vertical
                    y += b.s(n)
                else:
                    x += b.s(n)
                cur["segs"].append(("L", (x, y)))
            else:
                cx = x + b.s(n)
                cy = y + b.s(n)
                x = cx + b.s(n)
                y = cy + b.s(n)
                cur["segs"].append(("Q", (cx, cy), (x, y)))
    return [c for c in contours if c["segs"]]


def parse_definefont3(d, p, ln, version=3):
    end = p + ln
    font_id, flags, lang, nlen = struct.unpack_from("<HBBB", d, p)
    q = p + 5
    name = d[q:q + nlen].split(b"\0", 1)[0].decode("utf-8", "replace")
    q += nlen
    nglyphs = struct.unpack_from("<H", d, q)[0]
    q += 2
    wide_off = bool(flags & 0x08)
    otab = q
    fmt = "<I" if wide_off else "<H"
    osz = 4 if wide_off else 2
    offsets = [struct.unpack_from(fmt, d, otab + i * osz)[0] for i in range(nglyphs)]
    code_off = struct.unpack_from(fmt, d, otab + nglyphs * osz)[0] if nglyphs else 0
    shapes = [parse_shape(d, otab + offsets[i], otab + code_off) for i in range(nglyphs)]
    q = otab + code_off
    wide_codes = bool(flags & 0x04)
    csz = 2 if wide_codes else 1
    codes = [struct.unpack_from("<H" if wide_codes else "<B", d, q + i * csz)[0] for i in range(nglyphs)]
    q += nglyphs * csz
    font = {
        "id": font_id, "name": name, "flags": flags, "lang": lang, "nglyphs": nglyphs,
        "bold": bool(flags & 0x01), "italic": bool(flags & 0x02), "has_layout": bool(flags & 0x80),
        "small_text": bool(flags & 0x20), "codes": codes, "shapes": shapes,
        "ascent": None, "descent": None, "leading": None, "advances": None, "bounds": None, "kerning": [],
    }
    if flags & 0x80:
        asc, desc, lead = struct.unpack_from("<HHh", d, q)
        q += 6
        adv = list(struct.unpack_from("<%dh" % nglyphs, d, q))
        q += 2 * nglyphs
        bounds = []
        b = Bits(d, q)
        for _ in range(nglyphs):
            n = b.u(5)
            bounds.append((b.s(n), b.s(n), b.s(n), b.s(n)))
            b.align()
        q = b.byte
        nk = struct.unpack_from("<H", d, q)[0] if q + 2 <= end else 0
        q += 2
        kern = []
        for _ in range(nk):
            if wide_codes:
                c1, c2, a = struct.unpack_from("<HHh", d, q)
                q += 6
            else:
                c1, c2, a = struct.unpack_from("<BBh", d, q)
                q += 4
            kern.append((c1, c2, a))
        font.update(ascent=asc, descent=desc, leading=lead, advances=adv, bounds=bounds, kerning=kern)
    return font


def read_fonts(redswf_bytes):
    o, body, ver, sig, declared = find_movie(redswf_bytes)
    fonts = []
    tags = []
    for code, p, ln in iter_tags(body):
        tags.append(code)
        if code == 75:
            fonts.append(parse_definefont3(body, p, ln))
        elif code in (10, 48, 1005):
            # DefineFont / DefineFont2 / Scaleform DefineCompactedFont: not present in 5.0. A patch
            # that switches to them must be noticed, not silently skipped.
            fonts.append({"unsupported_tag": code, "name": "?", "nglyphs": 0, "codes": []})
    return {"movie_offset": o, "signature": sig, "version": ver, "declared": declared,
            "body_len": len(body), "tags": tags, "fonts": fonts}
