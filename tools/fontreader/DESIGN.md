# Game font reader - design for the C++ port into AMF (Witcher 3)

Status: the offline proof works end to end (2026-10-05). The Python tools in this folder read the UI font from the
installed game (5.0, Steam), convert it to an in-memory TrueType font, and render it with Pillow. The same TTF also
loads and draws through Dear ImGui 1.90.8's own atlas builder (`imgui_check.cpp`, built against `extern/imgui`).
Nothing in AMF's `src` or `dist` has been changed.

**The licence boundary.** The font belongs to CDPR (a licensed Parachute "PF DIN Text Cond Pro"), so AMF never ships
it, never copies it, and never writes the converted TTF to disk. At start-up AMF reads the font from the player's own
install and keeps the result in memory only. The built TTF's `OS/2.fsType` is set to 2 (restricted licence embedding).

## Files

| File | What it does |
|---|---|
| `w3bundle.py` | Reads the POTATO70 bundle header and TOC, and extracts an entry (read-only). |
| `swffont.py` | Strips the CR2W `.redswf` wrapper, inflates the CFX movie, walks the SWF tags and parses DefineFont3. |
| `swf2ttf.py` | Turns DefineFont3 glyphs into a TrueType font with fontTools, as bytes in memory. |
| `w3font.py` | The CLI. `list` shows every fontlib, its fonts and their coverage. `build --lang XX [--role BoldFont]` writes a TTF and a test PNG to the scratch folder. `time` times each step. It refuses to write inside the game folder or this repo. |
| `imgui_check.cpp` | Loads a TTF from memory through `ImFontAtlas`, reports missing glyphs, times `Build()`, and draws the probe text from the atlas. |

## Which font

The main menu text uses font class **`$NormalFont`**. All five DefineEditText fields in `panel_mainmenu.redswf` say so
(22-24 px). `componentslib.redswf` uses `$NormalFont` almost everywhere, with 2 `$BoldFont` and 2 `$ItalicFont`
fields.

`$NormalFont` is resolved by **`gameplay\gui_new\fonts.xml`**. It is stored uncompressed in `xml.bundle`, and a
byte-identical copy sits in `blob.bundle`. The file maps each text language to a fontlib and a font name:

| Language(s) | fontlib (`gameplay\gui_new\swf\witcher3\`) | NormalFont |
|---|---|---|
| EN, and by `ref="EN"` PL DE FR IT ES BR ESMX CZ HU TR DEBUG | `fonts_en.redswf` | PF Din Text Cond Pro |
| RU, UA | `fonts_ru.redswf`, `fonts_ua.redswf` (byte-identical) | PF Din Text Cond Pro |
| AR | `fonts_ar.redswf` | Arial |
| ZH | `fonts_zh.redswf` | Noto Sans TC Regular |
| CN | `fonts_cn.redswf` | 文鼎UD晶熙黑体G30_D |
| JP | `fonts_jp.redswf` | ＤＦ華康明朝体W5 |
| KR | `fonts_kr.redswf` | 나눔명조 (Bold: 나눔명조 Bold) |

The `<fonts fontlib="gfxfontlib">` default, `gfxfontlib.redswf`, is what the menus import (`ImportAssets2
"gfxfontlib.swf"`, with no named assets). Its regular face matches `fonts_en`'s glyph for glyph (382 of 382 identical).

The `lang` values match what AMF's `WitcherTextLanguage()` already reads from `dx12user.settings` (`TextLanguage=EN`).
Compare them case-insensitively (logic library, 2026-09-17).

### Coverage, as found

PF Din Text Cond Pro, regular:

| Source | Glyphs | Coverage |
|---|---|---|
| `fonts_en` | 383 | Basic Latin 95, Latin-1 96, Latin Ext-A 127, Latin Ext-B 11, General Punctuation 18. Covers PL DE FR IT ES BR CZ HU TR. |
| `fonts_ru` / `fonts_ua` | 1118 | Everything in EN plus Cyrillic 94 (RU and UA complete, including ґєіїЁ), Greek 72, and 526 Private Use glyphs (0xE000-0xF7EE; at least 33 duplicate other glyphs, e.g. old-style figures). |

All 383 EN glyphs are identical in the RU font: the same outlines, advances and 809 kerning pairs. The RU regular
face is therefore a **strict superset** of EN.

The other fonts:

- **Bold:** EN 383, RU 477 (Latin + Cyrillic, no Greek). The bold faces have no kerning.
- **Italic:** EN 383, RU 1119 (706 kerning pairs).
- **CJK and Korean faces are SUBSETS.** They hold only the characters the game's own text uses:
  - CN: 4149 CJK ideographs;
  - ZH: 4082;
  - JP: 2503 kanji, plus 81 hiragana and 84 katakana;
  - KR: 1802 of the 11172 Hangul syllables.

  AMF's own strings can need characters outside these sets.
- **Arabic:** Arial, 2138 glyphs, including the Arabic presentation forms A and B.

### Recommendation for AMF

1. Resolve `$NormalFont` for the game's language through `fonts.xml`, following `ref` attributes.
2. **If the font is PF Din Text Cond Pro, take it from `fonts_ru.redswf`.** The glyphs are the same, and AMF's
   Russian and Ukrainian page strings then draw in the game font even when the game is in English. For the
   bold/italic roles, use the same lib's bold/italic face.
3. For CN / ZH / JP / KR, the game's CJK face is primary, because that is what the game shows. AMF's existing
   **MergeMode** chain fills the gaps with Windows faces: Meiryo/Yu Gothic, Malgun Gothic, YaHei/SimSun. This
   chain already exists in `Renderer.cpp BuildFonts()`; the game face just becomes the first face.
4. For AR, use the game's Arial for the glyphs. ImGui has no Arabic shaping or RTL, so this does not change AMF's
   current Arabic limitation.

## The pipeline, step by step (5.0 formats and offsets as found)

All game paths come from **`GetModuleFileNameW(nullptr)`**, the exe (logic library, 2026-10-05). Under MO2 that path
is merged by the VFS. The plugin's own module path is its real mod folder and would miss the game data. Every file is
opened read-only (`GENERIC_READ`, `FILE_SHARE_READ|FILE_SHARE_WRITE`), and each step is a bounds-checked read of an
in-memory buffer.

### 1. Bundle: `<game>\content\content0\bundles\{xml,r4gui}.bundle`

The header is 0x20 bytes:

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | char[8] | `"POTATO70"` |
| 0x08 | u32 | bundle file size (77,613,232 for r4gui) |
| 0x0C | u32 | 0 |
| 0x10 | u32 | TOC byte size. The entry count is `tocSize / 0x130`: 0x69B0, so 89 entries in r4gui. |
| 0x14 | 12 bytes | u16 5, u32 data start (= 0x20 + tocSize), padding. Not needed. |

The TOC starts at 0x20, one 0x130-byte entry per file:

| Offset | Type | Meaning |
|---|---|---|
| 0x000 | char[0x100] | path, NUL-padded, backslashes, lower case (`gameplay\gui_new\swf\witcher3\fonts_ru.redswf`) |
| 0x100 | u8[16] | hash. Not MD5 of either the stored or the uncompressed data. Unused. |
| 0x110 | u32 | data offset, absolute, 16-byte aligned |
| 0x114 | u32 | 0 (the high half of a u64 offset in practice) |
| 0x118 | u32 | uncompressed size |
| 0x11C | u32 | stored (compressed) size |
| 0x120 | u32 | CRC32 of the **uncompressed** data (matched on every entry extracted) |
| 0x124 | u32 | compression: 0 none, 1 zlib (78 DA), 2 snappy, 3 doboz, 4/5 lz4 |
| 0x128 | u8[8] | 0 |

The pre-5.0 entry was 0x140 bytes, with a u64 timestamp and 16 more zero bytes. 5.0 dropped them: the entry is now
**0x130**. A reader that hard-codes 0x140 breaks.

Cost: 1 seek and 27 KB to read the TOC, then one seek and read of the entry. `fonts_ru.redswf` is stored as 288,597
bytes and inflates to 289,661.

### 2. The `.redswf` wrapper (CR2W)

`"CR2W"` with one `CSwfResource`. Its properties include `linkageName` and `fonts: array:2,0,SSwfFontDesc{fontName,
numGlyphs, italic}`, which lists the font names and glyph counts. That list is useful only for a log line. The movie is
a raw buffer near the end, at 0x1F6-0x259 in the font libs:

```
u32  movieByteCount                 // font libs: the file ends 4 bytes after the movie
"CFX" u8 version(15) u32 uncompressedLength(incl. the 8-byte header)
zlib stream (78 DA)                 // "CFX" = Scaleform's compressed GFX = SWF "CWS" with another signature
```

Find the movie by scanning for `CFX`/`GFX`/`CWS`/`FWS` and accepting the first hit that meets all of these:

- the u32 in front of it is > 8 and fits in the file;
- the version is 6..40;
- the zlib stream inflates to at least `uncompressedLength - 8`.

This works for the font libs and for menus that carry more data after the movie, such as `panel_mainmenu.redswf`.
Inflate exactly `uncompressedLength - 8` bytes. Python's inflate returned more, page-padded; ignore anything past
the declared length.

A full CR2W parse (name table, export table, property walk) is possible, but the signature scan is cheaper and
survived every file tried. Keep the CR2W magic check as the first gate.

### 3. The SWF/GFX body

- `RECT` frame size: nbits = 15, so 9 bytes, (0,11000,0,8000) twips.
- u16 frame rate (0x1800 = 24.0), then u16 frame count (1).
- Tags: `u16 codeAndLength` (code = >>6, length = &0x3F; 0x3F means a u32 length follows).

Tags seen in the font libs:

- **1000** Scaleform ExporterInfo: u16 version 0x0401 (GFx 4.1), u32 flags **0** (glyph shapes NOT stripped, no font
  textures), u16 0x0E, then the prefix and swf name strings;
- 69 FileAttributes;
- 9 SetBackgroundColor;
- 86 DefineSceneAndFrameLabelData;
- **75 DefineFont3** (2-3 per lib);
- 37 DefineEditText, 74 CSMTextSettings, 26 PlaceObject2;
- 82 DoABC and 76 SymbolClass (gfxfontlib only);
- 1 ShowFrame, 0 End.

There is no DefineFont2, DefineFontName, DefineFontAlignZones or Scaleform DefineCompactedFont (1005). If a patch
introduces 1005, it is a different, packed format: fail soft.

### 4. DefineFont3 (tag 75)

```
u16 fontId | u8 flags | u8 language | u8 nameLen | name[nameLen] (UTF-8, may hold a trailing NUL)
u16 numGlyphs
OffsetTable[numGlyphs]  u32 (flags & 0x08 WideOffsets - set in every font here) else u16, relative to the table start
u32/u16 CodeTableOffset  (same base)
GlyphShapeTable: numGlyphs x SHAPE
CodeTable[numGlyphs]     u16 (flags & 0x04 WideCodes - always for Font3): UCS-2 code point per glyph, ascending
if (flags & 0x80 HasLayout):
  u16 ascent, u16 descent, s16 leading     (20480-unit EM)
  s16 advance[numGlyphs]
  RECT bounds[numGlyphs]                   (each byte-aligned; ALL ZERO in these fonts - compute from the outline)
  u16 kerningCount, then {u16 code1, u16 code2, s16 adjustment}
```

The flags seen are 0x8C (regular), 0x8D (+Bold 0x01) and 0x8E (+Italic 0x02). Pick the face by name, bold flag and
italic flag; `fonts.xml` gives the bold/italic attributes.

**SHAPE:**

1. u4 numFillBits (1), u4 numLineBits (0), then bit-packed records until EndShape.
2. **StyleChange** (`0` + 5 flag bits; all zero = EndShape):
   - MoveTo (0x01): u5 n, sN x, sN y, absolute;
   - FillStyle0 (0x02): uF;
   - FillStyle1 (0x04): uF;
   - LineStyle (0x08): uL;
   - NewStyles (0x10) never appears in a glyph; treat it as an error.
3. **Edge** (`1`):
   - StraightFlag, u4 (n - 2);
   - straight: GeneralLine ? (dx, dy) : VertLine ? dy : dx;
   - curved: control dx, dy, then anchor dx, dy, all relative.
4. Only a MoveTo starts a new contour. Every contour here is closed and uses fillStyle0 = 1. Outer contours and holes
   have opposite winding, so the fill side does not decide orientation; the winding does.

### 5. Outline -> TrueType glyph

- Units: EM = 20480 and y points down, with the baseline at 0. TTF: `unitsPerEm = 2048`, `x' = round(x/10)`,
  `y' = round(-y/10)`.
- A SWF curve is a TrueType quadratic segment: the control point is off-curve and the anchor is on-curve. A straight
  edge is an on-curve point. **The conversion is exact; no curves are approximated.**
- Orientation: after the y flip an outer contour runs counter-clockwise. **Reverse every contour** to get TrueType's
  clockwise outers. stb_truetype's non-zero fill would draw it either way, because only the relative winding matters,
  but the reversed form is spec-correct for any other consumer. Drop the closing point when it repeats the start.
- Metrics: hmtx advance = advance/10; lsb = the glyph's xMin. hhea and OS/2 ascent/descent/lineGap come from
  DefineFont3's ascent, descent and leading /10 (PF Din regular: 1810 / 424 / 186).
- Composite glyphs: none. Code points above 0xFFFF: none (Font3 codes are 16-bit), so **cmap format 4 alone is
  enough**.

### 6. The minimal TTF writer (C++)

**Tables:** `head`, `hhea`, `maxp` (v0.5 is enough for stb; write v1.0 for other loaders), `OS/2`, `hmtx`, `cmap`
(one (3,1) format-4 subtable), `loca` (long format), `glyf`, `name`, `post` (format 3).

- stb_truetype REQUIRES `cmap, loca, head, glyf, hhea, hmtx` (`stbtt_InitFont_internal`). It reads `maxp` for the
  glyph count, and `head+50` for the loca format. It accepts a cmap from platform 3 encoding 1/10, or from platform 0.
- `kern` is optional. **ImGui 1.90.8 applies no kerning**, so the C++ writer can leave it out. The proof's TTF
  includes it for Pillow.
- Table checksums are ignored by stb, but they cost nothing to compute, so write them.

**The glyf record per glyph:** numberOfContours, the bbox, endPtsOfContours, instructionLength 0, flags (bit 0 =
on-curve; no packing needed, one flag byte per point is valid), then x and y as int16 deltas. It is about 250 lines,
with no dependency.

**Validate before ImGui sees it.** A TTF that `stbtt_InitFont` rejects makes `ImFontAtlas::Build()` return false for
the WHOLE atlas, and asserts in debug builds (`imgui_draw.cpp:2803`). AMF must therefore:

1. call `stbtt_InitFont` itself (imstb_truetype.h is already in `extern/imgui`);
2. check that `stbtt_FindGlyphIndex` finds 'A' and that `stbtt_GetGlyphBox` gives a non-empty box;
3. only then call `AddFontFromMemoryTTF`.

### 7. Into ImGui

- Keep the TTF in a static `std::vector<uint8_t>` for the session and pass `ImFontConfig.FontDataOwnedByAtlas =
  false`. The atlas is rebuilt on every text-size change, so the bytes must outlive each rebuild, and they are built
  only once per session.
- Call `AddFontFromMemoryTTF(ttf.data(), size, px, &cfg, s_ranges.Data)` as the first face, before the existing
  `sFontPath` / Windows candidates. Then the existing MergeMode chain runs unchanged.
- `sFontPath` keeps priority. A new INI switch, `bGameFont=1`, lets the player turn the game font off.
- Proof: `imgui_check.exe` loaded `w3ui_RU_NormalFont.ttf` through `AddFontFromMemoryTTF` and `Build()` at 48 px: OK
  in **1.6 ms**, atlas 512x1024, 289 glyphs, 0 of 77 probe characters missing.

## Start-up cost

These are warm-cache timings, best of 5, from `w3font.py time`. Python parses every face in the lib with a per-bit
reader, so it is slow; the C++ port parses only the face it needs.

| Step | Python EN (ms) | Python RU (ms) | C++ estimate (ms) |
|---|---|---|---|
| fonts.xml (TOC + 2.6 KB, stored) | 0.3 | 0.3 | < 0.2 |
| r4gui.bundle TOC (27 KB) | 0.1 | 0.1 | < 0.2 |
| read + inflate the entry (zlib, ~290 KB) | 0.2 | 0.4 | 0.5 |
| inflate the CFX movie (zlib, ~470 KB) | 0.4 | 1.1 | 1.5 |
| parse tags + glyphs (all faces in the lib) | 115 | 327 | ~1 (one face, ~2.5k contours) |
| build the TTF (fontTools) | 27 | 89 | ~0.5 (one pass, ~200 KB write) |
| **total** | **144** | **419** | **~3-5 warm** |
| ImGui `Build()` (already paid today for any TTF) | | | 1.6 measured |

- A cold start adds at most a few disk reads: one 27 KB TOC and one ~290 KB entry, inside a 77 MB file that is not
  read whole. Expect under 10 ms on an SSD and tens of ms on a spinning disk. This was not measured; the OS cache was
  warm.
- CJK costs more because the faces are bigger. KR: 1951 glyphs, 6727 contours, a 965 KB TTF, 2.2 s in Python. The C++
  estimate is ~10-30 ms, plus a bigger atlas when the glyph ranges include all of Hangul.
- AMF already builds its atlas outside a frame, before NewFrame. The reader runs once, on the first `BuildFonts()`.
  If a CJK build shows up in the frame-time log, it can move to a worker thread started at plugin load, with the
  Windows chain as a stand-in until it lands.

## What has to be vendored

- **An inflater only.** A survey of every 5.0 bundle (all TOCs under `content\`, 365,866 entries) found compression
  types **0 (none, 177,104) and 1 (zlib, 188,762) only**. No snappy, doboz or LZ4 is needed in 5.0. The CFX movie
  inside is zlib too.
- Options, all permissive:
  - **miniz's `tinfl`** (MIT; one .c, ~800 lines, inflate only) - recommended;
  - zlib itself (zlib licence; larger);
  - stb_image's zlib decoder (public domain / MIT; `stbi_zlib_decode_buffer`, but it is buried in stb_image).
  Record the licence in AMF's licence notes either way.
- Do NOT load the `zlib1.dll` that sits in the game folder. It belongs to the REDlauncher, not to the game, and can
  change or disappear.
- Keep the compression switch: on 2-5, log "compression N not supported, keeping AMF's font". If a future patch moves
  the UI bundle to LZ4, LZ4's `lz4.c` is BSD-2 and ~2k lines; doboz is zlib-licensed.
- No font, glyph data or TTF is vendored, and nothing is cached to disk.

## When a patch changes things: fail soft

The rule is that AMF keeps its own font and logs ONE line saying which check failed. It never asserts, throws out of
a frame, or relies on SEH: in Witcher 3 an SEH guard does not make a fault harmless (logic library, 2026-10-05).
Every read is bounds-checked against the buffer; the bit reader carries an end bit and stops there. Each check, in
order, with its log line:

| Check | Log on failure |
|---|---|
| the exe path gives `<game>\content\content0\bundles\r4gui.bundle` | `game font: r4gui.bundle not found at ...` |
| `POTATO70` magic; tocSize % 0x130 == 0; tocSize < file size | `game font: bundle layout changed (magic/toc)` |
| `fonts.xml` found and parsed; the language (case-insensitive, `ref` followed) gives a lib + font name | `game font: no fonts.xml entry for language XX` (then try EN) |
| the lib entry is in the TOC; offset + zsize within the file | `game font: <lib> not in r4gui.bundle` |
| compression is 0 or 1 | `game font: compression N not supported` |
| the inflated size equals the TOC size; CRC32 matches | `game font: <lib> failed size/CRC check` |
| `CR2W` magic; a movie is found by the scan rules in section 2 | `game font: no GFX movie in <lib>` |
| ExporterInfo flags show no stripped shapes (bit tests logged) | `game font: glyph shapes stripped (flags 0x..)` |
| a DefineFont3 with the wanted name/bold/italic exists (fall back to the name only) | `game font: face "<name>" not in <lib> (found: ...)` |
| numGlyphs > 0; the code table holds A-Z, a-z, 0-9; contours close; coordinates fit int16 after /10 | `game font: glyph table implausible (...)` |
| `stbtt_InitFont` accepts the TTF; 'A' has a glyph box | `game font: built TTF rejected by stb_truetype` |

On success, log one line:

```
game font: "PF Din Text Cond Pro" regular from fonts_ru.redswf, 1118 glyphs (Latin, Cyrillic, Greek), 3.4 ms
```

That line is also the DevBench/TestBench probe. Add an `op=font` field such as `gameFont=<name>|off|<reason>`, so the
in-game proof reads the state instead of a capture (logic library, 2026-09-17).

## Languages and the fallback

| Game text language | Primary face (game font) | AMF's fallback for what it lacks |
|---|---|---|
| EN PL DE FR IT ES ESMX BR CZ HU TR RU UA | PF Din Text Cond Pro (from fonts_ru: Latin + Latin Ext-A + Cyrillic + Greek) | the existing Windows chain: Segoe UI, then the CJK merges (only for characters like kana that AMF strings might carry) |
| CN / ZH / JP | the game's CJK face (a subset) | MergeMode: YaHei/SimSun, or Meiryo/Yu Gothic, for every ideograph the subset lacks |
| KR | 나눔명조 (a subset, 1802 syllables) | MergeMode: Malgun Gothic |
| AR | Arial (has Arabic presentation forms) | no change from today; ImGui has no shaping or RTL |
| anything unknown | EN's entry | - |

Glyph ranges stay BUILT, as `BuildFonts()` does today: the defaults, plus the languages in play, plus every character
of the loaded translations.

## Uncertain / not verified

1. **Rendering weight.** The game's main-menu letters look heavier than ours at the same cap height
   (`compare_mainmenu.png` in the scratch folder). The letterforms and widths match: CONTINUE spans ~73 px in both at
   a 14 px cap height. The extra weight is most likely Scaleform's rasteriser or a text filter/shadow in the menu
   movie; it was not investigated. If wanted, a 1 px shadow or a faux-bold double draw in ImGui would get closer.
2. **Mod overrides.** A font-replacer mod would ship `fonts_*.redswf` or `fonts.xml` in `mods\<mod>\content\*.bundle`
   (or DLC bundles). The proof reads only `content\content0`. Following the game's mod priority (mods.settings, then
   alphabetical) is a later step. No such mod was present to test.
3. **Cold-disk timing** was not measured, only warm-cache.
4. **The C++ timings are estimates.** Only ImGui's `Build()` was measured in C++.
5. **The 16-byte TOC hash** is not MD5 of the stored or the uncompressed data. It is unused, so harmless.
6. **The 526 Private Use glyphs** in the RU face are not identified; some duplicate digits (old-style figures?).
   They are harmless: AMF never asks for those code points.
7. **Kerning is lost in ImGui** (ImGui applies none), so pairs such as "AV" or "LT" sit a little wider than in the
   game. The game applies 809 pairs.
8. Arabic shaping and RTL remain unsupported, as they are today.
