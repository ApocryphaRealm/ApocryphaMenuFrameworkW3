// Off-game check that every control's outline shows in every theme AMF-W3 ships (1.0.6, from Skyrim 2.2.0's "every control
// keeps a visible outline" and "outlines 4.5:1" - the owner, 2026-10-12, on Norden - Black: "the button faces don't have any
// kind of outline at all"). It runs include/ThemeOutline.h, the same sums theme::Apply runs, over:
//   * every theme INI in dist/bin/x64_dx12/AMF/themes (Norden, Norden - Black, Skellige, Vel'dun), read as Theme.cpp's
//     ScanUserThemes reads them (sBorder, else sFrame; sBackground; fBorderThickness; sBackgroundArt);
//   * the three built-in themes (Untarnished, Skyrim, Oblivion), whose colours are written below AND looked up in
//     src/Theme.cpp, so a changed built-in colour fails here until this list follows it.
// For each: the border colour must reach 4.5:1 against the panel and both control faces (boxes and buttons sit on the panel
// colour at rest), and against the theme's background art drawn over the panel, at the art's darker tone (the 10th
// percentile of the line's contrast across the art, at most 128 x 128 - the .MD theme outline gate's sum); the outline is
// never under one pixel. Controls: Norden's pre-1.0.6 border (#8E9396, 4.07) and Oblivion's (#6B563F on the parchment, 3.60)
// must fail, and the run-time fallback (the text colour) must pass where the border fails.
//   xmake build theme_style_check && xmake run theme_style_check [themes folder]
// Runs from the repo root (set_rundir). Prints one line per check and "theme_style_check: N of N passed"; exits 1 on any
// failure.
#include "ThemeOutline.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	std::string Fixed(float a_v)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.2f", a_v);
		return buf;
	}

	// "#RRGGBB" / "#RRGGBBAA" (Theme.cpp ParseColor) -> ImVec4
	bool ParseHex(std::string a_hex, ImVec4& a_out)
	{
		if (!a_hex.empty() && a_hex.front() == '#') { a_hex.erase(0, 1); }
		if (a_hex.size() != 6 && a_hex.size() != 8) { return false; }
		unsigned long v = 0;
		try { v = std::stoul(a_hex, nullptr, 16); } catch (...) { return false; }
		float a = 1.0f;
		if (a_hex.size() == 8) { a = (v & 0xFF) / 255.0f; v >>= 8; }
		a_out = ImVec4{ ((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a };
		return true;
	}

	struct Theme
	{
		std::string name;
		std::string border, background, text, art;   // as written (#RRGGBB); art = a background art name, or empty
		float thickness = 1.0f;
	};

	std::string Trim(std::string a_s)
	{
		while (!a_s.empty() && (a_s.back() == '\r' || a_s.back() == ' ' || a_s.back() == '\t')) { a_s.pop_back(); }
		while (!a_s.empty() && (a_s.front() == ' ' || a_s.front() == '\t')) { a_s.erase(0, 1); }
		return a_s;
	}

	std::vector<Theme> IniThemes(const std::filesystem::path& a_dir)
	{
		std::vector<Theme> out;
		std::error_code ec;
		for (const auto& e : std::filesystem::directory_iterator(a_dir, ec))
		{
			if (e.path().extension() != ".ini") { continue; }
			Theme t;
			t.name = e.path().stem().string();
			std::string frame;
			std::ifstream in(e.path());
			std::string line;
			while (std::getline(in, line))
			{
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') { continue; }
				const auto eq = line.find('=');
				if (eq == std::string::npos) { continue; }
				const std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
				if (k == "sName") { t.name = v; }
				else if (k == "sBorder") { t.border = v; }
				else if (k == "sFrame") { frame = v; }
				else if (k == "sBackground") { t.background = v; }
				else if (k == "sText") { t.text = v; }
				else if (k == "sBackgroundArt") { t.art = v; }
				else if (k == "fBorderThickness") { t.thickness = std::stof(v); }
			}
			if (t.border.empty()) { t.border = frame; }     // Theme.cpp: the border falls back to the frame colour
			if (t.text.empty()) { t.text = frame; }
			out.push_back(t);
		}
		return out;
	}

	// The theme's background art as straight RGBA, scaled to fit 128 x 128 (WIC); empty when it cannot be read.
	std::vector<ImVec4> ArtPixels(const std::filesystem::path& a_png)
	{
		std::vector<ImVec4> out;
		IWICImagingFactory* f = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) || !f) { return out; }
		IWICBitmapDecoder* dec = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICBitmapScaler* scaler = nullptr;
		IWICFormatConverter* conv = nullptr;
		UINT w = 0, h = 0;
		if (SUCCEEDED(f->CreateDecoderFromFilename(a_png.wstring().c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
			SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0)
		{
			const double k = std::min(1.0, 128.0 / std::max(w, h));
			const UINT sw = std::max(1u, static_cast<UINT>(w * k)), sh = std::max(1u, static_cast<UINT>(h * k));
			if (SUCCEEDED(f->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant)) &&
				SUCCEEDED(f->CreateFormatConverter(&conv)) &&
				SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
			{
				std::vector<BYTE> px(static_cast<std::size_t>(sw) * sh * 4);
				if (SUCCEEDED(conv->CopyPixels(nullptr, sw * 4, static_cast<UINT>(px.size()), px.data())))
				{
					for (std::size_t i = 0; i + 3 < px.size(); i += 4)
					{
						out.push_back(ImVec4{ px[i] / 255.0f, px[i + 1] / 255.0f, px[i + 2] / 255.0f, px[i + 3] / 255.0f });
					}
				}
			}
		}
		for (IUnknown* u : std::initializer_list<IUnknown*>{ conv, scaler, frame, dec, f }) { if (u) { u->Release(); } }
		return out;
	}

	constexpr float kArtPercentile = 0.10f;   // the art's darker tone (the .MD theme outline gate's ART_PERCENTILE)

	// The line's contrast against the art drawn over the panel, at the art's darker tone; < 0 when there are no pixels.
	float ArtContrast(const ImVec4& a_line, const ImVec4& a_panel, const std::vector<ImVec4>& a_px)
	{
		if (a_px.empty()) { return -1.0f; }
		const ImVec4 base = themeoutline::Over(a_panel, ImVec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		std::vector<float> cs;
		cs.reserve(a_px.size());
		for (const ImVec4& q : a_px)
		{
			const ImVec4 tone = themeoutline::Over(q, base);
			cs.push_back(themeoutline::OutlineContrast(a_line, tone, tone));
		}
		std::sort(cs.begin(), cs.end());
		return cs[static_cast<std::size_t>(cs.size() * kArtPercentile)];
	}

	// The built-in themes as Theme.cpp's Apply registers them (ABGR there, #RRGGBB here).
	struct BuiltIn
	{
		Theme theme;
		const char* borderAbgr;       // the literal that must appear in src/Theme.cpp
		const char* backgroundAbgr;
	};

	std::string ReadAll(const std::filesystem::path& a_p)
	{
		std::ifstream in(a_p, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
}

int main(int argc, char** argv)
{
	const std::filesystem::path dir = argc > 1 ? argv[1] : "dist/bin/x64_dx12/AMF/themes";
	const std::filesystem::path assets = dir.parent_path() / "assets" / "backgrounds";
	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) { std::cout << "theme_style_check: COM did not start\n"; return 2; }

	std::vector<Theme> themes = IniThemes(dir);
	Check(themes.size() == 4, "the 4 shipped theme INIs are read from " + dir.string() + " (" + std::to_string(themes.size()) + ")");

	const std::string themeCpp = ReadAll("src/Theme.cpp");
	const BuiltIn builtIns[] = {
		{ { "Untarnished (built in)", "#F5F2E9", "#000000", "#F5F2E9", "", 1.0f }, "0xFFE9F2F5, 1.0f", "\"Untarnished\", 0xFF000000" },
		{ { "Skyrim (built in)", "#B0B0B0", "#000000", "#F5F2E9", "", 1.0f }, "/*border*/ 0xFFB0B0B0", "/*background*/ 0xFF000000" },
		{ { "Oblivion (built in)", "#584734", "#E4DBCC", "#2A1C12", "parchment", 1.0f }, "/*border*/ 0xFF344758", "/*background*/ 0xFFCCDBE4" },
	};
	for (const BuiltIn& b : builtIns)
	{
		Check(themeCpp.find(b.borderAbgr) != std::string::npos && themeCpp.find(b.backgroundAbgr) != std::string::npos,
			  b.theme.name + ": its border and background here are the ones src/Theme.cpp registers");
		themes.push_back(b.theme);
	}
	Check(themeCpp.find("oblivion.art[static_cast<std::size_t>(skin::ArtKind::kBackground)] = \"parchment\"") != std::string::npos,
		  "Oblivion (built in): its background art is the parchment, as src/Theme.cpp registers it");

	for (const Theme& t : themes)
	{
		ImVec4 line{}, panel{}, text{};
		if (!ParseHex(t.border, line) || !ParseHex(t.background, panel) || !ParseHex(t.text, text))
		{
			Check(false, t.name + ": border, background and text colours are readable");
			continue;
		}
		// faces: boxes and buttons are the panel colour at rest (Theme.cpp kRoleBoxes / kRoleButtons)
		const float worst = themeoutline::WorstContrast(line, panel, panel, panel);
		Check(worst >= themeoutline::kMinOutlineContrast, t.name + ": border " + t.border + " on panel and faces " + t.background +
			  " has contrast " + Fixed(worst) + " (needs " + Fixed(themeoutline::kMinOutlineContrast) + ")");
		Check(themeoutline::OutlineWidth(t.thickness) >= 1.0f && t.thickness > 0.0f,
			  t.name + ": outline " + Fixed(themeoutline::OutlineWidth(t.thickness)) + " px (fBorderThickness " + Fixed(t.thickness) + ")");
		Check(themeoutline::WorstContrast(text, panel, panel, panel) >= themeoutline::kMinOutlineContrast,
			  t.name + ": the run-time fallback, the text colour " + t.text + ", would show (" + Fixed(themeoutline::WorstContrast(text, panel, panel, panel)) + ")");
		if (!t.art.empty())
		{
			const std::vector<ImVec4> px = ArtPixels(assets / (t.art + ".png"));
			const float c = ArtContrast(line, panel, px);
			Check(c >= themeoutline::kMinOutlineContrast, t.name + ": border " + t.border + " on its background art " + t.art + " over " +
				  t.background + " has contrast " + Fixed(c) + " at the art's darker tone (needs " + Fixed(themeoutline::kMinOutlineContrast) + ")");
		}
	}

	// controls: the colours 1.0.6 changed fail the same sums, and their fallback would have shown
	{
		ImVec4 oldNorden{}, norden{}, nordenText{}, oldObl{}, obl{};
		ParseHex("#8E9396", oldNorden); ParseHex("#333333", norden); ParseHex("#E5E5E5", nordenText);
		ParseHex("#6B563F", oldObl); ParseHex("#E4DBCC", obl);
		const float c = themeoutline::WorstContrast(oldNorden, norden, norden, norden);
		Check(c < themeoutline::kMinOutlineContrast && themeoutline::WorstContrast(nordenText, norden, norden, norden) >= themeoutline::kMinOutlineContrast,
			  "control: Norden's pre-1.0.6 border #8E9396 fails (" + Fixed(c) + ") and theme::Apply would use its text colour");
		const float a = ArtContrast(oldObl, obl, ArtPixels(assets / "parchment.png"));
		Check(a >= 0.0f && a < themeoutline::kMinOutlineContrast,
			  "control: Oblivion's pre-1.0.6 border #6B563F fails on the parchment (" + Fixed(a) + ")");
		Check(themeoutline::OutlineWidth(0.0f) == 1.0f && themeoutline::OutlineWidth(2.0f) == 2.0f, "control: a 0 px theme still gets a 1 px outline, a 2 px theme keeps 2");
	}

	CoUninitialize();
	std::cout << "theme_style_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
