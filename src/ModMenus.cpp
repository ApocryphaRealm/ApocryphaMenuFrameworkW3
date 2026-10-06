#include "PCH.h"

#include "ModMenus.h"

#include "ModMenusParse.h"
#include "Paths.h"
#include "PreciseSlider.h"
#include "Red3.h"
#include "Renderer.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <fstream>
#include <iterator>
#include <thread>

namespace modmenus
{
	namespace
	{
		namespace fs = std::filesystem;
		using strings::TR;

		struct Page
		{
			std::string              mod;
			std::string              name;
			std::vector<std::size_t> groups;   // into g_groups, drawn in order
		};

		// Written once by the loader thread before any page is registered; read-only afterwards.
		std::vector<Group> g_groups;
		StringTable        g_strings;
		std::vector<Page>  g_pages;

		// The values, re-read on the render thread when a settings file changes.
		struct SettingsFile
		{
			fs::path        path;
			fs::file_time_type stamp{};
			Settings        values;
			bool            present = false;
		};
		SettingsFile                          g_dx12;   // dx12user.settings - the Remastered DX12 game's own
		SettingsFile                          g_legacy; // user.settings - read only for a group dx12user.settings lacks
		std::chrono::steady_clock::time_point g_lastCheck{};

		// what StatusJson reports
		std::atomic_bool g_done{ false };
		std::string      g_menuDir;
		std::size_t      g_files = 0, g_csvFiles = 0, g_w3sFiles = 0, g_varCount = 0, g_labelled = 0;
		long long        g_ms = 0;

		std::string ReadFile(const fs::path& a_path)
		{
			std::ifstream f(a_path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(f), {});
		}

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_s;
		}

		// The game folder, from the EXE: bin\x64_dx12\witcher3.exe -> the game root. Not from the .asi - under Mod Organizer 2
		// the .asi reports its real mod-folder path, and the menus were looked for inside AMF's own mod (1.0.1 first run,
		// 2026-10-05). Listing the game's folders from inside the process goes through the virtual folder, so every mod's
		// files appear merged there.
		fs::path GameRoot()
		{
			wchar_t exe[MAX_PATH]{};
			const DWORD n = ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			if (n == 0 || n >= MAX_PATH) {
				return paths::Data().parent_path().parent_path().parent_path();   // never expected; the log shows the path used
			}
			return fs::path(exe).parent_path().parent_path().parent_path();
		}

		fs::path DocumentsFolder()
		{
			PWSTR       docs = nullptr;
			fs::path    out;
			if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
				out = fs::path(docs) / "The Witcher 3";
			}
			if (docs) {
				::CoTaskMemFree(docs);
			}
			return out;
		}

		void Refresh(SettingsFile& a_file)
		{
			std::error_code ec;
			const auto      stamp = fs::last_write_time(a_file.path, ec);
			if (ec) {
				a_file.present = false;
				a_file.values.clear();
				return;
			}
			if (a_file.present && stamp == a_file.stamp) {
				return;
			}
			a_file.values = ParseSettings(DecodeText(ReadFile(a_file.path)));
			a_file.stamp = stamp;
			a_file.present = true;
			logger::info("mod menus: read {} ({} sections)", a_file.path.string(), a_file.values.size());
		}

		// Re-read the settings at most once a second while a page is drawn; the game rewrites them when its own menu closes.
		void RefreshValues()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - g_lastCheck < std::chrono::seconds(1)) {
				return;
			}
			g_lastCheck = now;
			Refresh(g_dx12);
			Refresh(g_legacy);
		}

		// ---- live values (M3): read from and written to the game through the engine bridge, on the game thread ----
		// The cache is what the pages draw; the game thread fills it. Edits go into g_dirty and one queued job writes them.
		std::mutex                                   g_liveLock;
		std::unordered_map<std::string, std::string> g_live;    // "group\x1fvar" -> value as the game holds it
		std::unordered_map<std::string, std::string> g_dirty;   // edits not yet written
		bool                                         g_flushQueued = false;
		std::vector<ULONGLONG>                       g_pageRead;   // per page: when its values were last asked for (render thread)

		std::string Key(const Group& a_group, const Var& a_var) { return a_group.id + '\x1f' + a_var.id; }

		void RequestRead(std::size_t a_page)
		{
			const ULONGLONG now = ::GetTickCount64();
			if (a_page >= g_pageRead.size() || now - g_pageRead[a_page] < 500) {
				return;
			}
			g_pageRead[a_page] = now;
			red3::Post([a_page] {
				for (const std::size_t gi : g_pages[a_page].groups) {
					const Group& g = g_groups[gi];
					for (const Var& v : g.vars) {
						if (v.type == "SUBTLE_SEPARATOR" || v.type == "SEPARATOR") {
							continue;
						}
						std::string value;
						if (!red3::GetVar(g.id, v.id, value)) {
							continue;   // not a var the game knows (yet), or the bridge is off: the file value stays
						}
						std::scoped_lock l(g_liveLock);
						if (!g_dirty.contains(Key(g, v))) {   // an edit not yet written wins over what the game held
							g_live[Key(g, v)] = value;
						}
					}
				}
			});
		}

		void Write(const Group& a_group, const Var& a_var, const std::string& a_value)
		{
			std::scoped_lock l(g_liveLock);
			const std::string key = Key(a_group, a_var);
			g_live[key] = a_value;
			g_dirty[key] = a_value;
			if (g_flushQueued) {
				return;
			}
			g_flushQueued = true;
			red3::Post([] {
				std::unordered_map<std::string, std::string> edits;
				{
					std::scoped_lock l2(g_liveLock);
					edits.swap(g_dirty);
					g_flushQueued = false;
				}
				for (const auto& [key, value] : edits) {
					const std::size_t sep = key.find('\x1f');
					red3::SetVar(key.substr(0, sep), key.substr(sep + 1), value);
				}
				red3::RequestSave();
			});
		}

		const std::string* ValueOf(const Group& a_group, const Var& a_var)
		{
			if (red3::ConfigReady()) {
				std::scoped_lock l(g_liveLock);
				if (const auto it = g_live.find(Key(a_group, a_var)); it != g_live.end()) {
					static thread_local std::string s_value;   // the render thread's copy, valid until the next call
					s_value = it->second;
					return s_value.empty() ? nullptr : &s_value;
				}
			}
			for (const SettingsFile* f : { &g_dx12, &g_legacy }) {
				const auto sec = f->values.find(a_group.id);
				if (sec == f->values.end()) {
					continue;
				}
				const auto it = sec->second.find(a_var.id);
				return it != sec->second.end() ? &it->second : nullptr;   // the first file holding the group decides
			}
			return nullptr;
		}

		bool IsSeparator(const Var& a_var) { return a_var.type == "SUBTLE_SEPARATOR" || a_var.type == "SEPARATOR"; }

		void DrawSeparator(const Var& a_var)
		{
			bool              labelled = false;
			const std::string label = VarLabel(a_var, g_strings, &labelled);
			// a SEPARATOR names a section (Brothers In Arms: "Act 1"); a SUBTLE_SEPARATOR is usually just a line
			if (a_var.type == "SEPARATOR" || (labelled && !a_var.label.empty())) {
				ImGui::SeparatorText(label.c_str());
			} else {
				ImGui::Separator();
			}
		}

		// One row of the page's table: the label wrapped in the left column (with "not set yet" under it), the control
		// filling the right one. Labels used to sit right of the control and were cut off at the window edge (1.0.1 run).
		void DrawVar(const Group& a_group, const Var& a_var)
		{
			const std::string  label = VarLabel(a_var, g_strings);
			const std::string* raw = ValueOf(a_group, a_var);
			// The game answers "-1" for a choice list that was never set (Auto Take All, 1.0.2 run): that is "not set",
			// unless the mod really has an option whose value is -1.
			if (raw && a_var.type == "OPTIONS" && *raw == "-1" &&
				std::none_of(a_var.options.begin(), a_var.options.end(), [](const Option& o) { return o.value == "-1"; })) {
				raw = nullptr;
			}
			const std::string  id = "##" + a_group.id + "." + a_var.id;
			const bool         readOnly = !red3::ConfigReady();

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			const ImVec2 rowMin = ImGui::GetCursorScreenPos();   // the row's highlight starts at the label
			ImGui::AlignTextToFramePadding();
			ImGui::TextWrapped("%s", label.c_str());
			if (!raw) {
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%s", TR("AMF_W3MenuNotSet", "(not set yet - the mod uses its own default)"));
				ImGui::PopTextWrapPos();
			}
			float rowBottom = ImGui::GetItemRectMax().y;

			ImGui::TableSetColumnIndex(1);
			const ImVec2 controlMin = ImGui::GetCursorScreenPos();
			const float  rowRight = controlMin.x + ImGui::GetContentRegionAvail().x;
			rowBottom = std::max(rowBottom, controlMin.y + ImGui::GetFrameHeight());
			// ONE HIGHLIGHT FOR THE ROW (W3 1.0.3 test, 2026-10-05: the frame went round the switch alone, away from its label in
			// the left column). The control stays the nav item - A, a slider's take-hold and a list's opening are unchanged - but
			// its own frame is drawn with a clear colour, and the frame is drawn once round the label and the control together.
			// The colour is put back straight after the control, before a list's options are drawn, so they keep theirs.
			ImGuiStyle&  style = ImGui::GetStyle();
			const ImVec4 navColour = style.Colors[ImGuiCol_NavHighlight];
			style.Colors[ImGuiCol_NavHighlight].w = 0.0f;
			bool         ownFrameHidden = true;
			const auto   ownFrameBack = [&] {
				if (ownFrameHidden) {
					style.Colors[ImGuiCol_NavHighlight] = navColour;
					ownFrameHidden = false;
				}
			};
			ImGuiID controlId = 0;
			ImGui::SetNextItemWidth(-FLT_MIN);
			// read-only: reachable by D-pad and mouse, never changed. Editable once the engine bridge is up (M3): a change is
			// set through the game's own SetVarValue and saved like Options > Mods saves.
			ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, readOnly);
			if (a_var.type == "TOGGLE") {
				bool on = raw && (_stricmp(raw->c_str(), "true") == 0 || *raw == "1");
				const bool flipped = widgets::Toggle(id.c_str(), &on, readOnly);
				controlId = ImGui::GetItemID();   // the switch (a "##" label draws no text after it)
				ownFrameBack();
				if (flipped && !readOnly) {
					Write(a_group, a_var, on ? "true" : "false");
				}
			} else if (a_var.type == "SLIDER") {
				float v = static_cast<float>(a_var.min);
				if (raw) {
					char* end = nullptr;
					const double d = std::strtod(raw->c_str(), &end);
					if (end != raw->c_str()) {
						v = static_cast<float>(d);
					}
				}
				const int         decimals = SliderDecimals(a_var);
				const std::string format = std::format("%.{}f", decimals);
				const bool moved = precise::SliderFloat(id.c_str(), &v, static_cast<float>(a_var.min), static_cast<float>(a_var.max), format.c_str());
				controlId = ImGui::GetItemID();
				ownFrameBack();
				if (moved && !readOnly) {
					Write(a_group, a_var, std::format("{:.{}f}", v, decimals));
				}
			} else if (a_var.type == "OPTIONS") {
				std::string preview = raw ? *raw : std::string{};
				int         current = -1;
				for (std::size_t i = 0; raw && i < a_var.options.size(); ++i) {
					if (a_var.options[i].value == *raw) {
						current = static_cast<int>(i);
						preview = OptionLabel(a_var.options[i], g_strings);
						break;
					}
				}
				// The list's own vertical padding comes from the window style, which the framed themes make large - it showed
				// as an empty row above the first option. Frame padding is enough for a dropdown.
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().FramePadding);
				controlId = ImGui::GetID(id.c_str());   // BeginCombo's own id; with the list open the last item is its popup
				const bool listOpen = ImGui::BeginCombo(id.c_str(), preview.c_str());
				ownFrameBack();
				ImGui::PopStyleVar();
				if (listOpen) {
					for (std::size_t i = 0; i < a_var.options.size(); ++i) {
						const bool selected = static_cast<int>(i) == current;
						if (ImGui::Selectable(OptionLabel(a_var.options[i], g_strings).c_str(), selected) && !readOnly && !selected) {
							Write(a_group, a_var, a_var.options[i].value);
						}
						if (selected) {
							ImGui::SetItemDefaultFocus();   // the D-pad starts on the current choice, not the first
						}
					}
					ImGui::EndCombo();
				}
			} else {
				// a type AMF does not draw yet (a key binding, a mod's own extension): the value as text
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(raw ? raw->c_str() : "");
			}
			ImGui::PopItemFlag();
			ownFrameBack();
			rowBottom = std::max(rowBottom, ImGui::GetItemRectMax().y);   // the control's own bottom (a wrapped value text)
			if (controlId != 0 && GImGui) {
				// THE FRAME SPANS BOTH COLUMNS (W3 1.0.4 - the tester's capture: it went round the control column only, as tall
				// as the label and its "not set yet" line). ImGui::RenderNavHighlight first CLIPS the rect to the current
				// window's clip rect, and inside a table cell that is the cell's column - so the label column was cut off
				// before the frame was drawn. It is drawn here instead, under the same conditions as ImGui's own (this control
				// is the nav item, highlights are shown, not hidden for this frame), with the TABLE's clip pushed on the draw
				// list so nothing narrows it to the cell: from the label cell's left to the control cell's right.
				ImGuiContext&      g = *GImGui;
				ImGuiWindow* const window = g.CurrentWindow;
				ImGuiTable* const  table = ImGui::GetCurrentTable();
				const bool         show = g.NavId == controlId && !g.NavDisableHighlight && window && !window->DC.NavHideHighlightOneFrame;
				if (show && table) {
					constexpr float thickness = 2.0f;
					constexpr float distance = 3.0f + thickness * 0.5f;   // ImGui's own offset for a nav frame
					ImRect frame(rowMin, ImVec2(rowRight, rowBottom));
					frame.Expand(ImVec2(distance, distance));
					const ImRect clip = table->HostClipRect;   // the pane's visible area at BeginTable: both columns, nothing past it
					window->DrawList->PushClipRect(clip.Min, clip.Max, false);
					window->DrawList->AddRect(frame.Min, frame.Max, ImGui::GetColorU32(ImGuiCol_NavHighlight), g.Style.FrameRounding, 0, thickness);
					window->DrawList->PopClipRect();
				}
				static ImGuiID s_rowLogged = 0;   // render thread; logged when the highlight reaches another row
				if (g.NavId == controlId && s_rowLogged != controlId) {
					s_rowLogged = controlId;
					logger::debug("mod menus: highlight on row '{}' ({}.{}, {}) - one frame round label and control, x {:.0f}-{:.0f}, y {:.0f}-{:.0f}{}",
								  label, a_group.id, a_var.id, a_var.type, rowMin.x, rowRight, rowMin.y, rowBottom,
								  show ? (table ? "" : " (no table: not drawn)") : " (highlight hidden)");
				}
			}
		}

		void DrawPage(std::size_t a_index)
		{
			const Page& a_page = g_pages[a_index];
			RefreshValues();
			if (red3::ConfigReady()) {
				RequestRead(a_index);
			} else {
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%s", TR("AMF_W3MenuReadOnly",
											  "AMF could not reach the game's settings code on this version of the game, so these values are "
											  "read-only here. Change them in the game's Options > Mods."));
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
			}
			// Each run of settings between two separators is one two-column table: labels 55 %, controls 45 %.
			int  table = 0;
			bool open = false;
			bool firstVar = true;   // the pad lands on the page's first setting, not the tab bar's list button (W3 1.0.5 run)
			auto begin = [&] {
				open = ImGui::BeginTable(std::format("##vars{}", table++).c_str(), 2, ImGuiTableFlags_SizingStretchProp);
				if (open) {
					ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.55f);
					ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 0.45f);
				}
			};
			auto end = [&] {
				if (open) {
					ImGui::EndTable();
					open = false;
				}
			};
			for (const std::size_t gi : a_page.groups) {
				const Group& g = g_groups[gi];
				ImGui::PushID(g.id.c_str());
				for (const Var& v : g.vars) {
					if (IsSeparator(v)) {
						end();
						DrawSeparator(v);
						continue;
					}
					if (!open) {
						begin();
					}
					if (open) {
						DrawVar(g, v);
						if (firstVar) {
							firstVar = false;
							ImGui::SetItemDefaultFocus();   // where nav starts when the pane is entered
						}
					}
				}
				end();
				ImGui::PopID();
			}
		}

		// Every file of the given extension under a folder, through the virtual folder Mod Organizer 2 lays over it.
		std::vector<fs::path> FilesUnder(const fs::path& a_dir, const char* a_ext)
		{
			std::vector<fs::path> out;
			std::error_code       ec;
			if (!fs::is_directory(a_dir, ec)) {
				return out;
			}
			for (fs::recursive_directory_iterator it(a_dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
				it.increment(ec)) {
				if (it->is_regular_file(ec) && _stricmp(it->path().extension().string().c_str(), a_ext) == 0) {
					out.push_back(it->path());
				}
			}
			if (ec) {
				logger::warn("mod menus: listing {} stopped early ({})", a_dir.string(), ec.message());
			}
			return out;
		}

		void Load()
		{
			const auto start = std::chrono::steady_clock::now();
			const fs::path root = GameRoot();
			const fs::path menuDir = root / "bin" / "config" / "r4game" / "user_config_matrix" / "pc";
			g_menuDir = menuDir.string();

			// 1. the menus
			std::error_code ec;
			for (fs::directory_iterator it(menuDir, ec), end; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || _stricmp(it->path().extension().string().c_str(), ".xml") != 0) {
					continue;
				}
				++g_files;
				const std::string  file = it->path().filename().string();
				std::vector<Group> parsed = ParseMenuXml(DecodeText(ReadFile(it->path())), file);
				std::vector<Group> mine;
				for (Group& g : parsed) {
					if (IsModGroup(g)) {
						mine.push_back(std::move(g));
					}
				}
				if (mine.empty()) {
					logger::debug("mod menus: {} - {} group(s), none under Mods. (the game's own)", file, parsed.size());
					continue;
				}
				Place(mine);
				logger::info("mod menus: {} - {} page(s)", file, mine.size());
				for (Group& g : mine) {
					g_groups.push_back(std::move(g));
				}
			}
			if (ec) {
				logger::warn("mod menus: could not list {} ({})", menuDir.string(), ec.message());
			}

			// 2. the labels: the keys the menus use, from the mods' string CSVs (English first; another language fills gaps)
			std::unordered_map<std::string, bool> wanted;
			for (const Group& g : g_groups) {
				for (const std::string& k : NeededKeys(g)) {
					wanted[Lower(k)] = true;
				}
			}
			if (!g_groups.empty()) {
				std::vector<fs::path> csvs = FilesUnder(root / "Mods", ".csv");
				for (fs::path& p : FilesUnder(root / "dlc", ".csv")) {
					csvs.push_back(std::move(p));
				}
				g_csvFiles = csvs.size();
				for (int pass = 0; pass < 2; ++pass) {
					for (const fs::path& c : csvs) {
						const std::string text = DecodeText(ReadFile(c));
						if ((CsvLanguage(text, c.filename().string()) == "en") == (pass == 0)) {
							ReadStringsCsv(text, wanted, g_strings);
						}
					}
				}
				// then the compiled string files - most mods ship only these. English only: another author's text stays
				// in English (owner's rule for third-party settings text).
				std::unordered_map<std::uint32_t, bool> wantedHashes;
				for (const auto& [k, _] : wanted) {
					if (!g_strings.byKey.contains(k)) {
						wantedHashes[KeyHash(k)] = true;
					}
				}
				std::vector<fs::path> w3s = FilesUnder(root / "Mods", ".w3strings");
				for (fs::path& p : FilesUnder(root / "dlc", ".w3strings")) {
					w3s.push_back(std::move(p));
				}
				for (const fs::path& w : w3s) {
					if (_stricmp(w.filename().string().c_str(), "en.w3strings") != 0) {
						continue;
					}
					++g_w3sFiles;
					if (ReadW3Strings(ReadFile(w), wantedHashes, g_strings) < 0) {
						logger::warn("mod menus: {} is not a string file AMF can read", w.string());
					}
				}
			}

			// 3. the pages: one AMF entry per mod, one tab per page; groups that land on the same page are drawn together
			for (std::size_t gi = 0; gi < g_groups.size(); ++gi) {
				const Group& g = g_groups[gi];
				const std::string mod = g.path.size() > g.modIndex ? PanelLabel(g.path[g.modIndex], g_strings) : g.id;
				std::string       page;
				for (std::size_t i = g.modIndex + 1; i < g.path.size(); ++i) {
					page += (page.empty() ? "" : " / ") + PanelLabel(g.path[i], g_strings);
				}
				if (page.empty()) {
					page = TR("AMF_W3MenuSettingsPage", "Settings");
				}
				auto it = std::find_if(g_pages.begin(), g_pages.end(), [&](const Page& p) { return p.mod == mod && p.name == page; });
				if (it == g_pages.end()) {
					g_pages.push_back({ mod, page, {} });
					it = g_pages.end() - 1;
				}
				it->groups.push_back(gi);
				for (const Var& v : g.vars) {
					if (v.type == "SUBTLE_SEPARATOR" || v.type == "SEPARATOR") {
						continue;
					}
					++g_varCount;
					bool found = false;
					VarLabel(v, g_strings, &found);
					g_labelled += found;
				}
			}

			const fs::path docs = DocumentsFolder();
			g_dx12.path = docs / "dx12user.settings";
			g_legacy.path = docs / "user.settings";
			Refresh(g_dx12);
			Refresh(g_legacy);

			g_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			g_pageRead.assign(g_pages.size(), 0);
			std::size_t registered = 0;
			for (std::size_t i = 0; i < g_pages.size(); ++i) {
				registered += registry::RegisterBuilt(g_pages[i].mod, g_pages[i].name, [i] { DrawPage(i); });
			}
			logger::info("mod menus: {} menu file(s) in {}, {} mod page(s) registered, {} of {} settings labelled from {} CSV and {} "
						 ".w3strings file(s), {} ms",
				g_files, g_menuDir, registered, g_labelled, g_varCount, g_csvFiles, g_w3sFiles, g_ms);
			g_done = true;
			ApplyImportChoices();   // the mods the player switched off are hidden before the menu is first opened
		}

		std::string Escape(const std::string& a_s)
		{
			std::string out;
			for (const char c : a_s) {
				if (c == '\\' || c == '"') {
					out += '\\';
				}
				out += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
			}
			return out;
		}
	}

	void Start()
	{
		std::thread([] {
			try {
				red3::Resolve();   // the engine bridge first: the pages are editable only when it is up
				// The framework's entry in the game's own menu (Mods\modApocryphaMenuFramework's script) sets the hidden
				// setting ApocryphaMenuFramework.OpenRequest; read it a few times a second on the game thread, open the
				// window and clear it. A controller player's way in (the owner, 2026-10-05: no pad button - "it should just
				// be on the menu ... just like it is in Skyrim").
				red3::AddFrameHook([] {
					static ULONGLONG s_next = 0;
					static ULONGLONG s_firstMiss = 0;
					static int       s_state = 0;   // 0 not found yet, 1 found, 2 warned
					const ULONGLONG  now = ::GetTickCount64();
					if (now < s_next || !red3::ConfigReady()) {
						return;
					}
					s_next = now + 200;
					std::string value;
					if (!red3::GetVar("ApocryphaMenuFramework", "OpenRequest", value)) {
						// The game loads its config matrix after the first frames, so early misses are expected (rule 17: retry,
						// don't warn) - the 1.0.3 run logged a false "the XML is missing" at start-up while the entry worked.
						// Warned only if it is still not there two minutes after the first look.
						if (s_firstMiss == 0) {
							s_firstMiss = now;
							logger::debug("game menu entry: ApocryphaMenuFramework.OpenRequest not loaded yet - asking again");
						} else if (s_state == 0 && now - s_firstMiss > 120000) {
							s_state = 2;
							logger::warn("game menu entry: the setting ApocryphaMenuFramework.OpenRequest is still not there after "
										 "two minutes - bin\\config\\r4game\\user_config_matrix\\pc\\ApocryphaMenuFramework.xml is "
										 "probably missing, so the entry in the game's menu cannot open the framework");
						}
						return;
					}
					if (s_state != 1) {
						s_state = 1;
						logger::info("game menu entry: ready (the game's setting ApocryphaMenuFramework.OpenRequest is loaded)");
					}
					if (_stricmp(value.c_str(), "true") == 0 || value == "1") {
						red3::SetVar("ApocryphaMenuFramework", "OpenRequest", "false");
						renderer::SetMenuVisible(true);
						logger::info("game menu entry: the framework opened from the game's own menu");
					}
				});
				// [Menu] bSkipIntro: the game lists its start-up videos in its own script (CR4StartupMoviesMenu), which the
				// framework's script empties while the hidden setting ApocryphaMenuFramework.SkipIntro is "true". Keep that
				// setting equal to AMF's, about once a second; the game saves it with its own settings, so it holds at the
				// next start, before AMF is up.
				red3::AddFrameHook([] {
					static ULONGLONG s_next = 0;
					const ULONGLONG  now = ::GetTickCount64();
					// NO WRITES IN THE FIRST 30 SECONDS (1.0.0 run, 2026-10-05: writing the loading-screen setting about a second
					// after the config was ready faulted inside the game's SetVarValue, and the game's crash reporter came up
					// although the bridge's guard caught it). Reads are fine; every write below waits until the game is well up.
					static const ULONGLONG s_start = now;
					if (now < s_next || !red3::ConfigReady()) {
						return;
					}
					s_next = now + 1000;
					const bool settled = now - s_start >= 30000;
					std::string value;
					if (!red3::GetVar("ApocryphaMenuFramework", "SkipIntro", value)) {
						return;   // not loaded yet, or the XML is missing (the OpenRequest hook above warns about that)
					}
					const bool want = settings::Get().skipIntro;
					const bool have = _stricmp(value.c_str(), "true") == 0 || value == "1";
					if (settled && have != want && red3::SetVar("ApocryphaMenuFramework", "SkipIntro", want ? "true" : "false")) {
						red3::RequestSave();
						logger::info("intro: the game's start-up videos are {} from the next start (ApocryphaMenuFramework.SkipIntro = {})",
							want ? "skipped" : "played", want ? "true" : "false");
					}
					// [Menu] bSkipLoadingRecap. The engine's [LoadingScreen/Debug] DisableVideos is not saved between sessions, so
					// it is applied at every start - by the framework's SCRIPT, when the game's main menu opens (before any save
					// can load), from the plain hidden setting ApocryphaMenuFramework.SkipLoadingRecap kept equal to the switch
					// here. AMF itself writes DisableVideos (the overrideGroup var) only when the switch is flipped in this
					// session, so the change applies at the next load without a restart; never at start-up, and never with the
					// switch left off, so a player who set it another way (Fast Launch's engine.ini) keeps it.
					const bool recapWant = settings::Get().skipLoadingRecap;
					std::string plain;
					if (settled && red3::GetVar("ApocryphaMenuFramework", "SkipLoadingRecap", plain)) {
						const bool plainHave = _stricmp(plain.c_str(), "true") == 0 || plain == "1";
						if (plainHave != recapWant && red3::SetVar("ApocryphaMenuFramework", "SkipLoadingRecap", recapWant ? "true" : "false")) {
							red3::RequestSave();
							logger::info("loading recap: the switch is {} (ApocryphaMenuFramework.SkipLoadingRecap = {}); the game's script applies it "
										 "at the main menu of every start", recapWant ? "on" : "off", recapWant ? "true" : "false");
						}
					}
					static int s_lastRecapWant = -1;
					if (s_lastRecapWant < 0) {
						s_lastRecapWant = recapWant ? 1 : 0;   // the state at start: nothing to change in this session yet
					}
					if (settled && (recapWant ? 1 : 0) != s_lastRecapWant) {
						std::string recap;
						if (red3::GetVar("ApocryphaMenuFramework", "DisableVideos", recap)) {
							const bool recapHave = _stricmp(recap.c_str(), "true") == 0 || recap == "1";
							if (recapHave == recapWant ||
								red3::SetVar("ApocryphaMenuFramework", "DisableVideos", recapWant ? "true" : "false")) {
								logger::info("loading recap: the story recap on loading screens is {} from the next load ([LoadingScreen/Debug] DisableVideos = {})",
									recapWant ? "skipped" : "played again", recapWant ? "true" : "false");
							}
						}
						s_lastRecapWant = recapWant ? 1 : 0;
					}
				});
				Load();
			} catch (const std::exception& e) {
				logger::error("mod menus: reading failed ({}); no mod menu pages this session", e.what());
				g_done = true;
			}
		}).detach();
	}

	std::vector<ModInfo> Mods()
	{
		std::vector<ModInfo> out;
		if (!g_done) {
			return out;
		}
		for (const Page& p : g_pages) {
			auto it = std::find_if(out.begin(), out.end(), [&](const ModInfo& m) { return m.entry == p.mod; });
			if (it == out.end()) {
				ModInfo m;
				m.entry = p.mod;
				if (!p.groups.empty()) {
					const Group& g = g_groups[p.groups.front()];
					m.key = g.path.size() > g.modIndex ? g.path[g.modIndex] : g.id;
				}
				out.push_back(std::move(m));
				it = out.end() - 1;
			}
			it->pages.push_back(p.name);
			for (const std::size_t gi : p.groups) {
				const std::string stem = fs::path(g_groups[gi].file).stem().string();
				if (std::find(it->names.begin(), it->names.end(), stem) == it->names.end()) {
					it->names.push_back(stem);
				}
			}
		}
		return out;
	}

	std::string StatusJson()
	{
		if (!g_done) {
			return R"({"done":false})";
		}
		std::string pages;
		for (const Page& p : g_pages) {
			std::string groups;
			for (const std::size_t gi : p.groups) {
				groups += (groups.empty() ? "\"" : ",\"") + Escape(g_groups[gi].id) + "\"";
			}
			pages += std::format(R"({}{{"mod":"{}","page":"{}","groups":[{}]}})", pages.empty() ? "" : ",", Escape(p.mod), Escape(p.name), groups);
		}
		return std::format(
			R"({{"done":true,"menuDir":"{}","files":{},"pages":[{}],"settings":{},"labelled":{},"csvFiles":{},"w3stringsFiles":{},"ms":{},"dx12user":{},"user":{},"engine":{}}})",
			Escape(g_menuDir), g_files, pages, g_varCount, g_labelled, g_csvFiles, g_w3sFiles, g_ms, g_dx12.present ? "true" : "false",
			g_legacy.present ? "true" : "false", red3::StatusJson());
	}
}
