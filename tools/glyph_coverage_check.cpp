// Off-game check of include/GlyphCore.h - the same glyph rules BuildFonts runs (1.0.6, from Skyrim 2.2.0's
// tools/glyph_coverage_check: a Simplified Chinese player's report, Chinese on mod pages garbled with a full CJK font
// picked). Built against AMF-W3's own vendored Dear ImGui 1.90.8 (extern/imgui, the xmake "imgui" target), so the atlas
// code is exactly the game's. For each of AMF's eleven languages, with the default face (Segoe UI) and with typical picked
// faces (a Latin face, a Simplified and a Traditional CJK face), at the 1080p size, a 4K size and the largest the
// text-size setting allows, it builds a real atlas the way the renderer does and checks:
//   * every character of AMF's translation file for that language (and of the English file, the fallback) has a glyph;
//   * the script's ranges are there: Latin Extended-A for Polish and Czech, Cyrillic for Russian, every Hangul syllable
//     for Korean, kana and the whole CJK block for Japanese and Chinese (at the full level) - also when only the GAME's
//     text language is CJK (AMF in English on a TextLanguage=ZH / CN / JP / KR game);
//   * the atlas fits: no side over 16384 (the D3D12 limit - the 1.90.8 DX12 backend does not check its font texture's
//     creation, so over it is a crash) and within the 64 MB budget;
//   * the on-demand path: Traditional and rare Simplified hanzi, a rare kanji and a Hangul syllable a page draws get a
//     glyph after the in-place rebuild, and the ranges buffer every font config points at never moves.
//
//   xmake build glyph_coverage_check && xmake run glyph_coverage_check [translations folder]
// Runs from the repo root (set_rundir); the folder defaults to dist/bin/x64_dx12/AMF/Translations. Prints each failure
// and "glyph_coverage_check: N of N passed"; exits 1 on any failure. GLYPH_CHECK_VERBOSE=1 prints every check.
#include "GlyphCore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	bool Verbose()
	{
		char* v = nullptr;
		size_t n = 0;
		const bool on = _dupenv_s(&v, &n, "GLYPH_CHECK_VERBOSE") == 0 && v != nullptr;
		free(v);
		return on;
	}

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		if (!a_ok || Verbose()) { std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n"; }
	}

	// A translation file is UTF-16 LE with a BOM, "$key<TAB>text" per line (strings::ReadInto); returns the values as UTF-8.
	std::string ReadTranslationValues(const std::filesystem::path& a_path)
	{
		std::ifstream f(a_path, std::ios::binary);
		std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		std::string out;
		size_t i = (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) ? 2 : 0;
		bool inValue = false;
		for (; i + 1 < b.size(); i += 2)
		{
			unsigned int c = b[i] | (b[i + 1] << 8);
			if (c >= 0xD800 && c <= 0xDBFF && i + 3 < b.size())   // a surrogate pair - outside the BMP, which ImWchar16 cannot hold
			{
				i += 2;
				c = 0x10000;
			}
			if (c == '\n') { inValue = false; out += '\n'; continue; }
			if (c == '\t') { inValue = true; continue; }
			if (!inValue || c == '\r') { continue; }
			char u[5] = {};
			ImTextCharToUtf8(u, c);
			out += u;
		}
		return out;
	}

	bool RangesHold(const ImWchar* a_r, ImWchar a_lo, ImWchar a_hi)
	{
		for (ImWchar c = a_lo;; ++c)
		{
			bool in = false;
			for (const ImWchar* r = a_r; r[0]; r += 2) { if (c >= r[0] && c <= r[1]) { in = true; break; } }
			if (!in) { return false; }
			if (c == a_hi) { return true; }
		}
	}

	std::string Hex(unsigned int c)
	{
		char b[16];
		std::snprintf(b, sizeof(b), "U+%04X", c);
		return b;
	}

	struct Built
	{
		ImFontAtlas atlas;
		ImVector<ImWchar> ranges;
		ImFont* text = nullptr;
		glyphcore::FitResult fit;
		std::string fallback;
		bool merged = false;
		std::vector<std::string> mergedFaces;
	};

	// FillMissing, as the renderer runs it after a build: the characters known to be drawn that the face still lacks get
	// the Windows face for their script merged in. True when anything was merged (the caller builds once more).
	bool FillMissing(Built& a_b, float a_px, int a_oversampleH, const std::string& a_text, const std::vector<ImWchar>& a_extra)
	{
		const std::vector<ImWchar> missing = glyphcore::MissingIn(a_b.text, a_text, a_extra);
		if (missing.empty()) { return false; }
		bool merged = false;
		for (const std::string& face : glyphcore::FacesToAdd(missing, a_b.mergedFaces, [](const std::string& f) { std::error_code ec; return std::filesystem::exists(f, ec); }))
		{
			a_b.mergedFaces.push_back(face);
			ImFontConfig merge;
			merge.MergeMode = true;
			merge.PixelSnapH = true;
			merge.OversampleH = a_oversampleH;
			merge.DstFont = a_b.text;
			merged = a_b.atlas.AddFontFromFileTTF(face.c_str(), a_px, &merge, a_b.ranges.Data) != nullptr || merged;
		}
		return merged;
	}

	// BuildFonts' text face, in the renderer's order: the face with the text ranges, the Windows fallback merged when the
	// ranges need it, AMF's own icons merged into the text face (Renderer.cpp kOwnIcons), then the fitted build.
	void Build(Built& a_b, const std::string& a_face, float a_px, const std::string& a_lang, const std::string& a_allText,
		const std::vector<ImWchar>& a_onDemand, const std::string& a_ownIcons)
	{
		a_b.atlas.Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
		auto setLevel = [&](int a_level) { glyphcore::BuildTextRanges(a_b.ranges, a_b.atlas, a_lang, a_lang, a_allText, a_onDemand, a_level); };
		setLevel(glyphcore::kFitSteps[0].level);
		a_b.text = a_b.atlas.AddFontFromFileTTF(a_face.c_str(), a_px, nullptr, a_b.ranges.Data);
		if (!a_b.text) { return; }
		for (const std::string& f : glyphcore::FallbackFaces(glyphcore::CjkLanguage(a_lang, a_lang, a_allText)))
		{
			std::error_code ec;
			if (std::filesystem::exists(f, ec)) { a_b.fallback = f; break; }
		}
		if (!a_b.fallback.empty() && glyphcore::NeedsFallbackFace(a_b.ranges.Data))
		{
			ImFontConfig merge;
			merge.MergeMode = true;
			merge.PixelSnapH = true;
			merge.OversampleH = glyphcore::kFitSteps[0].oversampleH;
			merge.DstFont = a_b.text;
			a_b.merged = a_b.atlas.AddFontFromFileTTF(a_b.fallback.c_str(), a_px, &merge, a_b.ranges.Data) != nullptr;
			a_b.mergedFaces.push_back(a_b.fallback);
		}
		std::error_code ec;
		if (!a_ownIcons.empty() && std::filesystem::exists(a_ownIcons, ec))
		{
			static const ImWchar kOwnIcons[] = { 0xF00D, 0xF00D, 0xF023, 0xF023, 0xF054, 0xF054, 0xF05A, 0xF05A, 0xF071, 0xF071,
				0xF11C, 0xF11C, 0xF252, 0xF252, 0xF2EA, 0xF2EA, 0xF304, 0xF304, 0xF53F, 0xF53F, 0 };
			ImFontConfig own;
			own.MergeMode = true;
			own.PixelSnapH = true;
			own.OversampleH = 1;
			own.GlyphMinAdvanceX = static_cast<float>(static_cast<int>(a_px * 0.8f + 0.5f));
			a_b.atlas.AddFontFromFileTTF(a_ownIcons.c_str(), own.GlyphMinAdvanceX, &own, kOwnIcons);
		}
		a_b.fit = glyphcore::BuildFitted(a_b.atlas, a_b.ranges.Data, setLevel, 0, a_px);
		if (FillMissing(a_b, a_px, glyphcore::kFitSteps[a_b.fit.step].oversampleH, a_allText, a_onDemand))
		{
			a_b.fit = glyphcore::BuildFitted(a_b.atlas, a_b.ranges.Data, setLevel, a_b.fit.step, a_px);
		}
	}
}

int main(int argc, char** argv)
{
	const std::filesystem::path tr = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("dist/bin/x64_dx12/AMF/Translations");
	const std::string ownIcons = (tr.parent_path() / "icons" / "fa-solid-900.ttf").string();
	const std::string english = ReadTranslationValues(tr / "ApocryphaMenuFramework_english.txt");
	Check(english.size() > 1000, "the English translation file reads (" + std::to_string(english.size()) + " bytes of values) from " + tr.string());

	const char* const langs[] = { "english", "french", "german", "italian", "spanish", "polish", "czech", "russian", "japanese", "korean", "chinese" };
	// The faces: the default, a Latin face a player might pick, and full CJK faces a player might pick (Simplified-first,
	// Traditional-first). Missing on this machine = skipped, said once.
	const char* const faces[] = { "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/georgia.ttf", "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/msjh.ttc" };
	// 1080p at text size 1; 4K at 1; the largest: 4K (uiScale 2) at the INI's 2.5 (the slider stops at 2).
	const float sizes[] = { 16.0f, 32.0f, 80.0f };

	for (const char* face : faces)
	{
		std::error_code ec;
		if (!std::filesystem::exists(face, ec)) { std::cout << "SKIP " << face << " is not on this machine\n"; }
	}

	// AMF in English on a game whose TextLanguage is CJK (strings::GameLanguage reads ZH and CN as "chinese"): the game's
	// script is in the ranges at the full level, as the renderer passes both languages.
	{
		const std::pair<const char*, ImWchar> gameCases[] = { { "chinese", ImWchar(0x9019) }, { "japanese", ImWchar(0x3042) }, { "korean", ImWchar(0xD55C) } };
		for (const auto& [game, probe] : gameCases)
		{
			ImFontAtlas a;
			ImVector<ImWchar> r;
			glyphcore::BuildTextRanges(r, a, "english", game, english, {}, 2);
			bool in = false;
			for (const ImWchar* p = r.Data; p[0]; p += 2) { if (probe >= p[0] && probe <= p[1]) { in = true; break; } }
			Check(in, std::string("AMF english on a ") + game + " game: the game's script is in the ranges (" + Hex(probe) + ")");
		}
	}

	for (const char* lang : langs)
	{
		const std::string own = ReadTranslationValues(tr / (std::string("ApocryphaMenuFramework_") + lang + ".txt"));
		Check(!own.empty(), std::string(lang) + ": the translation file reads");
		const std::string allText = own + "\n" + english;

		// The ranges at the full level hold the script.
		{
			ImFontAtlas a;
			ImVector<ImWchar> r;
			glyphcore::BuildTextRanges(r, a, lang, lang, allText, {}, 2);
			const std::string L = lang;
			Check(RangesHold(r.Data, 0x0020, 0x00FF), L + ": Latin-1 in the ranges");
			Check(RangesHold(r.Data, 0x0100, 0x017F), L + ": Latin Extended-A in the ranges (Polish, Czech)");
			Check(RangesHold(r.Data, 0x0400, 0x04FF), L + ": Cyrillic in the ranges");
			if (L == "korean") { Check(RangesHold(r.Data, 0xAC00, 0xD7A3), L + ": every Hangul syllable in the ranges"); }
			if (L == "japanese")
			{
				Check(RangesHold(r.Data, 0x3041, 0x3096) && RangesHold(r.Data, 0x30A1, 0x30FA), L + ": hiragana and katakana in the ranges");
				Check(RangesHold(r.Data, 0x4E00, 0x9FFF), L + ": the whole CJK block (all kanji) in the ranges at the full level");
			}
			if (L == "chinese") { Check(RangesHold(r.Data, 0x4E00, 0x9FFF), L + ": the whole CJK block (Simplified and Traditional) in the ranges at the full level"); }
			// every character of the translation is in the ranges at EVERY level, including the last
			for (int level = 2; level >= 0; --level)
			{
				glyphcore::BuildTextRanges(r, a, lang, lang, allText, {}, level);
				int missing = 0;
				std::string first;
				glyphcore::ForEachCodepoint(allText, [&](ImWchar c) {
					if (c < 0x20) { return; }
					bool in = false;
					for (const ImWchar* p = r.Data; p[0]; p += 2) { if (c >= p[0] && c <= p[1]) { in = true; break; } }
					if (!in) { if (!missing++) { first = Hex(c); } }
				});
				Check(missing == 0, L + ": every translation character in the ranges at level " + std::to_string(level) +
					(missing ? " (" + std::to_string(missing) + " missing, first " + first + ")" : ""));
			}
		}

		for (const char* face : faces)
		{
			std::error_code ec;
			if (!std::filesystem::exists(face, ec)) { continue; }
			for (const float px : sizes)
			{
				Built b;
				Build(b, face, px, lang, allText, {}, ownIcons);
				const std::string what = std::string(lang) + ", " + std::filesystem::path(face).filename().string() + ", " + std::to_string(static_cast<int>(px)) + "px";
				Check(b.text != nullptr, what + ": the face loads");
				if (!b.text) { continue; }
				Check(b.fit.fits && b.fit.width <= glyphcore::kMaxTextureSide && b.fit.height <= glyphcore::kMaxTextureSide,
					what + ": the atlas fits (" + std::to_string(b.fit.width) + "x" + std::to_string(b.fit.height) + ", step " +
					std::to_string(b.fit.step + 1) + ", " + std::to_string(b.fit.attempts) + " build(s))");
				int missing = 0;
				std::string first;
				glyphcore::ForEachCodepoint(allText, [&](ImWchar c) {
					if (c < 0x20 || c == 0x2028 || c == 0x2029) { return; }
					if (!b.text->FindGlyphNoFallback(c)) { if (!missing++) { first = Hex(c); } }
				});
				Check(missing == 0, what + ": every translation character has a glyph" +
					(missing ? " (" + std::to_string(missing) + " missing, first " + first + ")" : ""));
				if (std::string(lang) == "polish" || std::string(lang) == "czech")
				{
					Check(b.text->FindGlyphNoFallback(0x0142) && b.text->FindGlyphNoFallback(0x0159) && b.text->FindGlyphNoFallback(0x017E),
						what + ": Latin Extended-A letters (l-stroke, r-caron, z-caron) have glyphs");
				}
				if (std::string(lang) == "russian") { Check(b.text->FindGlyphNoFallback(0x0416) && b.text->FindGlyphNoFallback(0x044F), what + ": Cyrillic has glyphs"); }

				// THE ON-DEMAND PATH, as AddMissedGlyphs does it: the characters join the set, the ranges are rewritten in the
				// same buffer, and the atlas is rebuilt in place from the step it ended on.
				const std::vector<ImWchar> drawn = { 0x9019, 0x88E1, 0x954D, 0x9B31, 0xD7A3, 0x3094 };   // Traditional x2, rare Simplified, rare kanji, Hangul, rare kana
				const ImWchar* const before = b.ranges.Data;
				ImFont* const textBefore = b.text;
				auto setLevel = [&](int a_level) { glyphcore::BuildTextRanges(b.ranges, b.atlas, lang, lang, allText, drawn, a_level); };
				setLevel(glyphcore::kFitSteps[b.fit.step].level);
				if (!b.merged && !b.fallback.empty() && glyphcore::NeedsFallbackFace(b.ranges.Data))
				{
					ImFontConfig merge;
					merge.MergeMode = true;
					merge.PixelSnapH = true;
					merge.OversampleH = glyphcore::kFitSteps[b.fit.step].oversampleH;
					merge.DstFont = b.text;
					b.merged = b.atlas.AddFontFromFileTTF(b.fallback.c_str(), px, &merge, b.ranges.Data) != nullptr;
					b.mergedFaces.push_back(b.fallback);
				}
				glyphcore::FitResult again = glyphcore::BuildFitted(b.atlas, b.ranges.Data, setLevel, b.fit.step, px);
				if (FillMissing(b, px, glyphcore::kFitSteps[again.step].oversampleH, std::string(), drawn))
				{
					again = glyphcore::BuildFitted(b.atlas, b.ranges.Data, setLevel, again.step, px);
				}
				Check(b.ranges.Data == before && b.atlas.Fonts[0] == textBefore, what + ": the ranges buffer and the text face are the same objects after the in-place rebuild");
				Check(again.fits, what + ": the atlas still fits after the on-demand rebuild (" + std::to_string(again.width) + "x" + std::to_string(again.height) + ")");
				int drawnMissing = 0;
				std::string drawnFirst;
				for (const ImWchar c : drawn) { if (!b.text->FindGlyphNoFallback(c)) { if (!drawnMissing++) { drawnFirst = Hex(c); } } }
				Check(drawnMissing == 0, what + ": Traditional and rare hanzi, a rare kanji, a Hangul syllable and a rare kana a page drew have glyphs after the on-demand rebuild" +
					(drawnMissing ? " (first missing " + drawnFirst + ")" : ""));
				if (Verbose())
				{
					std::cout << "     " << what << ": " << b.text->Glyphs.Size << " glyphs, atlas " << again.width << "x" << again.height
							  << ", step " << again.step + 1 << ", fallback " << (b.merged ? b.fallback : "none") << "\n";
				}
			}
		}
	}

	std::cout << "glyph_coverage_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
