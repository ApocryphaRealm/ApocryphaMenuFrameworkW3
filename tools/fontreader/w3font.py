"""Offline proof: read The Witcher 3 (5.0) UI font from the installed game and turn it into a TTF.

  python w3font.py list                 # every fontlib in r4gui.bundle, its fonts and coverage
  python w3font.py build --lang EN      # NormalFont for that language -> TTF + test render (scratch only)
  python w3font.py time --lang EN       # start-up cost, the steps AMF would run, best of N

The game folder is only ever opened read-only. Outputs (extracted .redswf, .ttf, .png) go to the
scratch folder given by --out, never into the repo or dist: the font is CDPR's and is never shipped.
"""
import argparse
import io
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w3bundle  # noqa: E402
import swffont  # noqa: E402

GAME = r"D:\Steam Library\steamapps\common\The Witcher 3"
SCRATCH = (r"C:\Users\liamg\AppData\Local\Temp\claude\D--Claude-output"
           r"\116b3bd2-de6a-417e-a417-15d2fcb5abb2\scratchpad\w3font")
GUI_BUNDLE = r"content\content0\bundles\r4gui.bundle"
XML_BUNDLE = r"content\content0\bundles\xml.bundle"
FONTS_XML = r"gameplay\gui_new\fonts.xml"
DEFAULT_LIB = r"gameplay\gui_new\swf\witcher3\gfxfontlib.redswf"

SCRIPTS = [
    ("Basic Latin", 0x20, 0x7E), ("Latin-1", 0xA0, 0xFF), ("Latin Ext-A", 0x100, 0x17F),
    ("Latin Ext-B", 0x180, 0x24F), ("Greek", 0x370, 0x3FF), ("Cyrillic", 0x400, 0x4FF),
    ("Arabic", 0x600, 0x6FF), ("Arabic Pres-A", 0xFB50, 0xFDFF), ("Arabic Pres-B", 0xFE70, 0xFEFF),
    ("General Punct", 0x2000, 0x206F), ("CJK Symbols", 0x3000, 0x303F), ("Hiragana", 0x3040, 0x309F),
    ("Katakana", 0x30A0, 0x30FF), ("CJK Unified", 0x4E00, 0x9FFF), ("Hangul", 0xAC00, 0xD7AF),
    ("Fullwidth", 0xFF00, 0xFFEF), ("Private Use", 0xE000, 0xF8FF),
]
# characters each of the game's eleven-plus text languages needs beyond Basic Latin
LANG_PROBES = {
    "PL": "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ", "DE": "äöüßÄÖÜ", "FR": "àâçéèêëîïôùûüÿœæÀÇÉÈÊŒ",
    "IT": "àèéìòù", "ES": "áéíñóúü¿¡Ñ", "BR": "ãõçáéíóúâêô", "CZ": "čďěňřšťůžýáéíóúČŘŠŽ",
    "HU": "őűáéíóöúüŐŰ", "TR": "çğıöşüİĞŞ", "RU": "абвгдеёжзийклмнопрстуфхцчшщъыьэюяЁ",
    "UA": "ґєіїҐЄІЇ", "Greek": "αβγδΩ",
}


def gpath(game, rel):
    return os.path.join(game, rel)


def read_fonts_xml(game):
    xb = gpath(game, XML_BUNDLE)
    e = w3bundle.find(xb, FONTS_XML)
    if not e:
        raise RuntimeError("fonts.xml not in xml.bundle")
    text = w3bundle.read_entry(xb, e).decode("utf-8-sig")
    langs = {}
    for m in re.finditer(r'<lang name="([A-Z]+)"([^>]*)>(.*?)</lang>|<lang name="([A-Z]+)" ref="([A-Z]+)"\s*/>',
                         text, re.S):
        if m.group(1):
            attrs = m.group(2)
            lib = re.search(r'fontlib="([^"]+)"', attrs)
            roles = {}
            for r in re.finditer(r'<(\w+Font) font="([^"]+)"([^/]*)/>', m.group(3)):
                roles[r.group(1)] = (r.group(2), "bold=\"1\"" in r.group(3), "italic=\"1\"" in r.group(3))
            langs[m.group(1)] = {"lib": lib.group(1).replace("/", "\\") if lib else DEFAULT_LIB, "roles": roles}
        else:
            langs[m.group(4)] = {"ref": m.group(5)}
    for k, v in list(langs.items()):
        if "ref" in v:
            langs[k] = dict(langs[v["ref"]], ref=v["ref"])
    return langs, text


def coverage(codes):
    s = set(codes)
    out = []
    for name, a, b in SCRIPTS:
        n = sum(1 for c in s if a <= c <= b)
        if n:
            out.append("%s %d" % (name, n))
    return ", ".join(out)


def lang_check(codes):
    s = set(codes)
    res = {}
    for lang, chars in LANG_PROBES.items():
        missing = [ch for ch in chars if ord(ch) not in s]
        res[lang] = "ok" if not missing else "missing " + "".join(missing)
    return res


def load_lib(game, lib_name):
    gb = gpath(game, GUI_BUNDLE)
    e = w3bundle.find(gb, lib_name)
    if not e:
        raise RuntimeError("%s not in r4gui.bundle" % lib_name)
    return swffont.read_fonts(w3bundle.read_entry(gb, e))


def pick(fonts, name, bold, italic):
    for f in fonts:
        if f.get("name") == name and bool(f.get("bold")) == bold and bool(f.get("italic")) == italic:
            return f
    for f in fonts:
        if f.get("name") == name:
            return f
    return None


def cmd_list(a):
    langs, _ = read_fonts_xml(a.game)
    print("fonts.xml languages:")
    for k, v in langs.items():
        nf = v["roles"].get("NormalFont", ("?",))[0]
        print("  %-5s lib=%s NormalFont=%r%s" % (k, v["lib"].split("\\")[-1], nf, " (ref %s)" % v["ref"] if "ref" in v else ""))
    gb = gpath(a.game, GUI_BUNDLE)
    libs = [e for e in w3bundle.read_toc(gb) if re.search(r"\\(fonts_\w+|gfxfontlib)\.redswf$", e.name)]
    for e in libs:
        r = swffont.read_fonts(w3bundle.read_entry(gb, e))
        print("\n%s  (%s v%d, movie at 0x%X, %d bytes)" % (e.name, r["signature"], r["version"], r["movie_offset"], r["declared"]))
        for f in r["fonts"]:
            if "unsupported_tag" in f:
                print("   UNSUPPORTED font tag %d" % f["unsupported_tag"])
                continue
            print("   id %-2d %-28r flags 0x%02X lang %d glyphs %-5d bold %-5s italic %-5s kern %d" % (
                f["id"], f["name"], f["flags"], f["lang"], f["nglyphs"], f["bold"], f["italic"], len(f["kerning"])))
            print("        " + coverage(f["codes"]))
            if f["name"].replace(" ", "").lower().startswith("pfdin"):
                lc = lang_check(f["codes"])
                print("        langs: " + "; ".join("%s %s" % (k, v) for k, v in lc.items()))


def build(game, lang, role="NormalFont"):
    """The steps AMF runs at start-up. Returns (ttf_bytes, font dict, timings)."""
    import swf2ttf
    t = {}
    t0 = time.perf_counter()
    langs, _ = read_fonts_xml(game)
    t["fonts.xml"] = time.perf_counter() - t0
    info = langs.get(lang) or langs["EN"]
    name, bold, italic = info["roles"][role]
    t1 = time.perf_counter()
    gb = gpath(game, GUI_BUNDLE)
    e = w3bundle.find(gb, info["lib"])
    t["toc"] = time.perf_counter() - t1
    t1 = time.perf_counter()
    raw = w3bundle.read_entry(gb, e)
    t["read+inflate entry"] = time.perf_counter() - t1
    t1 = time.perf_counter()
    o, body, ver, sig, declared = swffont.find_movie(raw)
    t["inflate CFX"] = time.perf_counter() - t1
    t1 = time.perf_counter()
    r = swffont.read_fonts(raw)  # (inflates again; timing above isolates that cost)
    f = pick(r["fonts"], name, bold, italic)
    t["parse tags+glyphs"] = time.perf_counter() - t1 - t["inflate CFX"]
    t1 = time.perf_counter()
    ttf, cmap, stats = swf2ttf.build_ttf(f, style=("Bold " if bold else "") + ("Italic" if italic else "") or "Regular")
    t["build TTF"] = time.perf_counter() - t1
    t["total"] = time.perf_counter() - t0
    return ttf, f, t, stats, info["lib"]


def cmd_build(a):
    from PIL import Image, ImageDraw, ImageFont
    ttf, f, t, stats, lib = build(a.game, a.lang, a.role)
    os.makedirs(a.out, exist_ok=True)
    tag = "%s_%s" % (a.lang, a.role)
    ttf_path = os.path.join(a.out, "w3ui_%s.ttf" % tag)
    with open(ttf_path, "wb") as fh:
        fh.write(ttf)
    print("font %r from %s: %s" % (f["name"], lib, stats))
    print("timings ms: " + ", ".join("%s %.1f" % (k, v * 1000) for k, v in t.items()))

    # validate with fontTools' own reader and list what a TrueType loader needs
    from fontTools.ttLib import TTFont
    tt = TTFont(io.BytesIO(ttf))
    print("tables:", sorted(tt.keys()))
    print("cmap subtables:", [(s.platformID, s.platEncID, s.format) for s in tt["cmap"].tables])
    missing = [ch for ch in a.text if ch != " " and ord(ch) not in tt.getBestCmap()]
    print("missing from cmap for the test text:", "".join(missing) or "none")

    font = ImageFont.truetype(io.BytesIO(ttf), 48)  # in memory, as AMF would
    lines = [a.text[i:i + 40] for i in range(0, len(a.text), 40)] if a.wrap else [a.text]
    lines = a.text.split("|") if "|" in a.text else lines
    w = max(int(font.getlength(s)) for s in lines) + 40
    h = 64 * len(lines) + 30
    img = Image.new("RGB", (w, h), (10, 12, 14))
    d = ImageDraw.Draw(img)
    for i, s in enumerate(lines):
        d.text((20, 15 + 64 * i), s, font=font, fill=(235, 235, 235))
    png = os.path.join(a.out, "w3ui_%s_test.png" % tag)
    img.save(png)
    print("wrote", ttf_path)
    print("wrote", png)


def cmd_time(a):
    best = None
    for _ in range(a.n):
        _, _, t, _, _ = build(a.game, a.lang, a.role)
        best = t if best is None else {k: min(best[k], t[k]) for k in t}
    print("best of %d, ms: %s" % (a.n, ", ".join("%s %.1f" % (k, v * 1000) for k, v in best.items())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=GAME)
    ap.add_argument("--out", default=SCRATCH)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    b = sub.add_parser("build")
    b.add_argument("--lang", default="EN")
    b.add_argument("--role", default="NormalFont")
    b.add_argument("--text", default="CONTINUE NEW GAME LOAD GAME|Apocrypha Menu Framework|0123456789 Zażółć Привет")
    b.add_argument("--wrap", action="store_true")
    tm = sub.add_parser("time")
    tm.add_argument("--lang", default="EN")
    tm.add_argument("--role", default="NormalFont")
    tm.add_argument("-n", type=int, default=10)
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    repo = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    for forbidden, why in ((a.game, "the game folder"), (repo, "the AMF repo (the font is CDPR's, never shipped)")):
        f = os.path.normcase(os.path.abspath(forbidden)).rstrip(os.sep) + os.sep
        if (os.path.normcase(out).rstrip(os.sep) + os.sep).startswith(f):
            sys.exit("refusing to write inside " + why)
    {"list": cmd_list, "build": cmd_build, "time": cmd_time}[a.cmd](a)


if __name__ == "__main__":
    main()
