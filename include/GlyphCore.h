#pragma once
// GLYPH COVERAGE (Skyrim 2.2.0 - a Simplified Chinese player's report, 2026-10-13: "AMF can not correctly display, while
// SMF is OK", Chinese on other mods' pages garbled or partly missing with a full CJK font picked). The font was never the
// problem; the atlas's character list was. Chinese asked ImGui for its 2,500-hanzi Simplified "common" set only, so every
// Traditional form and most of Simplified drew as "?", and a mod page's own strings were in no list at all. The Witcher 3
// build had the same list (1.0.6) - and The Witcher 3 ships a Traditional Chinese text language (TextLanguage=ZH) beside
// the Simplified one (CN), both read as "chinese" by strings::GameLanguage.
//
// The rules, in one place so the off-game check (tools/glyph_coverage_check.cpp) runs the SAME code the renderer does:
//   * ALWAYS in the atlas, whatever the language: Latin-1, Latin Extended-A and -B (Polish, Czech, Romanian...), Cyrillic
//     and its supplement, general punctuation, currency signs, letterlike symbols, arrows.
//   * The scripts of AMF's language AND the game's text language (dx12user.settings TextLanguage), FULL where the atlas
//     allows: Chinese the whole CJK unified block (Simplified and Traditional), Japanese kana plus the whole block, Korean
//     every Hangul syllable and jamo. A CJK translation installed under another language's name (Chinese files named
//     _english) brings its own script.
//   * Every character of the loaded translations.
//   * ON DEMAND: every character a page actually drew that the atlas lacked (the glyph-miss hook in the vendored ImGui,
//     extern/imgui, "[AMF] glyph misses"), added in place at the next frame start - so a page's text gets its glyphs
//     whatever the language settings say.
//   * THE ATLAS MUST FIT: a D3D12 Texture2D side is at most 16384 (D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION), and the 1.90.8
//     DX12 backend does not check its font texture's CreateCommittedResource - a failed creation is a crash, not a blank
//     menu. BuildFitted steps down - full sets at 2x horizontal oversampling, full at 1x, the common sets at 1x, then only
//     the characters in use - until the atlas is within kTexelBudget. The on-demand set keeps every drawn character
//     covered at every step.
#include <imgui.h>
#include <imgui_internal.h>   // ImTextCharFromUtf8

#include <algorithm>
#include <string>
#include <vector>

namespace glyphcore
{
	// One step of the atlas fit, largest first.
	struct FitStep
	{
		int level;        // 2 full script sets, 1 ImGui's common sets, 0 only the characters in use
		int oversampleH;  // horizontal oversampling of the text faces
	};
	inline constexpr FitStep kFitSteps[] = { { 2, 2 }, { 2, 1 }, { 1, 1 }, { 0, 1 } };
	inline constexpr int kFitStepCount = static_cast<int>(sizeof(kFitSteps) / sizeof(kFitSteps[0]));

	inline constexpr int kMaxTextureSide = 16384;                    // D3D12 Texture2D limit (the same number as D3D11's)
	inline constexpr long long kTexelBudget = 16LL * 1024 * 1024;    // 64 MB as RGBA - the atlas never grows past this
	inline constexpr int kRangesCapacity = 65544;                     // worst case of BuildRanges over 16-bit codepoints

	inline bool IsChinese(const std::string& a_l) { return a_l == "chinese" || a_l == "schinese" || a_l == "tchinese"; }
	inline bool IsCjk(const std::string& a_l) { return a_l == "japanese" || a_l == "korean" || IsChinese(a_l); }

	// Latin-1, Latin Extended-A/B, Cyrillic + supplement, general punctuation, currency, letterlike, arrows, U+FFFD.
	inline const ImWchar* AlwaysRanges()
	{
		static const ImWchar r[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x0180, 0x024F, 0x0400, 0x052F, 0x2000, 0x206F,
			0x20A0, 0x20CF, 0x2100, 0x214F, 0x2190, 0x21FF, 0xFFFD, 0xFFFD, 0 };
		return r;
	}
	// The text part of a Font Awesome face (icon + label): Latin-1, Latin Extended-A, Cyrillic - plus the on-demand set.
	inline const ImWchar* IconTextRanges()
	{
		static const ImWchar r[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x0400, 0x04FF, 0 };
		return r;
	}
	// CJK punctuation, kana (Japanese, and quoted in Chinese text), full- and half-width forms.
	inline const ImWchar* CjkSymbolRanges()
	{
		static const ImWchar r[] = { 0x3000, 0x30FF, 0x31F0, 0x31FF, 0xFF00, 0xFFEF, 0 };
		return r;
	}
	// The whole CJK unified ideographs block: every hanzi/kanji/hanja in common use, Simplified and Traditional alike.
	inline const ImWchar* CjkIdeographRanges()
	{
		static const ImWchar r[] = { 0x4E00, 0x9FFF, 0 };
		return r;
	}
	// Every precomposed Hangul syllable plus the jamo and compatibility jamo.
	inline const ImWchar* HangulRanges()
	{
		static const ImWchar r[] = { 0x1100, 0x11FF, 0x3130, 0x318F, 0xAC00, 0xD7A3, 0 };
		return r;
	}

	// Decodes UTF-8 into codepoints (BMP only - ImWchar is 16-bit in this build).
	template <class F>
	void ForEachCodepoint(const std::string& a_text, F&& a_f)
	{
		const char* s = a_text.c_str();
		const char* const end = s + a_text.size();
		while (s < end)
		{
			unsigned int c = 0;
			const int n = ImTextCharFromUtf8(&c, s, end);
			if (n <= 0) { break; }
			s += n;
			if (c > 0 && c <= 0xFFFF) { a_f(static_cast<ImWchar>(c)); }
		}
	}

	// The CJK language a text is written in, from its characters: Hangul -> korean, kana -> japanese, hanzi -> chinese.
	inline std::string ScriptOfText(const std::string& a_text)
	{
		bool hangul = false, kana = false, hanzi = false;
		ForEachCodepoint(a_text, [&](ImWchar c) {
			if (c >= 0xAC00 && c <= 0xD7A3) { hangul = true; }
			else if (c >= 0x3040 && c <= 0x30FF) { kana = true; }
			else if (c >= 0x4E00 && c <= 0x9FFF) { hanzi = true; }
		});
		return hangul ? "korean" : kana ? "japanese" : hanzi ? "chinese" : "";
	}

	// The CJK language whose Windows face is merged first: AMF's language when CJK, else the game's, else the text's.
	inline std::string CjkLanguage(const std::string& a_lang, const std::string& a_gameLang, const std::string& a_allText)
	{
		if (IsCjk(a_lang)) { return a_lang; }
		if (IsCjk(a_gameLang)) { return a_gameLang; }
		return ScriptOfText(a_allText);
	}

	// A script's set at a fit level. Level 0 adds nothing - the characters in use and the on-demand set carry the page.
	inline void AddScript(ImFontGlyphRangesBuilder& a_b, ImFontAtlas& a_atlas, const std::string& a_lang, int a_level)
	{
		if (a_level <= 0) { return; }
		const bool full = a_level >= 2;
		if (a_lang == "japanese")
		{
			a_b.AddRanges(a_atlas.GetGlyphRangesJapanese());   // kana + 2,999 common kanji
			a_b.AddRanges(CjkSymbolRanges());
			if (full) { a_b.AddRanges(CjkIdeographRanges()); }
		}
		else if (a_lang == "korean")
		{
			a_b.AddRanges(a_atlas.GetGlyphRangesKorean());     // already every Hangul syllable
			a_b.AddRanges(HangulRanges());
			a_b.AddRanges(CjkSymbolRanges());
		}
		else if (IsChinese(a_lang))
		{
			a_b.AddRanges(full ? a_atlas.GetGlyphRangesChineseFull() : a_atlas.GetGlyphRangesChineseSimplifiedCommon());
			a_b.AddRanges(CjkSymbolRanges());
			if (full) { a_b.AddRanges(CjkIdeographRanges()); }
		}
		else if (a_lang == "thai") { a_b.AddRanges(a_atlas.GetGlyphRangesThai()); }
		else if (a_lang == "vietnamese") { a_b.AddRanges(a_atlas.GetGlyphRangesVietnamese()); }
	}

	// The text face's ranges, written INTO a_out's existing buffer (reserve kRangesCapacity once): every font config
	// holding a_out.Data keeps a valid pointer when the set grows, so the atlas can be rebuilt in place.
	inline void BuildTextRanges(ImVector<ImWchar>& a_out, ImFontAtlas& a_atlas, const std::string& a_lang, const std::string& a_gameLang,
		const std::string& a_allText, const std::vector<ImWchar>& a_onDemand, int a_level)
	{
		ImFontGlyphRangesBuilder b;
		b.AddRanges(AlwaysRanges());
		AddScript(b, a_atlas, a_lang, a_level);
		if (a_gameLang != a_lang) { AddScript(b, a_atlas, a_gameLang, a_level); }
		if (!IsCjk(a_lang) && !IsCjk(a_gameLang)) { AddScript(b, a_atlas, ScriptOfText(a_allText), a_level); }
		b.AddText(a_allText.c_str(), a_allText.c_str() + a_allText.size());
		for (const ImWchar c : a_onDemand) { b.AddChar(c); }
		if (a_out.Capacity < kRangesCapacity) { a_out.reserve(kRangesCapacity); }
		a_out.resize(0);   // NOT clear(): clear() frees the buffer the configs point at
		b.BuildRanges(&a_out);
	}

	// The icon faces' text ranges, in place like BuildTextRanges.
	inline void BuildIconTextRanges(ImVector<ImWchar>& a_out, const std::vector<ImWchar>& a_onDemand)
	{
		ImFontGlyphRangesBuilder b;
		b.AddRanges(IconTextRanges());
		for (const ImWchar c : a_onDemand) { b.AddChar(c); }
		if (a_out.Capacity < kRangesCapacity) { a_out.reserve(kRangesCapacity); }
		a_out.resize(0);
		b.BuildRanges(&a_out);
	}

	// Does a set of ranges hold anything a Latin/Cyrillic face does not (so a Windows CJK face is worth merging)?
	inline bool NeedsFallbackFace(const ImWchar* a_ranges)
	{
		for (const ImWchar* r = a_ranges; r && r[0]; r += 2)
		{
			if (r[1] >= 0x0530 && !(r[0] >= 0x2000 && r[1] <= 0x21FF) && !(r[0] == 0xFFFD)) { return true; }
		}
		return false;
	}

	// The Windows faces merged in for what the chosen face lacks, preferred face first (the owner's order: Japanese,
	// Korean, Chinese), then every CJK/Hangul face Windows ships, so a missing preferred face still finds glyphs.
	inline std::vector<std::string> FallbackFaces(const std::string& a_cjkLang)
	{
		std::vector<std::string> v;
		if (a_cjkLang == "japanese") { v = { "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/YuGothM.ttc" }; }
		else if (a_cjkLang == "korean") { v = { "C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/malgunbd.ttf" }; }
		else { v = { "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc" }; }
		for (const char* f : { "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/malgun.ttf",
				 "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/msgothic.ttc", "C:/Windows/Fonts/simsun.ttc" })
		{
			v.emplace_back(f);
		}
		return v;
	}

	// The Windows faces that can draw a character, by its script, preferred first. Used AFTER a build for the characters
	// known to be drawn (the translations, the on-demand set) that the face and its merges still lack: a picked CJK face
	// with no Cyrillic, a Chinese face asked for Hangul, a Japanese face asked for a Simplified-only hanzi.
	inline const std::vector<std::string>& FacesForScriptOf(ImWchar a_c)
	{
		static const std::vector<std::string> hangul = { "C:/Windows/Fonts/malgun.ttf" };
		static const std::vector<std::string> kana = { "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/msgothic.ttc" };
		static const std::vector<std::string> han = { "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/msjh.ttc", "C:/Windows/Fonts/simsun.ttc",
			"C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc" };
		static const std::vector<std::string> other = { "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/seguisym.ttf" };
		if ((a_c >= 0x1100 && a_c <= 0x11FF) || (a_c >= 0x3130 && a_c <= 0x318F) || (a_c >= 0xAC00 && a_c <= 0xD7A3)) { return hangul; }
		if ((a_c >= 0x3040 && a_c <= 0x30FF) || (a_c >= 0x31F0 && a_c <= 0x31FF)) { return kana; }
		if ((a_c >= 0x2E80 && a_c <= 0x303F) || (a_c >= 0x3400 && a_c <= 0x4DBF) || (a_c >= 0x4E00 && a_c <= 0x9FFF) ||
			(a_c >= 0xF900 && a_c <= 0xFAFF) || (a_c >= 0xFF00 && a_c <= 0xFFEF)) { return han; }
		return other;
	}

	// The faces to merge for a set of still-missing characters: per script that has any, the first face that exists and is
	// not yet merged into that font. One pass - the caller rebuilds once, so a character no Windows face has costs one
	// rebuild, never a loop.
	template <class Exists>
	std::vector<std::string> FacesToAdd(const std::vector<ImWchar>& a_missing, const std::vector<std::string>& a_merged, Exists&& a_exists)
	{
		std::vector<std::string> out;
		std::vector<const std::vector<std::string>*> seen;
		for (const ImWchar c : a_missing)
		{
			const std::vector<std::string>& faces = FacesForScriptOf(c);
			if (std::find(seen.begin(), seen.end(), &faces) != seen.end()) { continue; }
			seen.push_back(&faces);
			for (const std::string& f : faces)
			{
				if (std::find(a_merged.begin(), a_merged.end(), f) != a_merged.end()) { continue; }
				if (std::find(out.begin(), out.end(), f) != out.end()) { break; }
				if (a_exists(f)) { out.push_back(f); break; }
			}
		}
		return out;
	}

	// The characters of a_text and a_extra a font has no glyph for (control characters and line separators aside).
	inline std::vector<ImWchar> MissingIn(const ImFont* a_font, const std::string& a_text, const std::vector<ImWchar>& a_extra)
	{
		std::vector<ImWchar> out;
		std::vector<bool> seen(0x10000, false);
		auto test = [&](ImWchar c) {
			if (c < 0x20 || c == 0x2028 || c == 0x2029 || c == 0x7F || seen[c]) { return; }
			seen[c] = true;
			if (!a_font->FindGlyphNoFallback(c)) { out.push_back(c); }
		};
		ForEachCodepoint(a_text, test);
		for (const ImWchar c : a_extra) { test(c); }
		return out;
	}

	inline bool Fits(const ImFontAtlas& a_atlas)
	{
		return a_atlas.TexWidth > 0 && a_atlas.TexHeight > 0 && a_atlas.TexWidth <= kMaxTextureSide && a_atlas.TexHeight <= kMaxTextureSide &&
		       static_cast<long long>(a_atlas.TexWidth) * a_atlas.TexHeight <= kTexelBudget;
	}

	struct FitResult
	{
		int step = 0;
		int width = 0, height = 0, attempts = 0;
		bool fits = false;
	};

	inline long long CountCodepoints(const ImWchar* a_ranges)
	{
		long long n = 0;
		for (const ImWchar* r = a_ranges; r && r[0]; r += 2) { n += static_cast<long long>(r[1]) - r[0] + 1; }
		return n;
	}

	// A generous estimate of a step's atlas (every codepoint counted, each a full em square with padding): a step whose
	// estimate is over twice the budget is not even tried, so a 4K screen does not rasterise 21,000 hanzi twice for nothing.
	inline long long EstimateTexels(const ImWchar* a_ranges, float a_px, int a_oversampleH)
	{
		const double w = a_px * a_oversampleH + 2.0, h = a_px + 2.0;
		return static_cast<long long>(static_cast<double>(CountCodepoints(a_ranges)) * w * h);
	}

	// Builds the atlas from a_startStep, stepping down until it fits. a_setLevel(level) rewrites the text ranges in place;
	// every config whose ranges ARE a_textRanges (the text face, a picked face, the merged Windows face) gets the step's
	// oversampling. ImFont objects are rebuilt in place - nothing anyone holds is freed. The last step is always built.
	template <class SetLevel>
	FitResult BuildFitted(ImFontAtlas& a_atlas, const ImWchar* a_textRanges, SetLevel&& a_setLevel, int a_startStep, float a_px)
	{
		FitResult r;
		for (int s = a_startStep < 0 ? 0 : a_startStep; s < kFitStepCount; ++s)
		{
			a_setLevel(kFitSteps[s].level);
			if (s + 1 < kFitStepCount && EstimateTexels(a_textRanges, a_px, kFitSteps[s].oversampleH) > 2 * kTexelBudget) { continue; }
			for (ImFontConfig& cfg : a_atlas.ConfigData)
			{
				if (cfg.GlyphRanges == a_textRanges) { cfg.OversampleH = kFitSteps[s].oversampleH; }
			}
			a_atlas.ClearTexData();
			a_atlas.Build();
			r.step = s;
			r.width = a_atlas.TexWidth;
			r.height = a_atlas.TexHeight;
			++r.attempts;
			r.fits = Fits(a_atlas);
			if (r.fits) { break; }
		}
		return r;
	}
}
