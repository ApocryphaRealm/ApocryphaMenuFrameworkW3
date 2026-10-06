"""DefineFont3 glyphs (from swffont.py) -> an in-memory TrueType font (bytes), with fontTools.

Mapping (verified on 5.0's PF Din Text Cond Pro):
  * DefineFont3 units: EM = 1024 * 20 = 20480, SWF y-down, baseline at y = 0.
    TTF: unitsPerEm 2048, x' = round(x / 10), y' = round(-y / 10).
  * SWF quadratic edges are exactly TrueType quadratic segments: control point -> off-curve point,
    anchor -> on-curve point. Straight edges -> on-curve points. No approximation.
  * Every glyph contour in the game's fonts is closed and uses fillStyle0 = 1 only; outer
    contours and holes have opposite winding (outer: negative shoelace area in SWF y-down).
    After the y flip an outer contour runs counter-clockwise, so each contour is REVERSED to give
    TrueType's clockwise outer / counter-clockwise hole convention.
  * The BoundsTable is all zeros in these fonts (Scaleform leaves it empty) - glyph bounds are
    computed from the outline (fontTools does this when it compiles glyf).
  * AdvanceTable / Ascent / Descent / Leading / KerningRecords are used as given, /10.
"""
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0
from fontTools.ttLib import newTable
import io

SCALE = 10  # 20480 -> 2048


def _pt(p):
    return (int(round(p[0] / SCALE)), int(round(-p[1] / SCALE)))


def _draw_contour(pen, ct):
    """Reverse the SWF contour and draw it into a TTGlyphPen."""
    segs = []
    for s in ct["segs"]:
        if s[0] == "L":
            segs.append(("L", None, s[1]))
        else:
            segs.append(("Q", s[1], s[2]))
    # forward vertices
    verts = [ct["start"]] + [s[2] for s in segs]
    # reversed: start at the last vertex (== start for a closed contour), walk back
    rev = []
    for i in range(len(segs) - 1, -1, -1):
        kind, ctrl, _to = segs[i]
        rev.append((kind, ctrl, verts[i]))
    start = verts[-1]
    pen.moveTo(_pt(start))
    last = _pt(start)
    for kind, ctrl, to in rev:
        tp = _pt(to)
        if kind == "L":
            if tp != last:
                pen.lineTo(tp)
        else:
            pen.qCurveTo(_pt(ctrl), tp)
        last = tp
    pen.closePath()


def build_ttf(font, family="W3 Game UI", style="Regular"):
    """font: dict from swffont.parse_definefont3. Returns (ttf_bytes, cmap dict, stats)."""
    codes = font["codes"]
    n = font["nglyphs"]
    order = [".notdef"]
    cmap = {}
    names = []
    for i in range(n):
        nm = "g%d" % i
        names.append(nm)
        order.append(nm)
        cp = codes[i]
        if cp not in cmap:  # first glyph wins on a duplicated code
            cmap[cp] = nm

    fb = FontBuilder(2048, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)

    glyphs = {}
    p = TTGlyphPen(None)
    # .notdef: a hollow box
    p.moveTo((100, 0)); p.lineTo((100, 1400)); p.lineTo((900, 1400)); p.lineTo((900, 0)); p.closePath()
    p.moveTo((200, 100)); p.lineTo((800, 100)); p.lineTo((800, 1300)); p.lineTo((200, 1300)); p.closePath()
    glyphs[".notdef"] = p.glyph()
    ncont = 0
    for i in range(n):
        pen = TTGlyphPen(None)
        for ct in font["shapes"][i]:
            _draw_contour(pen, ct)
            ncont += 1
        g = pen.glyph()
        glyphs[names[i]] = g
    fb.setupGlyf(glyphs)

    adv = font["advances"] or [20480] * n
    glyf = fb.font["glyf"]
    metrics = {".notdef": (1000, 100)}
    for i in range(n):
        g = glyf[names[i]]
        lsb = getattr(g, "xMin", 0) if g.numberOfContours else 0
        metrics[names[i]] = (max(0, int(round(adv[i] / SCALE))), lsb)
    fb.setupHorizontalMetrics(metrics)

    asc = int(round((font["ascent"] or 18000) / SCALE))
    desc = int(round((font["descent"] or 4000) / SCALE))
    gap = int(round((font["leading"] or 0) / SCALE))
    fb.setupHorizontalHeader(ascent=asc, descent=-desc, lineGap=gap)
    fb.setupNameTable({"familyName": family, "styleName": style,
                       "uniqueFontIdentifier": "%s-%s-from-game-files" % (family, style),
                       "fullName": "%s %s" % (family, style),
                       "psName": (family + "-" + style).replace(" ", "")})
    fsSel = (0x01 if font.get("italic") else 0) | (0x20 if font.get("bold") else 0) or 0x40
    fb.setupOS2(sTypoAscender=asc, sTypoDescender=-desc, sTypoLineGap=gap,
                usWinAscent=asc, usWinDescent=desc, fsSelection=fsSel,
                usWeightClass=700 if font.get("bold") else 400, fsType=0x0002)  # restricted: not for redistribution
    fb.setupPost()
    fb.setupHead(unitsPerEm=2048, macStyle=(1 if font.get("bold") else 0) | (2 if font.get("italic") else 0))

    # Legacy 'kern' format 0 (FreeType/Pillow and stb_truetype both read it; ImGui itself ignores kerning)
    pairs = {}
    for c1, c2, a in font.get("kerning") or []:
        if c1 in cmap and c2 in cmap:
            pairs[(cmap[c1], cmap[c2])] = int(round(a / SCALE))
    if pairs:
        kern = newTable("kern")
        kern.version = 0
        st = KernTable_format_0()
        st.version = 0
        st.coverage = 1
        st.format = 0
        st.kernTable = pairs
        kern.kernTables = [st]
        fb.font["kern"] = kern

    buf = io.BytesIO()
    fb.font.save(buf)
    data = buf.getvalue()
    return data, cmap, {"glyphs": n, "contours": ncont, "kern_pairs": len(pairs), "bytes": len(data)}
