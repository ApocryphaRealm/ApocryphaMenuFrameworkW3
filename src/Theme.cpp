#include "Paths.h"
#include "Theme.h"

#include "KnotworkBorder.h"
#include "MapEdgeBorder.h"

#include "Settings.h"
#include "Logger.h"

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace theme
{
	namespace
	{
		std::vector<Palette> g_themes;
		std::string g_activeId;

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) a_text.remove_prefix(1);
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) a_text.remove_suffix(1);
			return a_text;
		}

		bool ParseColor(std::string_view a_hex, std::uint32_t& a_out)
		{
			// Accepts "RRGGBB" (as written in a theme file); stored/consumed as ABGR to match
			// the rest of this codebase's packed-colour convention.
			if (!a_hex.empty() && a_hex.front() == '#')
			{
				a_hex.remove_prefix(1);
			}
			// RRGGBBAA as well (1.9.8), so a theme can ask for a slightly translucent panel the way
			// Vel'dun UI's own ImGui style does (#1D1A17F4).
			if (a_hex.size() != 6 && a_hex.size() != 8)
			{
				return false;
			}

			std::uint32_t value = 0;
			const auto result = std::from_chars(a_hex.data(), a_hex.data() + a_hex.size(), value, 16);
			if (result.ec != std::errc{})
			{
				return false;
			}

			std::uint32_t a = 0xFF;
			if (a_hex.size() == 8)
			{
				a = value & 0xFF;
				value >>= 8;
			}
			const std::uint32_t r = (value >> 16) & 0xFF;
			const std::uint32_t g = (value >> 8) & 0xFF;
			const std::uint32_t b = value & 0xFF;
			a_out = (a << 24) | (b << 16) | (g << 8) | r;  // ABGR
			return true;
		}

		// Skyrim read fHUDOpacity from its INI collections (ported from Dragon's Eye Minimap). Oblivion Remastered
		// keeps its HUD opacity in Unreal's config, which is not wired yet, so the framework renders fully opaque -
		// the same outcome Skyrim had when no candidate name matched.
	}

	void RegisterTheme(Palette a_palette)
	{
		for (Palette& existing : g_themes)
		{
			if (existing.id == a_palette.id)
			{
				logger::info("theme \"{}\" ({}) replaced (re-registration)", a_palette.name, a_palette.id);
				existing = std::move(a_palette);
				return;
			}
		}

		logger::info("theme \"{}\" ({}) registered ({} theme(s) total)", a_palette.name, a_palette.id, g_themes.size() + 1);
		g_themes.push_back(std::move(a_palette));
	}

	void ScanUserThemes()
	{
		const std::string kDir = paths::Str("themes");

		std::error_code ec;
		if (!std::filesystem::exists(kDir, ec) || ec)
		{
			logger::debug("theme scan: {} does not exist; only built-in themes are available", kDir);
			return;
		}

		std::size_t found = 0;

		for (const auto& entry : std::filesystem::directory_iterator(kDir, ec))
		{
			if (ec || !entry.is_regular_file())
			{
				continue;
			}
			if (entry.path().extension() != ".ini")
			{
				continue;
			}

			std::ifstream file(entry.path());
			if (!file.is_open())
			{
				logger::warn("theme scan: could not open {}", entry.path().string());
				continue;
			}

			Palette palette{};
			palette.id = entry.path().stem().string();
			palette.name = palette.id;
			palette.background = 0xFF000000;
			palette.frame = 0xFFE9F2F5;
			palette.borderThickness = 1.0f;

			std::string line;
			while (std::getline(file, line))
			{
				const std::string_view trimmed = Trim(line);
				if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' || trimmed.front() == '[')
				{
					continue;
				}
				const auto equals = trimmed.find('=');
				if (equals == std::string_view::npos)
				{
					continue;
				}
				const std::string_view key = Trim(trimmed.substr(0, equals));
				const std::string_view value = Trim(trimmed.substr(equals + 1));

				if (key == "sName")
				{
					palette.name = std::string(value);
				}
				else if (key == "sBackground")
				{
					ParseColor(value, palette.background);
				}
				else if (key == "sFrame")
				{
					ParseColor(value, palette.frame);
				}
				else if (key == "fBorderThickness")
				{
					float v{};
					if (std::from_chars(value.data(), value.data() + value.size(), v).ec == std::errc{})
					{
						palette.borderThickness = v;
					}
				}
				// 1.9.8: the refined colour roles and the theme's own art, so an INI theme can be as
				// complete as a built-in one. Every key is optional; an absent one keeps the fallback.
				else if (key == "sBorder") { ParseColor(value, palette.border); }
				else if (key == "sText") { ParseColor(value, palette.text); }
				else if (key == "sTextDim") { ParseColor(value, palette.textDim); }
				else if (key == "sAccent") { ParseColor(value, palette.accent); }
				else if (key == "sTextHeader") { ParseColor(value, palette.textHeader); }   // 1.0.5: the text roles
				else if (key == "sTextHelp") { ParseColor(value, palette.textHelp); }
				else if (key == "bKnotwork") { palette.knotwork = (value == "1" || value == "true"); }
				else if (key == "bMapEdge") { palette.mapEdge = (value == "1" || value == "true"); if (palette.mapEdge) { palette.knotwork = true; } }
				else if (key == "sSkinFrame") { palette.skinFrame = std::string(value); }
				else if (key == "sSkinBackground") { palette.skinBackground = std::string(value); }
				else if (key == "sSkinPlates") { palette.skinPlates = std::string(value); }
				else if (key == "uSkinFrameCorner")
				{
					std::uint32_t v{};
					if (std::from_chars(value.data(), value.data() + value.size(), v).ec == std::errc{} && v > 0)
					{
						palette.skinFrameCorner = v;
					}
				}
			}

			RegisterTheme(std::move(palette));
			++found;
		}

		logger::info("theme scan: {} user theme(s) loaded from {}", found, kDir);
	}

	std::vector<Palette> ListThemes()
	{
		return g_themes;
	}

	// The 2026-09-01 merge: "vanilla" and "mo2-skyrim" both became "skyrim". An INI written
	// before that names a theme that no longer exists, so map the retired ids rather than
	// silently falling back and losing the player's choice.
	std::string MigrateThemeId(const std::string& a_id)
	{
		if (a_id == "vanilla" || a_id == "mo2-skyrim") { return "skyrim"; }
		// 2026-10-02: the map-edge theme's first name, and the local Oblivion Paper theme it replaced (the owner: "rename
		// the current theme to Oblivion, and we can get rid of the old Oblivion paper theme").
		if (a_id == "cyrodiil" || a_id == "oblivion-paper") { return "oblivion"; }
		// 2026-10-05 (Witcher 3): Oathvein became Skellige, on the main menu's black (the owner: "let's call it Skellige").
		if (a_id == "oathvein") { return "skellige"; }
		return a_id;
	}

	void SetActiveTheme(const std::string& a_id)
	{
		if (a_id.empty())
		{
			return;
		}

		for (const Palette& p : g_themes)
		{
			if (p.id == a_id)
			{
				g_activeId = a_id;
				logger::info("active theme -> \"{}\" ({})", p.name, p.id);
				return;
			}
		}

		logger::warn("SetActiveTheme(\"{}\") refused: no such theme registered", a_id);
	}

	const Palette& GetActiveTheme()
	{
		for (const Palette& p : g_themes)
		{
			if (p.id == g_activeId)
			{
				return p;
			}
		}

		// Fall back to whatever registered first (the compiled-in default) rather than crash -
		// this only happens if g_activeId was never set to a real id, which Apply()'s caller
		// prevents by registering built-ins before ever calling this.
		return g_themes.front();
	}

	float GetGameHUDOpacity()
	{
		return 1.0f;   // see the note above GetGameHUDOpacity's Skyrim source: not wired on Oblivion Remastered yet
	}

	float BaseWindowPadding()
	{
		return static_cast<float>(knotwork::kCorner) + 8.0f;   // the kFramePadding Apply() sets
	}

	namespace
	{
		// The right-click menus' padding (Renderer's ctxPad). ImGui reads WindowPadding.y when the combo popup
		// opens, inside BeginCombo, so the push only has to cover that call.
		void PushListPadding()
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetFontSize() * 0.35f));
		}
	}

	bool BeginComboTight(const char* a_label, const char* a_preview)
	{
		PushListPadding();
		const bool open = ImGui::BeginCombo(a_label, a_preview);
		ImGui::PopStyleVar();
		return open;
	}

	bool ComboTight(const char* a_label, int* a_current, const char* const a_items[], int a_count)
	{
		PushListPadding();
		const bool changed = ImGui::Combo(a_label, a_current, a_items, a_count);
		ImGui::PopStyleVar();
		return changed;
	}

	// THE LOOK FROM A SET OF PICKS (1.0.5, Skyrim 2.1.5): every ImGui colour, role colour and text-role colour the theme draws
	// with, worked out from the active theme and a set of colour picks - with no side effects, so the Colours page can draw its
	// preview in picks the player has not applied yet. Apply() uses it with the saved picks.
	struct Look
	{
		ImVec4 colors[ImGuiCol_COUNT];
		std::uint32_t role[kRoleCount] = {};
		std::uint32_t themeRole[kRoleCount] = {};
		std::uint32_t header = 0xFFFFFFFF, help = 0xFFFFFFFF;
		std::string picked;
	};

	namespace
	{
		std::uint32_t g_roleU32[kRoleCount] = {};
		std::uint32_t g_themeRoleU32[kRoleCount] = {};
		std::uint32_t g_headerTextU32 = 0xFFFFFFFF;
		std::uint32_t g_helpTextU32 = 0xFFFFFFFF;

		void BuildLook(const Palette& active, const std::array<std::string, kRoleCount>& a_picks, Look& a_out)
		{
			for (int i = 0; i < ImGuiCol_COUNT; ++i) { a_out.colors[i] = ImGui::GetStyle().Colors[i]; }
			auto unpack = [](std::uint32_t abgr) {
				const float a = ((abgr >> 24) & 0xFF) / 255.0f;
				const float b = ((abgr >> 16) & 0xFF) / 255.0f;
				const float g = ((abgr >> 8) & 0xFF) / 255.0f;
				const float r = (abgr & 0xFF) / 255.0f;
				return ImVec4{ r, g, b, a };
			};

			// A theme provides `frame`; the refined roles (border/text/textDim/accent) fall back to
			// it when 0, so simple and INI-scanned themes are unchanged while MO2 Skyrim gets its
			// real layered look.
			const std::uint32_t borderId = active.border ? active.border : active.frame;
			const std::uint32_t textId = active.text ? active.text : active.frame;
			const std::uint32_t textDimId = active.textDim ? active.textDim : active.frame;
			const std::uint32_t accentId = active.accent ? active.accent : active.frame;

			// THE COLOUR ROLES (Appearance > Colours): the theme's own colour for each, then the player's [Colors] choice over
			// it. Every derived shade below (hover washes, separators, the see-through fade) is worked out from the result, so a
			// picked colour carries through the whole look the way the theme's own would.
			auto tint = [](const ImVec4& v, float a) { return ImVec4{ v.x, v.y, v.z, a }; };
			ImVec4 themeRole[kRoleCount];
			themeRole[kRoleBackground] = unpack(active.background);
			themeRole[kRoleBorder] = unpack(borderId);
			themeRole[kRoleArt] = ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };   // the art as drawn
			themeRole[kRoleBoxes] = themeRole[kRoleBackground];        // fields and buttons sit on the background at rest
			themeRole[kRoleText] = unpack(textId);
			themeRole[kRoleTextDim] = tint(unpack(textDimId), 1.0f);
			themeRole[kRoleAccent] = unpack(accentId);
			themeRole[kRoleSlider] = themeRole[kRoleAccent];
			themeRole[kRoleSwitchOn] = ImVec4{ 76 / 255.0f, 175 / 255.0f, 80 / 255.0f, 1.0f };    // the switch's own green / red
			themeRole[kRoleSwitchOff] = ImVec4{ 191 / 255.0f, 68 / 255.0f, 68 / 255.0f, 1.0f };
			// Text roles: a theme without them takes its accent for headings (the colour it already uses to mark what matters)
			// and, for help, its dim tone a third of the way toward the main text - apart from values, quieter than labels.
			themeRole[kRoleHeading] = active.textHeader ? unpack(active.textHeader) : themeRole[kRoleAccent];
			{
				const ImVec4& d = themeRole[kRoleTextDim];
				const ImVec4& t = themeRole[kRoleText];
				themeRole[kRoleHelp] = active.textHelp ? unpack(active.textHelp)
					: ImVec4{ d.x + (t.x - d.x) / 3.0f, d.y + (t.y - d.y) / 3.0f, d.z + (t.z - d.z) / 3.0f, 1.0f };
			}
			themeRole[kRoleHover] = themeRole[kRoleAccent];   // the hover wash is the selection colour unless picked
			// (the art library's per-kind roles start here as the colours they share - see the note on ColorRole)
			ImVec4 role[kRoleCount];
			{
				std::string picked;
				for (int r = 0; r < kRoleCount; ++r)
				{
					role[r] = themeRole[r];
					std::uint32_t abgr = 0;
					if (!a_picks[r].empty() && ParseColor(a_picks[r], abgr))
					{
						role[r] = unpack(abgr);
						picked += std::string(picked.empty() ? "" : ", ") + kColorRoleKeys[r] + "=" + a_picks[r];
					}
				}
				if (a_picks[kRoleHover].empty()) { role[kRoleHover] = role[kRoleAccent]; }   // follows a picked selection colour
				// (the art library's follow rules go here: an unpicked kind follows a picked colour of the role it shared)
				for (int r = 0; r < kRoleCount; ++r)
				{
					a_out.themeRole[r] = ImGui::ColorConvertFloat4ToU32(themeRole[r]);
					a_out.role[r] = ImGui::ColorConvertFloat4ToU32(role[r]);
				}
				a_out.picked = std::move(picked);
			}

			const ImVec4 black = role[kRoleBackground];
			const ImVec4 border = role[kRoleBorder];
			const ImVec4 text = role[kRoleText];
			const ImVec4 accent = role[kRoleAccent];
			const ImVec4 boxes = role[kRoleBoxes];
			const ImVec4 slider = role[kRoleSlider];

			// Alpha variants of a base colour, for the graded hover/active/fill states.
			const ImVec4 textDimC = role[kRoleTextDim];                      // secondary text (its own hue)
			const ImVec4 borderDim = tint(border, 0.55f);                    // separators
			const ImVec4 borderFaint = tint(border, 0.14f);                  // subtle fills
			const ImVec4 borderSoft = tint(border, 0.28f);                   // hover fills
			const ImVec4 accentFaint = tint(accent, 0.22f);                  // selected row (gold wash)
			const ImVec4 accentSoft = tint(accent, 0.42f);                   // hovered/active selection
			const ImVec4 hoverSoft = tint(role[kRoleHover], 0.42f);          // the hover wash (the player's Hover highlight)

			ImVec4* c = a_out.colors;
			c[ImGuiCol_WindowBg] = black;
			c[ImGuiCol_ChildBg] = black;
			c[ImGuiCol_PopupBg] = black;
			c[ImGuiCol_MenuBarBg] = black;
			c[ImGuiCol_TitleBg] = black;
			c[ImGuiCol_TitleBgActive] = black;
			c[ImGuiCol_TitleBgCollapsed] = black;

			c[ImGuiCol_Text] = text;
			c[ImGuiCol_TextDisabled] = textDimC;  // NOT ImGui's ~50% grey - the readability rule applies to every theme

			c[ImGuiCol_Border] = border;
			c[ImGuiCol_BorderShadow] = ImVec4{ 0, 0, 0, 0 };
			c[ImGuiCol_Separator] = borderDim;
			c[ImGuiCol_SeparatorHovered] = borderSoft;
			c[ImGuiCol_SeparatorActive] = border;

			c[ImGuiCol_FrameBg] = boxes;
			c[ImGuiCol_FrameBgHovered] = borderFaint;
			c[ImGuiCol_FrameBgActive] = borderSoft;
			c[ImGuiCol_Button] = boxes;
			c[ImGuiCol_ButtonHovered] = borderFaint;
			c[ImGuiCol_ButtonActive] = borderSoft;

			// Selection (Selectable, tree, list rows) = the accent wash.
			c[ImGuiCol_Header] = accentFaint;
			c[ImGuiCol_HeaderHovered] = hoverSoft;
			c[ImGuiCol_HeaderActive] = accentSoft;

			// Tabs: quiet by default, the accent when active/selected.
			c[ImGuiCol_Tab] = boxes;
			c[ImGuiCol_TabHovered] = hoverSoft;
			c[ImGuiCol_TabActive] = accentFaint;
			c[ImGuiCol_TabUnfocused] = boxes;
			c[ImGuiCol_TabUnfocusedActive] = borderFaint;

			// Scrollbar: dark trough, silver grab.
			c[ImGuiCol_ScrollbarBg] = black;
			c[ImGuiCol_ScrollbarGrab] = borderDim;
			c[ImGuiCol_ScrollbarGrabHovered] = borderSoft;
			c[ImGuiCol_ScrollbarGrabActive] = border;

			// Interactive accents.
			c[ImGuiCol_SliderGrab] = slider;
			c[ImGuiCol_SliderGrabActive] = slider;
			c[ImGuiCol_CheckMark] = slider;
			// The controller navigation box is bright blue in every theme (the owner, 2026-09-15: "the next amf version should have
			// a bright blue controller nav box instead of the old yellow one"), so the focused item stands out from the gold
			// selection wash instead of blending into it.
			c[ImGuiCol_NavHighlight] = ImVec4{ 0.24f, 0.62f, 1.00f, 1.00f };
			// 1.7.7: every remaining ImGui default that is blue or off-palette (the owner saw "a bit more of a
			// blue or purple colour in some areas"); nothing the menu draws is left on Dear ImGui's own palette.
			c[ImGuiCol_TextSelectedBg] = accentSoft;
			c[ImGuiCol_DragDropTarget] = accent;
			c[ImGuiCol_ResizeGrip] = borderFaint;
			c[ImGuiCol_ResizeGripHovered] = borderSoft;
			c[ImGuiCol_ResizeGripActive] = border;
			c[ImGuiCol_TableHeaderBg] = black;
			c[ImGuiCol_TableBorderStrong] = borderDim;
			c[ImGuiCol_TableBorderLight] = borderFaint;
			c[ImGuiCol_TableRowBg] = ImVec4{ 0, 0, 0, 0 };
			c[ImGuiCol_TableRowBgAlt] = borderFaint;
			c[ImGuiCol_PlotLines] = accent;
			c[ImGuiCol_PlotLinesHovered] = accentSoft;
			c[ImGuiCol_PlotHistogram] = accent;
			c[ImGuiCol_PlotHistogramHovered] = accentSoft;
			c[ImGuiCol_ModalWindowDimBg] = ImVec4{ 0, 0, 0, 0.6f };
			c[ImGuiCol_NavWindowingHighlight] = accent;
			c[ImGuiCol_NavWindowingDimBg] = ImVec4{ 0, 0, 0, 0.4f };

			// WINDOW OPACITY (Skyrim 2.1.1 - Barzing on Nexus, 2026-10-05: "the semi transparence of the window"; the owner: "ill
			// add ... opacity settings"). Applied last, over whatever the theme set, and only with See-through window on ("i want
			// these settings behind a toggle"). Down to 5%, and NOT one factor for everything (the owner: "affect the black
			// background proportionally more than things like the text or the boxes, because the black background is what is
			// blocking their view"):
			//   the window and pane backgrounds take the opacity as set (5% at the bottom);
			//   boxes - fields, buttons, headers, tabs, borders, separators, scrollbars, table lines - keep 30% plus 70% of it;
			//   text keeps 60% plus 40% of it, so it stays readable at the bottom of the scale.
			// The right-click menus and tooltips (PopupBg) stay solid: they are open only while being read.
			const float opacity = settings::Get().seeThrough
				? static_cast<float>(std::clamp(settings::Get().windowOpacity, 5, 100)) / 100.0f : 1.0f;
			if (opacity < 1.0f)
			{
				const float boxFade = 0.30f + 0.70f * opacity;
				const float words = 0.60f + 0.40f * opacity;
				c[ImGuiCol_WindowBg].w *= opacity;
				c[ImGuiCol_ChildBg].w *= opacity;
				for (const ImGuiCol box : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_Button,
						 ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive, ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
						 ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabActive, ImGuiCol_TabUnfocused, ImGuiCol_TabUnfocusedActive,
						 ImGuiCol_Border, ImGuiCol_Separator, ImGuiCol_SeparatorHovered, ImGuiCol_SeparatorActive,
						 ImGuiCol_ScrollbarBg, ImGuiCol_ScrollbarGrab, ImGuiCol_ScrollbarGrabHovered, ImGuiCol_ScrollbarGrabActive,
						 ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_CheckMark, ImGuiCol_TableHeaderBg,
						 ImGuiCol_TableBorderStrong, ImGuiCol_TableBorderLight, ImGuiCol_TableRowBgAlt, ImGuiCol_ResizeGrip,
						 ImGuiCol_ResizeGripHovered, ImGuiCol_ResizeGripActive })
				{
					c[box].w *= boxFade;
				}
				c[ImGuiCol_Text].w *= words;
				c[ImGuiCol_TextDisabled].w *= words;
			}

			// The heading and help colours as drawn: with the see-through window's text alpha, so they fade with the text.
			{
				const float words = c[ImGuiCol_Text].w;
				a_out.header = ImGui::ColorConvertFloat4ToU32(tint(role[kRoleHeading], words));
				a_out.help = ImGui::ColorConvertFloat4ToU32(tint(role[kRoleHelp], words));
			}
		}
	}

	void Apply()
	{
		if (g_themes.empty())
		{
			// Built-ins, registered here rather than at a separate call site so Apply() is
			// always safe to call standalone (e.g. from a test/DevBench path).
			//
			// "Untarnished" - the ORIGINAL identity (solid black, #F5F2E9 warm off-white),
			// shipped as a selectable theme per the author's instruction, no longer the only option.
			// 1.0.5 (Skyrim 2.1.5) text roles (ABGR): section headings warm gold #D8C27A, help text a cool steel blue #9FB8CC -
			// the same pair on Untarnished and Skyrim.
			Palette untarnished{ "untarnished", "Untarnished", 0xFF000000, 0xFFE9F2F5, 1.0f };
			untarnished.textHeader = 0xFF7AC2D8;
			untarnished.textHelp = 0xFFCCB89F;
			RegisterTheme(std::move(untarnished));

			// "Skyrim" - the knotwork look: the Nordic frame art and silver/white lines rebuilt from
			// the real Trosski Skyrim style (its border-image.png and stylesheet), with the crisper
			// warm off-white text of the Untarnished palette. It replaces the old "Vanilla" and
			// "MO2 Skyrim" entries, which the author retired on 2026-09-01 as "extremely similar in
			// colour and design" - they differed only in the text tone, and this keeps the better one.
			// Silver frame lines #b0b0b0, dim secondary #717171, accent #F5F2E9 for selection (gold until 1.0.5),
			// text #F5F2E9. ABGR packing (0xAABBGGRR). Background stays solid black: the project's
			// full-opacity rule holds over live gameplay, unlike MO2's near-transparent desktop look.
			Palette skyrim{ "skyrim", "Skyrim",
				/*background*/ 0xFF000000, /*frame*/ 0xFFB0B0B0, /*borderThickness*/ 1.0f,
				/*border*/ 0xFFB0B0B0, /*text*/ 0xFFE9F2F5, /*textDim*/ 0xFF717171,
				/*accent*/ 0xFFE9F2F5, /*knotwork*/ true };
			skyrim.textHeader = 0xFF7AC2D8;
			skyrim.textHelp = 0xFFCCB89F;
			RegisterTheme(std::move(skyrim));
			// 1.0.5 (Skyrim 2.1.6, the owner): the accent - the highlight, and the tick marks and slider grabs that follow it -
			// is the knotwork's white (#F5F2E9), not the old gold: "Everything that was originally yellow ... should be now white."

			// "Oblivion" (named "Cyrodiil Map" for an hour; the owner: "just rename the current theme to
			// Oblivion") - this framework's OWN look and its default (the owner, 2026-10-02: "give it a frame art
			// similar to how Skyrim has a frame art, except this frame will be more like an embroidered map's edge ...
			// in a gold or brown color"). The embroidered map-edge frame (MapEdgeBorder.h, original art) on a
			// parchment ground with brown ink: #E4DBCC paper, #2A1C12 text, #6F5D4C dim, #6B563F lines, #8A6A2C
			// brass accent - the game's paper-menu palette as plain colours, so no game art ships. ABGR packing.
			Palette oblivion{ "oblivion", "Oblivion",
				/*background*/ 0xFFCCDBE4, /*frame*/ 0xFF161C24, /*borderThickness*/ 1.0f,
				/*border*/ 0xFF3F566B, /*text*/ 0xFF121C2A, /*textDim*/ 0xFF4C5D6F,
				/*accent*/ 0xFF2C6A8A, /*knotwork*/ true, /*mapEdge*/ true };
			oblivion.textHeader = 0xFF1E3B7A;   // #7A3B1E heading, #4E5F72 help - Skyrim 2.1.5's oblivion.ini
			oblivion.textHelp = 0xFF725F4E;
			RegisterTheme(std::move(oblivion));

			g_activeId = "untarnished";   // the built-in fallback when the configured theme (default skellige, an INI theme) is absent

			ScanUserThemes();

			// Honour a saved preference from the INI (file-first, rule 16) if it names a real
			// theme; otherwise the compiled default above stands. Only checked on this first
			// call - the live picker calls SetActiveTheme() itself before every later Apply().
			SetActiveTheme(settings::Get().themeId);
		}
		else
		{
			ScanUserThemes();
		}

		const Palette& active = GetActiveTheme();

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowBorderSize = active.borderThickness;
		style.FrameBorderSize = active.borderThickness;
		style.PopupBorderSize = active.borderThickness;
		style.ChildBorderSize = active.borderThickness;

		// MARGINS FOR THE KNOTWORK ART (author, 2026-09-01, comparing Vanilla against Untarnished:
		// "the Skyrim theme doesn't let them fully see all the corners and lines of a box with a
		// border ... you might have to change the margin between those areas and the edge of the
		// menu frame"). The knotwork frame is a 9-slice drawn ON a rect, and its corner ornament is
		// a FIXED 26px regardless of UI scale, so it occupies a 26px band just inside whatever rect
		// it frames. With ImGui's default padding (8px, ~13px after the resolution scale) the
		// window's band lay over the panes inside it and each pane's band lay over its own first
		// line of text - which is why the version line read "pocrypha Menu Framework". Themes
		// WITHOUT the art keep ImGui's normal padding; themes with it get the band's width plus a
		// few pixels of air, so every box's border and all four corners stay visible.
		// EVERY theme gets the same padding (author, 2026-09-01: "edit the untarnished theme to have
		// the same margin edits so they look similar in spacing"). The knotwork art is what forced
		// the figure - its corner ornament is a fixed 26px band inside whatever rect it frames - but
		// applying it to the plain themes too keeps the layout identical whichever theme is picked,
		// so switching theme changes the colours and the art, never the geometry.
		// The map-edge frame (2026-10-02) was drawn to the same 26 px corner, so it shares this padding.
		static_assert(mapedge::kDrawCorner == knotwork::kCorner, "every built-in frame shares one corner, so one layout");
		constexpr float kFramePadding = static_cast<float>(knotwork::kCorner) + 8.0f;
		style.WindowPadding = ImVec2(kFramePadding, kFramePadding);
		style.TabBorderSize = active.borderThickness;
		style.WindowRounding = 0.0f;
		style.FrameRounding = 0.0f;

		{
			static const std::array<std::string, kRoleCount> kNone{};
			const auto& all = settings::Get().themeColors;
			const auto found = all.find(active.id);
			Look look;
			BuildLook(active, found != all.end() ? found->second : kNone, look);
			for (int i = 0; i < ImGuiCol_COUNT; ++i) { style.Colors[i] = look.colors[i]; }
			for (int r = 0; r < kRoleCount; ++r) { g_roleU32[r] = look.role[r]; g_themeRoleU32[r] = look.themeRole[r]; }
			g_headerTextU32 = look.header;
			g_helpTextU32 = look.help;
			logger::debug("theme colours ({}): {}", active.id, look.picked.empty() ? std::string("all the theme's own") : "the player's " + look.picked);
		}

		logger::info("Theme applied: \"{}\" ({}); frame={}; game HUD opacity {:.2f}; window opacity {}% (see-through {})",
					 active.name, active.id, !active.knotwork ? "none" : (active.mapEdge ? "map edge" : "knotwork"),
					 GetGameHUDOpacity(), settings::Get().windowOpacity, settings::Get().seeThrough ? "on" : "off");
	}

	std::uint32_t HeaderTextColor() { return g_headerTextU32; }
	std::uint32_t HelpTextColor() { return g_helpTextU32; }
	std::array<std::string, kRoleCount>& PlayerColors() { return settings::Get().themeColors[GetActiveTheme().id]; }
	bool ParseHexColor(std::string_view a_hex, std::uint32_t& a_out) { return ParseColor(a_hex, a_out); }

	bool RolePicked(int a_role)
	{
		if (a_role < 0 || a_role >= kRoleCount) { return false; }
		const auto& all = settings::Get().themeColors;
		const auto found = all.find(GetActiveTheme().id);
		return found != all.end() && !found->second[a_role].empty();
	}

	std::uint32_t RoleColor(int a_role) { return a_role >= 0 && a_role < kRoleCount ? g_roleU32[a_role] : 0xFFFFFFFF; }
	std::uint32_t ThemeRoleColor(int a_role) { return a_role >= 0 && a_role < kRoleCount ? g_themeRoleU32[a_role] : 0xFFFFFFFF; }

	// THE COLOURS PAGE'S PREVIEW IN UNAPPLIED PICKS (Skyrim 2.1.5): swap the draft look in for the preview's widgets, then put
	// the real one back. ImGui takes a colour when an item is drawn, so the rest of the menu keeps the applied look.
	namespace
	{
		ImVec4 g_savedColors[ImGuiCol_COUNT];
		std::uint32_t g_savedRole[kRoleCount], g_savedThemeRole[kRoleCount], g_savedHeader = 0, g_savedHelp = 0;
		bool g_previewing = false;
		Look g_previewLook;
	}

	void BeginPreviewColors(const std::array<std::string, kRoleCount>& a_picks)
	{
		if (g_previewing) { return; }
		BuildLook(GetActiveTheme(), a_picks, g_previewLook);
		ImGuiStyle& style = ImGui::GetStyle();
		for (int i = 0; i < ImGuiCol_COUNT; ++i) { g_savedColors[i] = style.Colors[i]; style.Colors[i] = g_previewLook.colors[i]; }
		for (int r = 0; r < kRoleCount; ++r)
		{
			g_savedRole[r] = g_roleU32[r]; g_roleU32[r] = g_previewLook.role[r];
			g_savedThemeRole[r] = g_themeRoleU32[r]; g_themeRoleU32[r] = g_previewLook.themeRole[r];
		}
		g_savedHeader = g_headerTextU32; g_headerTextU32 = g_previewLook.header;
		g_savedHelp = g_helpTextU32; g_helpTextU32 = g_previewLook.help;
		g_previewing = true;
	}

	void EndPreviewColors()
	{
		if (!g_previewing) { return; }
		ImGuiStyle& style = ImGui::GetStyle();
		for (int i = 0; i < ImGuiCol_COUNT; ++i) { style.Colors[i] = g_savedColors[i]; }
		for (int r = 0; r < kRoleCount; ++r) { g_roleU32[r] = g_savedRole[r]; g_themeRoleU32[r] = g_savedThemeRole[r]; }
		g_headerTextU32 = g_savedHeader;
		g_helpTextU32 = g_savedHelp;
		g_previewing = false;
	}

	std::uint32_t PicksRoleColor(const std::array<std::string, kRoleCount>& a_picks, int a_role)
	{
		Look look;
		BuildLook(GetActiveTheme(), a_picks, look);
		return a_role >= 0 && a_role < kRoleCount ? look.role[a_role] : 0xFFFFFFFF;
	}
}
