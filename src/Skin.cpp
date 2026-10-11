#include "PCH.h"

#include "Skin.h"

#include "Paths.h"

#include <cstring>

#include "Gfx.h"
#include "Settings.h"
#include "Theme.h"

#include "Logger.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace skin
{
	namespace
	{
		struct Entry
		{
			std::string path;      // as configured, after resolution; empty = not configured
			void*       srv = nullptr;
			ImVec2      size{ 0.0f, 0.0f };
			int         mips = 0;   // Witcher 3 1.0.5: the levels uploaded
			std::string error;     // why it is not loaded, for StatusJson and the log
		};

		Entry g_frame;
		Entry g_background;
		std::array<Entry, static_cast<std::size_t>(Plate::kCount)> g_plates;
		Entry g_toggleEdge;   // Skyrim 2.1.6: the switch track's outline (<name>-edge.png), drawn in its own colours
		float g_frameCorner = 64.0f;
		// Skyrim 2.1.6: the frame's cut from its .ini, and what the library resolved to (for the Art page and StatusJson)
		float g_frameDrawCorner = 0.0f;
		float g_frameBand = 0.0f;
		bool  g_frameTiles = false;
		bool  g_frameHighlightCorners = false;
		bool  g_frameNone = false;   // the player picked "none" for the frame - not even the built-in knotwork
		std::array<std::string, kArtKindCount> g_activeArt;
		std::array<ArtPart, kArtKindCount> g_parts;   // the control kinds (Box .. Cursor); Frame / Background / Switch above
		std::array<bool, kArtKindCount> g_partLoaded{};
		// The names and entries above are rewritten by Reload (the render thread, or DevBench's skinreload / theme ops on the
		// listener thread) and read by op=state / op=art on the listener thread: those readers and Reload take this lock.
		std::recursive_mutex g_stateLock;

		// Witcher 3: the library sits beside the theme folder, bin\x64_dx12\AMF\assets. Written the way Resolve() reads an
		// AMF-relative path, so a library part and a theme's own path resolve the same way.
		constexpr const char* kAssetDir = "AMF/assets";

		constexpr const char* kPlateFile[] = { "toggle.png", "slider.png", "tab.png" };
		constexpr const char* kPlateName[] = { "toggle", "slider", "tab" };

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		// The library folder on disk (bin\x64_dx12\AMF\assets\<folder>).
		std::filesystem::path AssetFolder(const char* a_folder)
		{
			return paths::Data() / "assets" / a_folder;
		}

		// An author writes a path the way they think about their own mod. On Oblivion Remastered that is
		// relative to the game's working folder (Binaries\Win64, where OBSE\Plugins lives) - Skyrim resolved
		// it under Data, and that prefix left the theme art at "Data\OBSE\..." where nothing is (first M2 run,
		// 2026-09-26). An absolute path is accepted too, so a work-in-progress file can be pointed at.
		// Witcher 3: a relative path is relative to the .asi's folder (bin\x64_dx12), never the working folder (the game
		// runs from bin\ - M1, 2026-10-05). A path written for the Oblivion or Skyrim AMF ("OBSE/Plugins/ApocryphaMenuFramework/..."
		// or "SKSE/Plugins/ApocryphaMenuFramework/...") maps onto AMF\, so a theme made for those builds works here unchanged.
		std::string Resolve(const std::string& a_configured)
		{
			if (a_configured.empty()) { return {}; }
			std::filesystem::path p(a_configured);
			p.make_preferred();
			if (p.is_absolute()) { return p.string(); }
			std::string rel = p.generic_string();
			for (const char* legacy : { "OBSE/Plugins/ApocryphaMenuFramework/", "SKSE/Plugins/ApocryphaMenuFramework/", "AMF/" }) {
				const std::size_t n = std::strlen(legacy);
				if (rel.size() > n && _strnicmp(rel.c_str(), legacy, n) == 0) {
					return (paths::Data() / std::filesystem::path(rel.substr(n)).make_preferred()).string();
				}
			}
			return (paths::Data().parent_path() / p).string();
		}

		// ONE TEXTURE PER FILE, loaded once and kept for the session (Skyrim 2.1.6's cache): Reload and the Art page's
		// pictures ask for the same few small PNGs again and again, and a fresh texture each time would never be freed. Every
		// texture here carries a CPU-built mip chain (gfx::CreateTextureRGBAMips), so art drawn far below its own size - a
		// frame's corner round a small popup, the 4K-sharp parts at 1080p - is filtered, not sparkling. A file that is missing
		// or does not decode is remembered too, so its warning is logged once, never per frame. Guarded: DevBench's
		// skinreload runs Reload on its listener thread.
		std::mutex g_cacheLock;

		ArtImage CachedTexture(const std::string& a_resolved)
		{
			if (a_resolved.empty()) { return {}; }
			std::scoped_lock lock(g_cacheLock);
			static std::map<std::string, ArtImage> cache;
			if (const auto it = cache.find(a_resolved); it != cache.end()) { return it->second; }
			if (!gfx::Ready()) { return {}; }   // before the device: not remembered, the next Reload tries again
			ArtImage img;
			std::error_code ec;
			if (!std::filesystem::exists(a_resolved, ec))
			{
				logger::warn("skin: \"{}\" does not exist", a_resolved);
			}
			else
			{
				std::vector<std::uint8_t> rgba;
				int w = 0, h = 0;
				if (!gfx::DecodeImageFile(std::filesystem::path(a_resolved).wstring(), rgba, w, h))
				{
					logger::warn("skin: \"{}\" could not be decoded - is it a real 32-bit RGBA PNG?", a_resolved);
				}
				else if (void* srv = gfx::CreateTextureRGBAMips(rgba.data(), w, h, &img.mips); srv)
				{
					img.srv = srv;
					img.w = static_cast<float>(w);
					img.h = static_cast<float>(h);
					logger::info("skin: \"{}\" loaded ({}x{}, {} mip levels)", a_resolved, w, h, img.mips);
				}
				else
				{
					logger::warn("skin: \"{}\" decoded but the GPU upload failed (see the lines above)", a_resolved);
				}
			}
			cache.emplace(a_resolved, img);
			return img;
		}

		// Takes an ALREADY-RESOLVED path. It used to call Resolve() itself, which double-prefixed
		// the plate files ("Data\Data\Interface\...") because the plate loop resolves the folder
		// before appending the file name. Resolving happens at the call sites now, exactly once.
		void Load(Entry& a_entry, const std::string& a_path, const char* a_what)
		{
			a_entry = Entry{};
			if (a_path.empty()) { return; }

			a_entry.path = a_path;

			// DDS is the one an author is most likely to reach for, being Skyrim's own texture
			// format, and it is exactly the one that cannot work here. Say so by name.
			if (Lower(std::filesystem::path(a_entry.path).extension().string()) == ".dds")
			{
				a_entry.error = "DDS is not decoded - save it as a 32-bit RGBA PNG";
				logger::warn("skin: {} \"{}\" is a .dds. AMF decodes PNG (WIC), not DDS - export it as a "
							 "32-bit RGBA PNG instead.", a_what, a_entry.path);
				return;
			}

			std::error_code ec;
			if (!std::filesystem::exists(a_entry.path, ec))
			{
				a_entry.error = "file not found";
				logger::warn("skin: {} \"{}\" does not exist", a_what, a_entry.path);
				return;
			}

			const ArtImage img = CachedTexture(a_entry.path);
			if (!img.srv)
			{
				a_entry.error = "could not decode";
				return;   // CachedTexture said why, once
			}

			a_entry.srv = img.srv;
			a_entry.size = ImVec2(img.w, img.h);
			a_entry.mips = img.mips;
			logger::info("skin: {} from \"{}\" ({:.0f}x{:.0f}, {} mip levels)", a_what, a_entry.path, img.w, img.h, img.mips);
		}

		// assets/<folder>/<name>.png, as a path Resolve() reads.
		std::string ArtPath(ArtKind a_kind, const std::string& a_name)
		{
			return std::format("{}/{}/{}.png", kAssetDir, kArtFolders[static_cast<std::size_t>(a_kind)], a_name);
		}

		// A part's cut, from the <name>.ini beside it. A missing file or key keeps the old behaviour: the corner the
		// caller had, drawn at its own size, edges stretched, the whole frame round a highlighted item.
		struct FrameCut
		{
			std::uint32_t corner = 0;
			std::uint32_t drawCorner = 0;
			std::uint32_t band = 0;   // uBand - how far a frame's art reaches in from its edge
			bool tile = false;
			bool highlightCorners = false;
			float hotX = 0.0f, hotY = 0.0f;
			bool ownColours = false;   // bOwnColours - the edge layer is drawn as painted, not in the line colour
			bool threeSlice = false;   // bThreeSlice - a grab: a square cap at each end, the middle between them
		};

		FrameCut ReadCut(const char* a_folder, const std::string& a_name)
		{
			FrameCut cut;
			std::ifstream in(AssetFolder(a_folder) / (a_name + ".ini"));
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				const auto eq = line.find('=');
				if (line.empty() || line[0] == ';' || line[0] == '[' || eq == std::string::npos) { continue; }
				const std::string key = Lower(line.substr(0, eq));
				const std::string value = line.substr(eq + 1);
				try
				{
					if (key == "ucorner") { cut.corner = static_cast<std::uint32_t>(std::stoul(value)); }
					else if (key == "udrawcorner") { cut.drawCorner = static_cast<std::uint32_t>(std::stoul(value)); }
					else if (key == "uband") { cut.band = static_cast<std::uint32_t>(std::stoul(value)); }
					else if (key == "btileedges") { cut.tile = value == "1" || Lower(value) == "true"; }
					else if (key == "shighlight") { cut.highlightCorners = Lower(value) == "corners"; }
					else if (key == "uhotx") { cut.hotX = std::stof(value); }
					else if (key == "uhoty") { cut.hotY = std::stof(value); }
					else if (key == "bowncolours") { cut.ownColours = value == "1" || Lower(value) == "true"; }
					else if (key == "bthreeslice") { cut.threeSlice = value == "1" || Lower(value) == "true"; }
				}
				catch (...)
				{
					logger::warn("skin: {} \"{}\" .ini has a bad line \"{}\"", a_folder, a_name, line);
				}
			}
			return cut;
		}

		// The theme's own part of a kind.
		const std::string& PaletteArt(const theme::Palette& a_t, ArtKind a_kind)
		{
			return a_t.art[static_cast<std::size_t>(a_kind)];
		}

		// A control kind's part (Box .. Cursor) and its extra layers. Missing extras are not errors.
		bool LoadPart(ArtKind a_kind, const std::string& a_name)
		{
			const std::size_t k = static_cast<std::size_t>(a_kind);
			const char* folder = kArtFolders[k];
			auto file = [&](const char* a_suffix, bool a_optional) {
				const std::string path = Resolve(std::format("{}/{}/{}{}.png", kAssetDir, folder, a_name, a_suffix));
				std::error_code ec;
				if (a_optional && !std::filesystem::exists(path, ec)) { return ArtImage{}; }   // an absent layer is not a miss
				return CachedTexture(path);
			};
			ArtPart part;
			part.main = file("", false);
			if (!part.main.srv)
			{
				logger::warn("skin: {} part \"{}\" is not in assets/{} (the built-in look draws)", kArtKeys[k] + 1, a_name, folder);
				return false;
			}
			part.edge = file("-edge", true);
			if (a_kind == ArtKind::kTickBox) { part.extra = file("-mark", true); }
			const FrameCut cut = ReadCut(folder, a_name);
			part.corner = static_cast<float>(cut.corner);
			if (part.corner <= 0.0f) { part.corner = std::floor(std::min(part.main.w, part.main.h) * 0.25f); }
			part.corner = std::min(part.corner, std::min(part.main.w, part.main.h) * 0.5f - 1.0f);
			part.drawCorner = static_cast<float>(cut.drawCorner);
			part.tile = cut.tile;
			part.hotX = cut.hotX;
			part.hotY = cut.hotY;
			part.cornersOnly = cut.highlightCorners;
			part.ownColours = cut.ownColours;
			part.threeSlice = cut.threeSlice;
			g_parts[k] = part;
			logger::info("skin: {} part \"{}\" ({:.0f}x{:.0f}, {} mip levels{}{})", kArtKeys[k] + 1, a_name, part.main.w, part.main.h,
						 part.main.mips, part.edge.srv ? ", edge" : "", part.extra.srv ? ", extra" : "");
			return true;
		}

		// The player's pick for a kind on the active theme ("" = the theme's own).
		std::string PlayerPick(const std::string& a_themeId, ArtKind a_kind)
		{
			const auto& v = settings::Get();
			const auto picks = v.themeArt.find(a_themeId);
			return picks != v.themeArt.end() ? picks->second[static_cast<std::size_t>(a_kind)] : std::string();
		}
	}

	const ArtPart* Part(ArtKind a_kind)
	{
		const std::size_t k = static_cast<std::size_t>(a_kind);
		return k < kArtKindCount && g_partLoaded[k] ? &g_parts[k] : nullptr;
	}

	std::vector<std::string> ListArt(ArtKind a_kind)
	{
		std::vector<std::string> out;
		const auto dir = AssetFolder(kArtFolders[static_cast<std::size_t>(a_kind)]);
		std::error_code ec;
		for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".png")
			{
				// a part's extra layers are not parts of their own
				const std::string stem = it->path().stem().string();
				const std::string low = Lower(stem);
				auto ends = [&low](const char* a_suf) {
					const std::size_t n = std::char_traits<char>::length(a_suf);
					return low.size() > n && low.compare(low.size() - n, n, a_suf) == 0;
				};
				// ...only when the part it belongs to is there: "oblivion-map-edge" is a frame of its own, "norden-edge"
				// beside "norden" is norden's edge layer
				bool layer = false;
				for (const char* suffix : { "-edge", "-mark", "-track" })
				{
					if (ends(suffix))
					{
						const std::string base = stem.substr(0, stem.size() - std::char_traits<char>::length(suffix));
						layer = std::filesystem::exists(dir / (base + ".png"), ec);
					}
				}
				if (layer) { continue; }
				out.push_back(stem);
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	std::string ThemeArt(ArtKind a_kind)
	{
		if (theme::ListThemes().empty()) { return {}; }
		return PaletteArt(theme::GetActiveTheme(), a_kind);
	}

	std::string ActiveArt(ArtKind a_kind)
	{
		std::scoped_lock lock(g_stateLock);
		return g_activeArt[static_cast<std::size_t>(a_kind)];
	}

	void* ArtThumb(ArtKind a_kind, const std::string& a_name, ImVec2* a_size)
	{
		const std::string path = Resolve(ArtPath(a_kind, a_name));
		std::error_code ec;
		ArtImage img;
		if (std::filesystem::exists(path, ec)) { img = CachedTexture(path); }   // a missing thumbnail layer is not a miss
		if (a_size) { *a_size = ImVec2(img.w, img.h); }
		return img.srv;
	}

	void Reload()
	{
		std::scoped_lock lock(g_stateLock);
		const auto& v = settings::Get();

		// WHERE THE ART COMES FROM. The master switch is honoured HERE rather than at each draw site,
		// so exactly one place decides whether custom art exists at all. On, the player's own [Skin]
		// art draws. Off, the ACTIVE THEME's art draws if it has any (1.9.8: a theme picked in the
		// Theme list is itself the request for that theme's look), and otherwise nothing does - which
		// is what a player who installed no art replacer must get, including when paths are still
		// sitting in the INI from somebody's experiment.
		std::string   frame, background, plates;
		std::uint32_t corner = 64;
		const char*   source = "none";
		std::string   toggle, frameName;   // the switch part's file, and the library frame's name (its .ini)
		std::string   pathFrame, pathBackground;   // Witcher 3: the theme's own path art, the fallback when a library part is missing
		bool          toggleNone = false;
		g_frameNone = false;
		g_frameDrawCorner = 0.0f;
		g_frameBand = 0.0f;
		g_frameTiles = false;
		g_frameHighlightCorners = false;
		g_activeArt = {};
		g_partLoaded = {};

		// THE CONTROL KINDS (Box .. Cursor) - the player's pick for this theme, else the theme's own part, else the
		// built-in look. A UI author's [Skin] art covers only the frame, background and plates, so these load either way.
		if (!theme::ListThemes().empty())
		{
			const theme::Palette& t = theme::GetActiveTheme();
			for (std::size_t k = static_cast<std::size_t>(ArtKind::kBox); k < kArtKindCount; ++k)
			{
				std::string name = PaletteArt(t, static_cast<ArtKind>(k));
				const std::string pick = PlayerPick(t.id, static_cast<ArtKind>(k));
				if (!pick.empty()) { name = pick; }
				if (name.empty() || Lower(name) == kArtNone) { continue; }
				if (LoadPart(static_cast<ArtKind>(k), name))
				{
					g_partLoaded[k] = true;
					g_activeArt[k] = name;
				}
			}
		}
		// The switch on with NO [Skin] paths set used to leave every art theme bare (Skyrim 2.1.5). The switch only means
		// something when a UI author's art is actually named; otherwise the theme's own.
		if (v.skinEnabled && !(v.skinFrame.empty() && v.skinBackground.empty() && v.skinPlates.empty()))
		{
			frame = v.skinFrame;
			background = v.skinBackground;
			plates = v.skinPlates;
			corner = v.skinFrameCorner;
			source = "[Skin]";
		}
		else if (!theme::ListThemes().empty())   // the registry fills at theme::Apply; D3D init can run first
		{
			const theme::Palette& t = theme::GetActiveTheme();
			frame = pathFrame = t.skinFrame;
			background = pathBackground = t.skinBackground;
			plates = t.skinPlates;
			corner = t.skinFrameCorner;
			source = "theme";

			// THE ART PARTS, BY NAME (Skyrim 2.1.6) - the player's pick for this theme (Appearance > Art) wins, then the
			// theme's own part; either wins over the old path keys above. "none" = no art of that kind, not even the theme's.
			for (std::size_t k = 0; k <= static_cast<std::size_t>(ArtKind::kToggle); ++k)
			{
				const auto kind = static_cast<ArtKind>(k);
				std::string name = PaletteArt(t, kind);
				const std::string pick = PlayerPick(t.id, kind);
				if (!pick.empty()) { name = pick; }
				if (name.empty()) { continue; }
				const bool none = Lower(name) == kArtNone;
				const std::string path = none ? std::string() : ArtPath(kind, name);
				switch (kind)
				{
				case ArtKind::kFrame:      frame = path; g_frameNone = none; frameName = none ? "" : name; if (none) { pathFrame.clear(); } break;
				case ArtKind::kBackground: background = path; if (none) { pathBackground.clear(); } break;
				default:                   toggle = path; toggleNone = none; break;
				}
				if (!none) { g_activeArt[k] = name; }
			}
		}

		g_frame = Entry{};
		g_background = Entry{};
		for (std::size_t i = 0; i < g_plates.size(); ++i) { g_plates[i] = Entry{}; }
		g_toggleEdge = Entry{};
		if (frame.empty() && background.empty() && plates.empty() && toggle.empty())
		{
			return;
		}
		logger::info("skin: loading art from {}", source);

		Load(g_frame, Resolve(frame), "frame");
		Load(g_background, Resolve(background), "background");
		// Witcher 3: a library part that did not load (assets folder missing, a file deleted) falls back to the theme's own
		// path art when it has one, then to the built-in look - never to a bare window.
		if (!g_frame.srv && !frameName.empty() && !pathFrame.empty() && Resolve(pathFrame) != Resolve(frame))
		{
			logger::warn("skin: frame part \"{}\" did not load; the theme's own frame \"{}\" draws instead", frameName, pathFrame);
			Load(g_frame, Resolve(pathFrame), "frame (theme path)");
			frameName.clear();
		}
		if (!g_background.srv && !pathBackground.empty() && Resolve(pathBackground) != Resolve(background))
		{
			Load(g_background, Resolve(pathBackground), "background (theme path)");
		}
		if (!g_frame.srv || frameName.empty()) { g_activeArt[static_cast<std::size_t>(ArtKind::kFrame)].clear(); }
		if (!g_background.srv) { g_activeArt[static_cast<std::size_t>(ArtKind::kBackground)].clear(); }

		// A library frame says how it is cut in the .ini beside it (Skyrim 2.1.6).
		if (g_frame.srv && !frameName.empty())
		{
			const FrameCut cut = ReadCut("frames", frameName);
			if (cut.corner > 0) { corner = cut.corner; }
			g_frameDrawCorner = static_cast<float>(cut.drawCorner);
			g_frameBand = static_cast<float>(cut.band);
			g_frameTiles = cut.tile;
			g_frameHighlightCorners = cut.highlightCorners;
		}

		// Plates live by fixed name inside one folder, so the author has one path to get right
		// and can supply any subset of the three.
		if (!plates.empty())
		{
			const std::filesystem::path dir(Resolve(plates));
			for (std::size_t i = 0; i < g_plates.size(); ++i)
			{
				const auto file = (dir / kPlateFile[i]).string();
				std::error_code ec;
				if (!std::filesystem::exists(file, ec)) { continue; }   // a missing plate is not an error
				Load(g_plates[i], file, kPlateName[i]);
			}
		}
		// The switch part by name replaces a plates-folder toggle.png; "none" takes the switch plate away.
		auto& togglePlate = g_plates[static_cast<std::size_t>(Plate::kToggle)];
		if (!toggle.empty())
		{
			Entry fromLibrary;
			Load(fromLibrary, Resolve(toggle), "toggle");
			if (fromLibrary.srv) { togglePlate = fromLibrary; }   // a missing part keeps the plates folder's toggle.png
			std::string edge = toggle;
			if (edge.size() > 4 && edge.compare(edge.size() - 4, 4, ".png") == 0) { edge.insert(edge.size() - 4, "-edge"); }
			std::error_code ec;
			if (fromLibrary.srv && edge != toggle && std::filesystem::exists(Resolve(edge), ec)) { Load(g_toggleEdge, Resolve(edge), "toggle outline"); }
		}
		else if (toggleNone) { togglePlate = Entry{}; }
		if (!togglePlate.srv || toggle.empty()) { g_activeArt[static_cast<std::size_t>(ArtKind::kToggle)].clear(); }

		// Clamp the corner so two of them always fit inside the frame texture. An artist who
		// types 64 for a 96px image would otherwise get flipped middle slices, which looks like
		// corrupt art rather than a bad number.
		g_frameCorner = static_cast<float>(corner);
		if (g_frame.srv && g_frame.size.x > 0.0f && g_frame.size.y > 0.0f)
		{
			const float maxCorner = std::min(g_frame.size.x, g_frame.size.y) * 0.5f - 1.0f;
			if (g_frameCorner > maxCorner)
			{
				logger::warn("skin: uFrameCorner {:.0f} is too large for a {:.0f}x{:.0f} frame; clamped to {:.0f}",
							 g_frameCorner, g_frame.size.x, g_frame.size.y, maxCorner);
				g_frameCorner = maxCorner;
			}
			if (g_frameCorner < 1.0f) { g_frameCorner = 1.0f; }
		}
	}

	bool   HasFrame() { return g_frame.srv != nullptr; }
	void*  FrameTexture() { return g_frame.srv; }
	ImVec2 FrameSize() { return g_frame.size; }
	float  FrameCorner() { return g_frameCorner; }
	float  FrameDrawCorner() { return g_frameDrawCorner; }
	float  FrameBand() { return g_frameBand; }
	bool   FrameTiles() { return g_frame.srv && g_frameTiles; }
	bool   FrameHighlightCorners() { return g_frame.srv && g_frameHighlightCorners; }

	bool DrawsFrame()
	{
		if (g_frame.srv) { return true; }
		if (g_frameNone || theme::ListThemes().empty()) { return false; }
		return theme::GetActiveTheme().knotwork;
	}

	bool   HasBackground() { return g_background.srv != nullptr; }
	void*  BackgroundTexture() { return g_background.srv; }
	ImVec2 BackgroundSize() { return g_background.size; }

	bool BackgroundTiles()
	{
		return g_background.srv && g_background.size.x <= static_cast<float>(kTileThreshold) &&
			   g_background.size.y <= static_cast<float>(kTileThreshold);
	}

	bool HasPlate(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() && g_plates[i].srv != nullptr;
	}

	void* ToggleEdgeTexture() { return g_toggleEdge.srv; }

	void* PlateTexture(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() ? g_plates[i].srv : nullptr;
	}

	ImVec2 PlateSize(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() ? g_plates[i].size : ImVec2(0.0f, 0.0f);
	}

	namespace
	{
		// 1.9.8: a resolved path is "Data\SKSE/..." - the backslash went out unescaped and the reply was not valid
		// JSON. Escape \ and " in every string.
		std::string Esc(const std::string& a_s)
		{
			std::string out;
			out.reserve(a_s.size());
			for (const char c : a_s)
			{
				if (c == '\\' || c == '"') { out += '\\'; }
				out += c;
			}
			return out;
		}
	}

	std::string StatusJson()
	{
		std::scoped_lock lock(g_stateLock);
		auto one = [](const char* a_name, const Entry& a_e) {
			return std::format(R"({{"what":"{}","path":"{}","loaded":{},"w":{:.0f},"h":{:.0f},"mips":{},"error":"{}"}})",
							   a_name, Esc(a_e.path), a_e.srv ? "true" : "false", a_e.size.x, a_e.size.y, a_e.mips, Esc(a_e.error));
		};
		std::string plates;
		for (std::size_t i = 0; i < g_plates.size(); ++i)
		{
			if (!plates.empty()) { plates += ","; }
			plates += one(kPlateName[i], g_plates[i]);
		}
		// the library parts drawing now, every kind by its [Art] key ("" = the built-in look), and the frame's cut
		std::string art;
		for (std::size_t k = 0; k < kArtKindCount; ++k)
		{
			art += std::format(R"({}"{}":"{}")", k ? "," : "", kArtKeys[k] + 1, Esc(g_activeArt[k]));
		}
		return std::format(R"({{"frame":{},"frameCorner":{:.0f},"frameDrawCorner":{:.0f},"frameBand":{:.0f},"frameTiles":{},"frameHighlight":"{}",)"
						   R"("frameNone":{},"art":{{{}}},"background":{},"backgroundTiles":{},"plates":[{}]}})",
						   one("frame", g_frame), g_frameCorner, g_frameDrawCorner, g_frameBand, FrameTiles() ? "true" : "false",
						   FrameHighlightCorners() ? "corners" : "whole", g_frameNone ? "true" : "false", art,
						   one("background", g_background), BackgroundTiles() ? "true" : "false", plates);
	}

	std::string ArtKindsJson()
	{
		std::scoped_lock lock(g_stateLock);
		const bool themes = !theme::ListThemes().empty();
		const std::string themeId = themes ? theme::GetActiveTheme().id : std::string();
		std::string out;
		for (std::size_t k = 0; k < kArtKindCount; ++k)
		{
			const auto kind = static_cast<ArtKind>(k);
			// the texture behind the kind: Frame / Background / Switch keep their own entries, Popup and Highlight are frames
			float w = 0.0f, h = 0.0f;
			int mips = 0;
			bool loaded = false;
			switch (kind)
			{
			case ArtKind::kFrame:      loaded = g_frame.srv != nullptr; w = g_frame.size.x; h = g_frame.size.y; mips = g_frame.mips; break;
			case ArtKind::kBackground: loaded = g_background.srv != nullptr; w = g_background.size.x; h = g_background.size.y; mips = g_background.mips; break;
			case ArtKind::kToggle:
				{
					const Entry& e = g_plates[static_cast<std::size_t>(Plate::kToggle)];
					loaded = e.srv != nullptr; w = e.size.x; h = e.size.y; mips = e.mips;
					break;
				}
			default:
				if (const ArtPart* p = Part(kind)) { loaded = true; w = p->main.w; h = p->main.h; mips = p->main.mips; }
				break;
			}
			out += std::format(R"({}"{}":{{"set":"{}","themeOwn":"{}","pick":"{}","loaded":{},"w":{:.0f},"h":{:.0f},"mips":{}}})",
							   k ? "," : "", kArtKeys[k] + 1, Esc(g_activeArt[k]), Esc(themes ? PaletteArt(theme::GetActiveTheme(), kind) : std::string()),
							   Esc(themes ? PlayerPick(themeId, kind) : std::string()), loaded ? "true" : "false", w, h, mips);
		}
		return "{" + out + "}";
	}
}
