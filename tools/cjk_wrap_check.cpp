// Off-game check of the Japanese/Chinese line breaking in the vendored Dear ImGui (1.0.6, from Skyrim 2.1.8's
// amf-cjk-wrap.patch: "[AMF] (Skyrim 2.1.8, a Japanese player's report ...)" in extern/imgui/imgui_draw.cpp). Built against
// AMF-W3's own extern/imgui (the xmake "imgui" target), so the wrap code measured is exactly the game's
// ImFont::CalcWordWrapPositionA. Beside it, OldWrap below is the unpatched 1.90.8 function, copied verbatim, as the
// baseline.
//
// Every text of the shipped translation files is wrapped, paragraph by paragraph (ImGui's own line loop: wrap, skip the
// blanks after the break), at nine widths from 120 to 800 px with a real 16 px face built the way the renderer builds it
// (Segoe UI, the Windows CJK face merged in by GlyphCore - Yu Gothic or Meiryo for Japanese, YaHei for Chinese) and again
// with Yu Gothic / Meiryo as the picked face. It counts, for the patched and the unpatched function:
//   * Japanese and Chinese: breaks where the next line starts with a character the kinsoku rules forbid there (closing
//     punctuation, small kana, the prolonged sound mark...) and where a line ends with an opening bracket - for breaks
//     with no blank between (the wrapper's own CJK break points) and in all;
//     and numbers split across lines ("1." / "00", "1" / "000" with no blank between).
//     PASS: no-blank kinsoku breaks 0 and number splits 0 with the patch.
//   * English and the other languages without CJK: the break positions of the patched and the unpatched function are
//     IDENTICAL, except where the unpatched one split a number (the number rule). PASS: no other difference.
//
//   xmake build cjk_wrap_check && xmake run cjk_wrap_check [translations folder]
// Runs from the repo root (set_rundir); the folder defaults to dist/bin/x64_dx12/AMF/Translations. Exits 1 on any failure.
#include "GlyphCore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	// ---- the unpatched Dear ImGui 1.90.8 ImFont::CalcWordWrapPositionA, verbatim but for the member accesses ----------
	const char* OldWrap(const ImFont* font, float scale, const char* text, const char* text_end, float wrap_width)
	{
		float line_width = 0.0f;
		float word_width = 0.0f;
		float blank_width = 0.0f;
		wrap_width /= scale; // We work with unscaled widths to avoid scaling every characters

		const char* word_end = text;
		const char* prev_word_end = NULL;
		bool inside_word = true;

		const char* s = text;
		IM_ASSERT(text_end != NULL);
		while (s < text_end)
		{
			unsigned int c = (unsigned int)*s;
			const char* next_s;
			if (c < 0x80)
				next_s = s + 1;
			else
				next_s = s + ImTextCharFromUtf8(&c, s, text_end);

			if (c < 32)
			{
				if (c == '\n')
				{
					line_width = word_width = blank_width = 0.0f;
					inside_word = true;
					s = next_s;
					continue;
				}
				if (c == '\r')
				{
					s = next_s;
					continue;
				}
			}

			const float char_width = ((int)c < font->IndexAdvanceX.Size ? font->IndexAdvanceX.Data[c] : font->FallbackAdvanceX);
			if (ImCharIsBlankW(c))
			{
				if (inside_word)
				{
					line_width += blank_width;
					blank_width = 0.0f;
					word_end = s;
				}
				blank_width += char_width;
				inside_word = false;
			}
			else
			{
				word_width += char_width;
				if (inside_word)
				{
					word_end = next_s;
				}
				else
				{
					prev_word_end = word_end;
					line_width += word_width + blank_width;
					word_width = blank_width = 0.0f;
				}

				// Allow wrapping after punctuation.
				inside_word = (c != '.' && c != ',' && c != ';' && c != '!' && c != '?' && c != '\"');
			}

			// We ignore blank width at the end of the line (they can be skipped)
			if (line_width + word_width > wrap_width)
			{
				// Words that cannot possibly fit within an entire line will be cut anywhere.
				if (word_width < wrap_width)
					s = prev_word_end ? prev_word_end : word_end;
				break;
			}

			s = next_s;
		}

		// Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
		// +1 may not be a character start point in UTF-8 but it's ok because caller loops use (text >= word_wrap_eol).
		if (s == text && text < text_end)
			return s + 1;
		return s;
	}

	// The kinsoku sets the patch applies (a copy of AmfCjkNoLineStart / AmfCjkNoLineEnd, which are file-static there).
	bool NoLineStart(unsigned int c)
	{
		switch (c)
		{
		case ')': case ']': case '}': case ',': case '.': case ':': case ';': case '!': case '?': case '%':
		case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0x30FB: case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF01:
		case 0x30FC: case 0xFF70: case 0x2010: case 0x2013: case 0x301C: case 0xFF5E: case 0x30A0:
		case 0x300D: case 0x300F: case 0xFF09: case 0x3011: case 0x3009: case 0x300B: case 0x3015: case 0xFF3D: case 0xFF5D:
		case 0x3019: case 0x3017: case 0xFF60: case 0xFF63: case 0x2019: case 0x201D: case 0x2026: case 0x2025:
		case 0x3005: case 0x303B: case 0x309D: case 0x309E: case 0x30FD: case 0x30FE: case 0xFF05: case 0x2030: case 0x00B0:
		case 0x2103: case 0xFF61: case 0xFF64: case 0xFF65: case 0x203C: case 0x2047: case 0x2048: case 0x2049:
		case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083: case 0x3085: case 0x3087:
		case 0x308E: case 0x3095: case 0x3096:
		case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3: case 0x30E5: case 0x30E7:
		case 0x30EE: case 0x30F5: case 0x30F6:
		case 0xFF67: case 0xFF68: case 0xFF69: case 0xFF6A: case 0xFF6B: case 0xFF6C: case 0xFF6D: case 0xFF6E: case 0xFF6F:
			return true;
		default:
			return c >= 0x31F0 && c <= 0x31FF;
		}
	}

	bool NoLineEnd(unsigned int c)
	{
		switch (c)
		{
		case '(': case '[': case '{':
		case 0x300C: case 0x300E: case 0xFF08: case 0x3010: case 0x3008: case 0x300A: case 0x3014: case 0xFF3B: case 0xFF5B:
		case 0x3018: case 0x3016: case 0xFF5F: case 0xFF62: case 0x2018: case 0x201C:
			return true;
		default:
			return false;
		}
	}

	bool IsDigit(unsigned int c) { return c >= '0' && c <= '9'; }

	// A copy of the patch's AmfCjkBreakable, for the forced-cut classifier below.
	bool Breakable(unsigned int c)
	{
		return (c >= 0x2E80 && c <= 0x2FFF) || (c >= 0x3001 && c <= 0x303F) || (c >= 0x3040 && c <= 0x31FF) || (c >= 0x3200 && c <= 0x4DBF) ||
		       (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF01 && c <= 0xFFEF);
	}

	// May the wrapper break between a and b (adjacent, no blank between)? The rules the patched function applies: after
	// . , ; ! ? " (ImGui's own), unless that splits a number or a kinsoku rule forbids it next to CJK; between two
	// characters when either is CJK, unless a kinsoku rule forbids it. Used only to tell a FORCED cut (a word wider than
	// the whole line, which ImGui cuts anywhere - the patch keeps that rule) from a break the wrapper chose.
	bool MayBreak(unsigned int a_before2, unsigned int a, unsigned int b)
	{
		const bool cjk = Breakable(a) || Breakable(b);
		const bool forbidden = NoLineStart(b) || NoLineEnd(a);
		if (a == '.' || a == ',' || a == ';' || a == '!' || a == '?' || a == '"')
		{
			if ((a == '.' || a == ',') && IsDigit(b) && IsDigit(a_before2)) { return false; }
			return !(cjk && forbidden);
		}
		return cjk && !forbidden;
	}

	bool IsBlank(unsigned int c) { return c == ' ' || c == '\t' || c == 0x3000; }

	// The texts of a translation file (UTF-16 LE with a BOM, "$key<TAB>text"), each as UTF-8, "\n" turned into a line break
	// as strings::ReadInto does.
	std::vector<std::string> ReadTexts(const std::filesystem::path& a_path)
	{
		std::ifstream f(a_path, std::ios::binary);
		std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		std::vector<std::string> out;
		std::u32string line;
		auto flush = [&]() {
			const size_t tab = line.find(U'\t');
			if (!line.empty() && line[0] == U'$' && tab != std::u32string::npos)
			{
				std::string v;
				for (size_t i = tab + 1; i < line.size(); ++i)
				{
					unsigned int c = line[i];
					if (c == '\\' && i + 1 < line.size() && line[i + 1] == U'n') { v += '\n'; ++i; continue; }
					if (c == '\r') { continue; }
					char u[5] = {};
					ImTextCharToUtf8(u, c);
					v += u;
				}
				if (!v.empty()) { out.push_back(v); }
			}
			line.clear();
		};
		for (size_t i = (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) ? 2 : 0; i + 1 < b.size(); i += 2)
		{
			unsigned int c = b[i] | (b[i + 1] << 8);
			if (c >= 0xD800 && c <= 0xDBFF) { i += 2; c = 0xFFFD; }
			if (c == '\n') { flush(); continue; }
			line.push_back(c);
		}
		flush();
		return out;
	}

	unsigned int CharAt(const char* a_s, const char* a_end)
	{
		unsigned int c = 0;
		if (a_s >= a_end) { return 0; }
		ImTextCharFromUtf8(&c, a_s, a_end);
		return c;
	}

	const char* PrevChar(const char* a_begin, const char* a_s)
	{
		if (a_s <= a_begin) { return a_begin; }
		const char* p = a_s - 1;
		while (p > a_begin && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) { --p; }
		return p;
	}

	struct Break
	{
		size_t eol, next;   // offsets in the paragraph: the line ends at eol, the next starts at next (blanks skipped)
	};

	// ImGui's line loop over one paragraph (no '\n'): wrap from s, the line ends there, the blanks after it are skipped.
	template <class Wrap>
	std::vector<Break> Lines(const std::string& a_para, float a_width, Wrap&& a_wrap)
	{
		std::vector<Break> out;
		const char* const begin = a_para.c_str();
		const char* const end = begin + a_para.size();
		const char* s = begin;
		int guard = 0;
		while (s < end && ++guard < 100000)
		{
			const char* eol = a_wrap(s, end, a_width);
			if (eol <= s) { eol = s + 1; }
			if (eol > end) { eol = end; }
			const char* next = eol;
			while (next < end && (*next == ' ' || *next == '\t')) { ++next; }
			if (next < end) { out.push_back({ static_cast<size_t>(eol - begin), static_cast<size_t>(next - begin) }); }
			s = next;
		}
		return out;
	}

	struct Counts
	{
		long long breaks = 0, kinsokuStartNoBlank = 0, kinsokuEndNoBlank = 0, kinsokuStartAll = 0, kinsokuEndAll = 0, numberSplits = 0;
		long long forced = 0;   // of the no-blank breaks above: forced cuts of a word wider than the whole line
		std::string firstStart, firstEnd, firstNumber, firstForced;
	};

	// Whole UTF-8 characters around a break, for the report.
	std::string Around(const std::string& a_para, size_t a_eol, size_t a_next)
	{
		const char* const begin = a_para.c_str();
		const char* l = begin + a_eol;
		for (int i = 0; i < 12 && l > begin; ++i) { l = PrevChar(begin, l); }
		const char* r = begin + a_next;
		const char* const end = begin + a_para.size();
		for (int i = 0; i < 12 && r < end; ++i) { unsigned int c = 0; r += ImTextCharFromUtf8(&c, r, end); }
		return std::string(l, begin + a_eol) + " | " + std::string(begin + a_next, r);
	}

	// Is the break at a_eol (no blank) a forced cut? The "word" around it - out to the nearest blank or break opportunity
	// on each side (MayBreak) - is wider than the whole line, so ImGui cut it anywhere.
	bool ForcedCut(const ImFont* a_font, float a_width, const std::string& a_para, size_t a_eol)
	{
		const char* const begin = a_para.c_str();
		const char* const end = begin + a_para.size();
		auto adv = [&](unsigned int c) { return (int)c < a_font->IndexAdvanceX.Size ? a_font->IndexAdvanceX.Data[c] : a_font->FallbackAdvanceX; };
		const char* l = begin + a_eol;   // extend left
		while (l > begin)
		{
			const char* p = PrevChar(begin, l);
			const unsigned int a = CharAt(p, end), b = CharAt(l, end);
			if (IsBlank(a) || (l < end && MayBreak(CharAt(PrevChar(begin, p), end), a, b) && l != begin + a_eol)) { break; }
			l = p;
		}
		const char* r = begin + a_eol;   // extend right
		while (r < end)
		{
			unsigned int b = 0;
			const int n = ImTextCharFromUtf8(&b, r, end);
			if (IsBlank(b)) { break; }
			if (r != begin + a_eol && r > begin)
			{
				const char* p = PrevChar(begin, r);
				if (MayBreak(CharAt(PrevChar(begin, p), end), CharAt(p, end), b)) { break; }
			}
			r += n;
		}
		float w = 0.0f;
		for (const char* s = l; s < r;) { unsigned int c = 0; s += ImTextCharFromUtf8(&c, s, r); w += adv(c); }
		return w >= a_width;
	}

	void Measure(const std::string& a_para, const std::vector<Break>& a_breaks, Counts& a_c, const ImFont* a_font, float a_width)
	{
		const char* const begin = a_para.c_str();
		const char* const end = begin + a_para.size();
		for (const Break& b : a_breaks)
		{
			++a_c.breaks;
			const bool blank = b.next != b.eol;
			const char* lastP = PrevChar(begin, begin + b.eol);
			const unsigned int last = CharAt(lastP, end);
			const unsigned int first = CharAt(begin + b.next, end);
			const std::string where = Around(a_para, b.eol, b.next) + " @ " + std::to_string(static_cast<int>(a_width)) + "px";
			const unsigned int before = CharAt(PrevChar(begin, lastP), end);
			const bool startBad = NoLineStart(first), endBad = NoLineEnd(last);
			const bool numberBad = !blank && IsDigit(first) && (IsDigit(last) || ((last == '.' || last == ',') && lastP > begin && IsDigit(before)));
			if (startBad) { ++a_c.kinsokuStartAll; }
			if (endBad) { ++a_c.kinsokuEndAll; }
			if (blank || !(startBad || endBad || numberBad)) { continue; }
			if (ForcedCut(a_font, a_width, a_para, b.eol))
			{
				if (!a_c.forced++) { a_c.firstForced = where; }
				continue;
			}
			if (startBad && !a_c.kinsokuStartNoBlank++) { a_c.firstStart = where; }
			if (endBad && !a_c.kinsokuEndNoBlank++) { a_c.firstEnd = where; }
			if (numberBad && !a_c.numberSplits++) { a_c.firstNumber = where; }
		}
	}

	std::vector<std::string> Paragraphs(const std::vector<std::string>& a_texts)
	{
		std::vector<std::string> out;
		for (const std::string& t : a_texts)
		{
			size_t p = 0;
			while (p <= t.size())
			{
				const size_t nl = t.find('\n', p);
				const std::string para = t.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
				if (!para.empty()) { out.push_back(para); }
				if (nl == std::string::npos) { break; }
				p = nl + 1;
			}
		}
		return out;
	}

	struct Face
	{
		ImFontAtlas atlas;
		ImVector<ImWchar> ranges;
		ImFont* font = nullptr;
		std::string desc;
	};

	// The renderer's text face at 16 px (level 0 ranges: the characters in use - the advances are what matter), the
	// Windows CJK face merged by GlyphCore's rules, then FillMissing's per-script merges.
	bool BuildFace(Face& a_f, const std::string& a_primary, const std::string& a_lang, const std::string& a_allText)
	{
		const float px = 16.0f;
		a_f.atlas.Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
		glyphcore::BuildTextRanges(a_f.ranges, a_f.atlas, a_lang, a_lang, a_allText, {}, 0);
		a_f.font = a_f.atlas.AddFontFromFileTTF(a_primary.c_str(), px, nullptr, a_f.ranges.Data);
		if (!a_f.font) { return false; }
		a_f.desc = std::filesystem::path(a_primary).filename().string();
		std::vector<std::string> merged;
		for (const std::string& f : glyphcore::FallbackFaces(glyphcore::CjkLanguage(a_lang, a_lang, a_allText)))
		{
			std::error_code ec;
			if (f == a_primary || !std::filesystem::exists(f, ec)) { continue; }
			if (glyphcore::NeedsFallbackFace(a_f.ranges.Data))
			{
				ImFontConfig m;
				m.MergeMode = true;
				m.PixelSnapH = true;
				m.DstFont = a_f.font;
				if (a_f.atlas.AddFontFromFileTTF(f.c_str(), px, &m, a_f.ranges.Data)) { a_f.desc += " + " + std::filesystem::path(f).filename().string(); }
				merged.push_back(f);
			}
			break;
		}
		a_f.atlas.Build();
		const std::vector<ImWchar> missing = glyphcore::MissingIn(a_f.font, a_allText, {});
		bool again = false;
		for (const std::string& f : glyphcore::FacesToAdd(missing, merged, [](const std::string& p) { std::error_code ec; return std::filesystem::exists(p, ec); }))
		{
			ImFontConfig m;
			m.MergeMode = true;
			m.PixelSnapH = true;
			m.DstFont = a_f.font;
			if (a_f.atlas.AddFontFromFileTTF(f.c_str(), px, &m, a_f.ranges.Data)) { a_f.desc += " + " + std::filesystem::path(f).filename().string(); again = true; }
		}
		if (again) { a_f.atlas.Build(); }
		return true;
	}

	int g_fail = 0;
	void Result(bool a_ok, const std::string& a_what)
	{
		if (!a_ok) { ++g_fail; }
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}
}

int main(int argc, char** argv)
{
	const std::filesystem::path tr = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("dist/bin/x64_dx12/AMF/Translations");
	const float widths[] = { 120, 180, 240, 320, 400, 480, 560, 640, 800 };
	std::string japaneseFace;
	for (const char* f : { "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/YuGothM.ttc" })
	{
		std::error_code ec;
		if (std::filesystem::exists(f, ec)) { japaneseFace = f; break; }
	}
	if (japaneseFace.empty()) { std::cout << "SKIP no Japanese face (Meiryo or Yu Gothic) on this machine for the picked-face runs\n"; }

	// ---- Japanese and Chinese: kinsoku and numbers ------------------------------------------------------------------
	for (const char* lang : { "japanese", "chinese" })
	{
		const std::vector<std::string> texts = ReadTexts(tr / (std::string("ApocryphaMenuFramework_") + lang + ".txt"));
		std::string all;
		for (const std::string& t : texts) { all += t + "\n"; }
		const std::vector<std::string> paras = Paragraphs(texts);
		Result(texts.size() > 100, std::string(lang) + ": " + std::to_string(texts.size()) + " texts, " + std::to_string(paras.size()) + " paragraphs read");
		std::vector<std::string> primaries = { "C:/Windows/Fonts/segoeui.ttf" };
		if (!japaneseFace.empty()) { primaries.push_back(japaneseFace); }
		for (const std::string& primary : primaries)
		{
			Face face;
			if (!BuildFace(face, primary, lang, all)) { Result(false, std::string(lang) + ": the face " + primary + " loads"); continue; }
			Counts now, old;
			for (const float w : widths)
			{
				for (const std::string& p : paras)
				{
					Measure(p, Lines(p, w, [&](const char* s, const char* e, float ww) { return face.font->CalcWordWrapPositionA(1.0f, s, e, ww); }), now, face.font, w);
					Measure(p, Lines(p, w, [&](const char* s, const char* e, float ww) { return OldWrap(face.font, 1.0f, s, e, ww); }), old, face.font, w);
				}
			}
			const std::string head = std::string(lang) + ", " + face.desc + ", 16px, 9 widths: ";
			std::cout << "     " << head << "patched " << now.breaks << " breaks / unpatched " << old.breaks << " breaks\n";
			std::cout << "     " << head << "unpatched: line starts with a kinsoku-forbidden character " << old.kinsokuStartNoBlank << " (no blank) / "
					  << old.kinsokuStartAll << " (all), line ends with an opening bracket " << old.kinsokuEndNoBlank << " / " << old.kinsokuEndAll
					  << ", numbers split " << old.numberSplits << "\n";
			std::cout << "     " << head << "patched:   line starts with a kinsoku-forbidden character " << now.kinsokuStartNoBlank << " (no blank) / "
					  << now.kinsokuStartAll << " (all), line ends with an opening bracket " << now.kinsokuEndNoBlank << " / " << now.kinsokuEndAll
					  << ", numbers split " << now.numberSplits << "\n";
			std::cout << "     " << head << "patched:   forced cuts (a word wider than the whole line, cut anywhere by ImGui's own rule, which the patch keeps) "
					  << now.forced << (now.firstForced.empty() ? "" : " (first: " + now.firstForced + ")") << " - not counted above\n";
			if (now.kinsokuStartAll > now.kinsokuStartNoBlank + now.forced) { std::cout << "     (a forbidden character after a BLANK is an ordinary space break, e.g. \"word )\" - Latin text's rule, left as before)\n"; }
			Result(now.kinsokuStartNoBlank == 0, head + "no line starts with a kinsoku-forbidden character at a CJK break" + (now.firstStart.empty() ? "" : " (first: " + now.firstStart + ")"));
			Result(now.kinsokuEndNoBlank == 0, head + "no line ends with an opening bracket at a CJK break" + (now.firstEnd.empty() ? "" : " (first: " + now.firstEnd + ")"));
			Result(now.numberSplits == 0, head + "no number split across lines" + (now.firstNumber.empty() ? "" : " (first: " + now.firstNumber + ")"));
		}
	}

	// ---- languages without CJK: the same breaks as before ------------------------------------------------------------
	for (const char* lang : { "english", "french", "german", "italian", "spanish", "polish", "czech", "russian", "korean" })
	{
		const std::vector<std::string> texts = ReadTexts(tr / (std::string("ApocryphaMenuFramework_") + lang + ".txt"));
		std::string all;
		for (const std::string& t : texts) { all += t + "\n"; }
		const std::vector<std::string> paras = Paragraphs(texts);
		Face face;
		if (texts.empty() || !BuildFace(face, "C:/Windows/Fonts/segoeui.ttf", lang, all)) { Result(false, std::string(lang) + ": the file and the face load"); continue; }
		long long cases = 0, same = 0, numberRule = 0, other = 0;
		std::string firstOther;
		for (const float w : widths)
		{
			for (const std::string& p : paras)
			{
				++cases;
				const auto a = Lines(p, w, [&](const char* s, const char* e, float ww) { return face.font->CalcWordWrapPositionA(1.0f, s, e, ww); });
				const auto b = Lines(p, w, [&](const char* s, const char* e, float ww) { return OldWrap(face.font, 1.0f, s, e, ww); });
				size_t i = 0;
				while (i < a.size() && i < b.size() && a[i].eol == b[i].eol && a[i].next == b[i].next) { ++i; }
				if (i == a.size() && i == b.size()) { ++same; continue; }
				// the FIRST break that differs: expected only where the unpatched function split a number there
				Counts oc;
				if (i < b.size()) { Measure(p, { b[i] }, oc, face.font, w); }
				if (oc.numberSplits > 0) { ++numberRule; }
				else
				{
					if (!other++) { firstOther = p.substr(0, 60) + " @ " + std::to_string(static_cast<int>(w)) + "px"; }
				}
			}
		}
		Result(other == 0, std::string(lang) + ": " + std::to_string(same) + " of " + std::to_string(cases) + " paragraph x width wraps identical to the unpatched function, " +
			std::to_string(numberRule) + " differ only where the unpatched one split a number" + (other ? ", " + std::to_string(other) + " OTHER (first: " + firstOther + ")" : ""));
	}

	std::cout << "cjk_wrap_check: " << (g_fail == 0 ? "all passed" : std::to_string(g_fail) + " FAILED") << "\n";
	return g_fail == 0 ? 0 : 1;
}
