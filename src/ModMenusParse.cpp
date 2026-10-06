#include "ModMenusParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace modmenus
{
	namespace
	{
		std::string Lower(std::string_view a_s)
		{
			std::string out(a_s);
			for (char& c : out) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return out;
		}

		std::string_view Trim(std::string_view a_s)
		{
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.front()))) {
				a_s.remove_prefix(1);
			}
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.back()))) {
				a_s.remove_suffix(1);
			}
			return a_s;
		}

		void AppendUtf8(std::string& a_out, std::uint32_t a_cp)
		{
			if (a_cp < 0x80) {
				a_out += static_cast<char>(a_cp);
			} else if (a_cp < 0x800) {
				a_out += static_cast<char>(0xC0 | (a_cp >> 6));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else if (a_cp < 0x10000) {
				a_out += static_cast<char>(0xE0 | (a_cp >> 12));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else {
				a_out += static_cast<char>(0xF0 | (a_cp >> 18));
				a_out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			}
		}

		std::string FromUtf16(std::string_view a_bytes, bool a_bigEndian)
		{
			std::string out;
			out.reserve(a_bytes.size() / 2);
			const auto* p = reinterpret_cast<const unsigned char*>(a_bytes.data());
			const std::size_t n = a_bytes.size() / 2;
			auto unit = [&](std::size_t i) -> std::uint32_t {
				return a_bigEndian ? (p[2 * i] << 8 | p[2 * i + 1]) : (p[2 * i + 1] << 8 | p[2 * i]);
			};
			for (std::size_t i = 0; i < n; ++i) {
				std::uint32_t cp = unit(i);
				if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n) {
					const std::uint32_t lo = unit(i + 1);
					if (lo >= 0xDC00 && lo < 0xE000) {
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						++i;
					}
				}
				AppendUtf8(out, cp);
			}
			return out;
		}

		struct Tag
		{
			std::string                                  name;   // lower case
			bool                                         closing = false;
			bool                                         selfClosing = false;
			std::unordered_map<std::string, std::string> attrs;   // lower-case names
			std::string Attr(const char* a_name) const
			{
				const auto it = attrs.find(a_name);
				return it != attrs.end() ? it->second : std::string{};
			}
		};

		// The next element tag from a_pos, skipping comments, <?...?> and <!...>. False at the end of the text.
		bool NextTag(std::string_view a_s, std::size_t& a_pos, Tag& a_tag)
		{
			while (true) {
				const std::size_t lt = a_s.find('<', a_pos);
				if (lt == std::string_view::npos) {
					return false;
				}
				if (a_s.compare(lt, 4, "<!--") == 0) {
					const std::size_t end = a_s.find("-->", lt + 4);
					a_pos = end == std::string_view::npos ? a_s.size() : end + 3;
					continue;
				}
				if (lt + 1 < a_s.size() && (a_s[lt + 1] == '?' || a_s[lt + 1] == '!')) {
					const std::size_t end = a_s.find('>', lt + 2);
					a_pos = end == std::string_view::npos ? a_s.size() : end + 1;
					continue;
				}
				// the tag runs to the first '>' outside quotes
				std::size_t i = lt + 1;
				char        quote = 0;
				for (; i < a_s.size(); ++i) {
					const char c = a_s[i];
					if (quote) {
						if (c == quote) {
							quote = 0;
						}
					} else if (c == '"' || c == '\'') {
						quote = c;
					} else if (c == '>') {
						break;
					}
				}
				if (i >= a_s.size()) {
					return false;
				}
				std::string_view body = a_s.substr(lt + 1, i - lt - 1);
				a_pos = i + 1;

				a_tag = Tag{};
				body = Trim(body);
				if (!body.empty() && body.front() == '/') {
					a_tag.closing = true;
					body.remove_prefix(1);
				}
				if (!body.empty() && body.back() == '/') {
					a_tag.selfClosing = true;
					body.remove_suffix(1);
				}
				std::size_t k = 0;
				while (k < body.size() && !std::isspace(static_cast<unsigned char>(body[k]))) {
					++k;
				}
				a_tag.name = Lower(body.substr(0, k));
				// attributes: name = "value" | 'value' | bare
				while (k < body.size()) {
					while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) {
						++k;
					}
					const std::size_t nameStart = k;
					while (k < body.size() && body[k] != '=' && !std::isspace(static_cast<unsigned char>(body[k]))) {
						++k;
					}
					const std::string name = Lower(body.substr(nameStart, k - nameStart));
					while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) {
						++k;
					}
					if (k >= body.size() || body[k] != '=') {
						if (!name.empty()) {
							a_tag.attrs[name] = "";
						}
						continue;
					}
					++k;
					while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) {
						++k;
					}
					std::string value;
					if (k < body.size() && (body[k] == '"' || body[k] == '\'')) {
						const char        q = body[k++];
						const std::size_t end = body.find(q, k);
						value = std::string(body.substr(k, end == std::string_view::npos ? std::string_view::npos : end - k));
						k = end == std::string_view::npos ? body.size() : end + 1;
					} else {
						const std::size_t start = k;
						while (k < body.size() && !std::isspace(static_cast<unsigned char>(body[k]))) {
							++k;
						}
						value = std::string(body.substr(start, k - start));
					}
					if (!name.empty()) {
						a_tag.attrs[name] = value;
					}
				}
				if (!a_tag.name.empty()) {
					return true;
				}
			}
		}

		std::vector<std::string> Split(std::string_view a_s, char a_sep)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			while (true) {
				const std::size_t at = a_s.find(a_sep, start);
				out.emplace_back(a_s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
				if (at == std::string_view::npos) {
					break;
				}
				start = at + 1;
			}
			return out;
		}

		double ToDouble(const std::string& a_s, double a_fallback)
		{
			char*        end = nullptr;
			const double v = std::strtod(a_s.c_str(), &end);
			return end != a_s.c_str() ? v : a_fallback;
		}
	}

	std::string DecodeText(std::string_view a_bytes)
	{
		const auto* p = reinterpret_cast<const unsigned char*>(a_bytes.data());
		const std::size_t n = a_bytes.size();
		if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
			return std::string(a_bytes.substr(3));
		}
		if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
			return FromUtf16(a_bytes.substr(2), false);
		}
		if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
			return FromUtf16(a_bytes.substr(2), true);
		}
		// no BOM: '<' as the first UTF-16 unit gives the byte order away
		if (n >= 2 && p[0] != 0 && p[1] == 0) {
			return FromUtf16(a_bytes, false);
		}
		if (n >= 2 && p[0] == 0 && p[1] != 0) {
			return FromUtf16(a_bytes, true);
		}
		return std::string(a_bytes);
	}

	std::vector<Group> ParseMenuXml(std::string_view a_utf8, const std::string& a_file)
	{
		std::vector<Group> groups;
		Group*             group = nullptr;
		bool               inVar = false;     // a <Var> opened without /> and not yet closed
		bool               inOption = false;
		std::vector<bool>  hidden;            // per var of the current group
		bool               groupHidden = false;   // the whole group is visibilityCondition="hideAlways", as the game hides it
		auto closeGroup = [&] {
			if (group && groupHidden) {
				groups.pop_back();   // the game's own menu never shows it, so neither does AMF
			} else if (group) {
				std::vector<Var> kept;
				for (std::size_t i = 0; i < group->vars.size(); ++i) {
					if (!hidden[i]) {
						kept.push_back(std::move(group->vars[i]));
					}
				}
				group->vars = std::move(kept);
			}
			group = nullptr;
			groupHidden = false;
			hidden.clear();
			inVar = inOption = false;
		};

		std::size_t pos = 0;
		Tag         tag;
		while (NextTag(a_utf8, pos, tag)) {
			if (tag.name == "group") {
				if (tag.closing) {
					closeGroup();
					continue;
				}
				closeGroup();
				Group g;
				g.id = tag.Attr("id");
				g.displayName = tag.Attr("displayname");
				g.path = Split(g.displayName, '.');
				g.file = a_file;
				groups.push_back(std::move(g));
				group = &groups.back();
				groupHidden = Lower(tag.Attr("visibilitycondition")) == "hidealways";
				if (tag.selfClosing) {
					closeGroup();
				}
			} else if (tag.name == "var" && group) {
				if (tag.closing) {
					inVar = inOption = false;
					continue;
				}
				Var v;
				v.id = tag.Attr("id");
				v.label = tag.Attr("displayname");
				// mods write SLIDER;min;max;steps, the game's own files SLIDER:min:max:steps (hidden.xml) - accept both
				std::string type = tag.Attr("displaytype");
				std::replace(type.begin(), type.end(), ':', ';');
				const auto parts = Split(type, ';');
				v.type = parts.empty() ? std::string{} : std::string(Trim(parts[0]));
				for (char& c : v.type) {
					c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				}
				if (v.type == "SLIDER") {
					v.min = parts.size() > 1 ? ToDouble(parts[1], 0.0) : 0.0;
					v.max = parts.size() > 2 ? ToDouble(parts[2], 1.0) : 1.0;
					v.steps = parts.size() > 3 ? static_cast<int>(ToDouble(parts[3], 0.0)) : 0;
				}
				group->vars.push_back(std::move(v));
				hidden.push_back(Lower(tag.Attr("visibilitycondition")) == "hidealways");
				inVar = !tag.selfClosing;
				inOption = false;
			} else if (tag.name == "option" && group && inVar && !group->vars.empty()) {
				if (tag.closing) {
					inOption = false;
					continue;
				}
				Option o;
				o.label = tag.Attr("displayname");
				o.value = tag.Attr("id");   // replaced by the <Entry> for this var when there is one
				group->vars.back().options.push_back(std::move(o));
				inOption = !tag.selfClosing;
			} else if (tag.name == "entry" && group && inVar && inOption && !group->vars.empty()) {
				Var& v = group->vars.back();
				if (!v.options.empty() && _stricmp(tag.Attr("varid").c_str(), v.id.c_str()) == 0) {
					v.options.back().value = tag.Attr("value");
				}
			}
		}
		closeGroup();
		return groups;
	}

	bool IsModGroup(const Group& a_group)
	{
		return !a_group.path.empty() && _stricmp(a_group.path[0].c_str(), "Mods") == 0;
	}

	void Place(std::vector<Group>& a_groups)
	{
		if (a_groups.empty()) {
			return;
		}
		// the common prefix of every group's path without its last (page) segment, compared case-insensitively
		std::vector<std::string> common(a_groups[0].path.begin(), a_groups[0].path.end() - (a_groups[0].path.empty() ? 0 : 1));
		for (const Group& g : a_groups) {
			const std::size_t n = g.path.empty() ? 0 : g.path.size() - 1;
			std::size_t       k = 0;
			while (k < common.size() && k < n && _stricmp(common[k].c_str(), g.path[k].c_str()) == 0) {
				++k;
			}
			common.resize(k);
		}
		for (Group& g : a_groups) {
			g.modIndex = common.size() >= 2 ? common.size() - 1 : 1;
		}
	}

	std::uint32_t KeyHash(std::string_view a_key)
	{
		// UTF-8 -> UTF-16 units, lower-cased (keys are ASCII in practice; anything else is hashed as written)
		std::uint32_t h = 0;
		const auto*   p = reinterpret_cast<const unsigned char*>(a_key.data());
		const std::size_t n = a_key.size();
		auto unit = [&h](std::uint32_t c) { h = h * 31u + c; };
		for (std::size_t i = 0; i < n;) {
			std::uint32_t cp = p[i];
			std::size_t   len = 1;
			if (cp >= 0xF0 && i + 3 < n) {
				cp = ((cp & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F);
				len = 4;
			} else if (cp >= 0xE0 && i + 2 < n) {
				cp = ((cp & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
				len = 3;
			} else if (cp >= 0xC0 && i + 1 < n) {
				cp = ((cp & 0x1F) << 6) | (p[i + 1] & 0x3F);
				len = 2;
			}
			i += len;
			if (cp >= 'A' && cp <= 'Z') {
				cp += 'a' - 'A';
			}
			if (cp >= 0x10000) {
				cp -= 0x10000;
				unit(0xD800 + (cp >> 10));
				unit(0xDC00 + (cp & 0x3FF));
			} else {
				unit(cp);
			}
		}
		return h;
	}

	namespace
	{
		// The language key (key1 << 16 | key2) -> the magic that XORs ids and seeds the text key. 0 = not obfuscated
		// (the languages added after release). Mods' files all carry the English key - it is what the encoder writes.
		constexpr std::pair<std::uint32_t, std::uint32_t> kLanguageMagic[] = {
			{ 0x00000000, 0x00000000 }, { 0x24987354, 0x21793217 }, { 0x75886138, 0x42791159 }, { 0x43975139, 0x79321793 },
			{ 0x18796651, 0x42387566 }, { 0x23863176, 0x75921975 }, { 0x42378932, 0x67823218 }, { 0x45931894, 0x12375973 },
			{ 0x54834893, 0x59825646 }, { 0x83496237, 0x73946816 }, { 0x63481486, 0x42386347 }, { 0x18632176, 0x16875467 },
		};

		// The variable-length count: 6 data bits in the first byte (0x40 = more follows), then 7 per byte (0x80 = more).
		bool Bit6(std::string_view a_d, std::size_t& a_p, std::uint32_t& a_out)
		{
			if (a_p >= a_d.size()) {
				return false;
			}
			std::uint8_t b = static_cast<std::uint8_t>(a_d[a_p++]);
			a_out = b & 0x3F;
			if (b & 0x40) {
				int shift = 6;
				do {
					if (a_p >= a_d.size() || shift > 27) {
						return false;
					}
					b = static_cast<std::uint8_t>(a_d[a_p++]);
					a_out |= static_cast<std::uint32_t>(b & 0x7F) << shift;
					shift += 7;
				} while (b & 0x80);
			}
			return true;
		}

		std::uint32_t U32(std::string_view a_d, std::size_t a_p)
		{
			const auto* p = reinterpret_cast<const unsigned char*>(a_d.data() + a_p);
			return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
		}

		// UTF-8 as written, except that a byte that is not valid UTF-8 is read as Latin-1 (the game's own Arabic file has
		// bare 0xA0 bytes), so a stray byte never breaks the text.
		std::string LenientUtf8(std::string_view a_s)
		{
			std::string out;
			out.reserve(a_s.size());
			const auto* p = reinterpret_cast<const unsigned char*>(a_s.data());
			for (std::size_t i = 0; i < a_s.size();) {
				const unsigned char c = p[i];
				std::size_t         len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
				bool                ok = len > 0 && i + len <= a_s.size();
				for (std::size_t k = 1; ok && k < len; ++k) {
					ok = (p[i + k] & 0xC0) == 0x80;
				}
				if (ok) {
					out.append(a_s.substr(i, len));
					i += len;
				} else {
					AppendUtf8(out, c);
					++i;
				}
			}
			return out;
		}
	}

	int ReadW3Strings(std::string_view a_d, const std::unordered_map<std::uint32_t, bool>& a_wantedHashes, StringTable& a_out)
	{
		if (a_d.size() < 15 || a_d.substr(0, 4) != "RTSW") {
			return -1;
		}
		const std::uint32_t version = U32(a_d, 4);
		const std::uint32_t key1 = static_cast<std::uint8_t>(a_d[8]) | (static_cast<std::uint8_t>(a_d[9]) << 8);
		const std::size_t   key2At = a_d.size() - 2;
		const std::uint32_t key2 = static_cast<std::uint8_t>(a_d[key2At]) | (static_cast<std::uint8_t>(a_d[key2At + 1]) << 8);
		const std::uint32_t langKey = (key1 << 16) | key2;
		std::uint32_t       magic = 0;
		bool                known = false;
		for (const auto& [key, m] : kLanguageMagic) {
			if (key == langKey) {
				magic = m;
				known = true;
			}
		}
		if (!known) {
			return -1;
		}

		std::size_t   p = 10;
		std::uint32_t entries = 0, keys = 0, units = 0;
		if (!Bit6(a_d, p, entries) || p + std::size_t{ 12 } * entries > key2At) {
			return -1;
		}
		const std::size_t entriesAt = p;
		p += std::size_t{ 12 } * entries;
		if (!Bit6(a_d, p, keys) || p + std::size_t{ 8 } * keys > key2At) {
			return -1;
		}
		const std::size_t keysAt = p;
		p += std::size_t{ 8 } * keys;
		if (!Bit6(a_d, p, units)) {
			return -1;
		}
		const std::size_t start = p;
		std::size_t       unit = version >= 164 ? 1 : 2;
		// some community files say 162 but hold UTF-8: the buffer only ends at key2 when counted in bytes
		if (unit == 2 && units && start + 2 * std::size_t{ units } != key2At && start + units == key2At) {
			unit = 1;
		}

		// the key table: the hashes we want -> their ids
		std::unordered_map<std::uint32_t, std::uint32_t> idToHash;
		for (std::uint32_t i = 0; i < keys; ++i) {
			const std::uint32_t hash = U32(a_d, keysAt + 8 * i);
			if (a_wantedHashes.contains(hash) && !a_out.byHash.contains(hash)) {
				idToHash.emplace(U32(a_d, keysAt + 8 * i + 4) ^ magic, hash);
			}
		}
		int added = 0;
		for (std::uint32_t i = 0; i < entries && !idToHash.empty(); ++i) {
			const std::size_t   e = entriesAt + 12 * i;
			const auto          it = idToHash.find(U32(a_d, e) ^ magic);
			if (it == idToHash.end()) {
				continue;
			}
			const std::uint32_t offset = U32(a_d, e + 4), length = U32(a_d, e + 8);
			const std::size_t   at = start + std::size_t{ offset } * unit;
			if (at + std::size_t{ length } * unit > key2At) {
				continue;
			}
			std::string   raw(a_d.substr(at, std::size_t{ length } * unit));
			std::uint32_t key = (magic >> 8) & 0xFFFF;
			for (std::uint32_t k = 0; k < length; ++k) {
				const std::uint32_t ck = (length + 1) * key;
				if (unit == 2) {
					raw[2 * k] = static_cast<char>(raw[2 * k] ^ (ck & 0xFF));
					raw[2 * k + 1] = static_cast<char>(raw[2 * k + 1] ^ ((ck >> 8) & 0xFF));
				} else {
					raw[k] = static_cast<char>(raw[k] ^ (ck & 0xFF));
				}
				key = ((key << 1) | (key >> 15)) & 0xFFFF;
			}
			a_out.byHash.emplace(it->second, unit == 2 ? FromUtf16(raw, false) : LenientUtf8(raw));
			idToHash.erase(it);
			++added;
		}
		return added;
	}

	std::vector<std::string> NeededKeys(const Group& a_group)
	{
		std::vector<std::string> keys;
		for (std::size_t i = 1; i < a_group.path.size(); ++i) {
			keys.push_back("panel_" + a_group.path[i]);
		}
		for (const Var& v : a_group.vars) {
			if (!v.label.empty()) {
				keys.push_back("option_" + v.label);
			}
			for (const Option& o : v.options) {
				if (!o.label.empty()) {
					keys.push_back(o.label);
					keys.push_back("preset_value_" + o.label);
					keys.push_back("option_" + o.label);
				}
			}
		}
		return keys;
	}

	std::size_t ReadStringsCsv(std::string_view a_utf8, const std::unordered_map<std::string, bool>& a_wanted, StringTable& a_out)
	{
		std::size_t added = 0;
		std::size_t start = 0;
		while (start < a_utf8.size()) {
			std::size_t end = a_utf8.find('\n', start);
			if (end == std::string_view::npos) {
				end = a_utf8.size();
			}
			std::string_view line = a_utf8.substr(start, end - start);
			start = end + 1;
			if (!line.empty() && line.back() == '\r') {
				line.remove_suffix(1);
			}
			if (line.empty() || line.front() == ';') {
				continue;
			}
			// id|hexkey|key|text - the text may itself hold '|'
			const std::size_t a = line.find('|');
			if (a == std::string_view::npos) {
				continue;
			}
			const std::size_t b = line.find('|', a + 1);
			const std::size_t c = b == std::string_view::npos ? b : line.find('|', b + 1);
			std::string       key;
			std::string_view  hex, text;
			if (c != std::string_view::npos) {
				hex = Trim(line.substr(a + 1, b - a - 1));
				key = Lower(Trim(line.substr(b + 1, c - b - 1)));
				text = line.substr(c + 1);
			} else if (b == std::string_view::npos) {
				// key|text (the encoder assigns ids from ";idspace=")
				key = Lower(Trim(line.substr(0, a)));
				text = line.substr(a + 1);
			} else {
				continue;
			}
			if (!key.empty()) {
				if (a_wanted.contains(key) && !a_out.byKey.contains(key)) {
					a_out.byKey.emplace(key, std::string(text));
					++added;
				}
			} else if (!hex.empty()) {
				char*                hexEnd = nullptr;
				const std::string    h(hex);
				const unsigned long  v = std::strtoul(h.c_str(), &hexEnd, 16);
				if (hexEnd != h.c_str() && v != 0 && !a_out.byHash.contains(static_cast<std::uint32_t>(v))) {
					a_out.byHash.emplace(static_cast<std::uint32_t>(v), std::string(text));
					++added;
				}
			}
		}
		return added;
	}

	std::string CsvLanguage(std::string_view a_utf8, const std::string& a_fileName)
	{
		static constexpr const char* kLanguages[] = { "en", "pl", "de", "fr", "it", "es", "esmx", "br", "ru", "cz", "hu", "jp", "kr",
			"cn", "zh", "tr", "ar" };
		std::string stem = Lower(a_fileName);
		if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
			stem.resize(dot);
		}
		const std::size_t sep = stem.find_last_of("_.-");
		const std::string last = sep == std::string::npos ? stem : stem.substr(sep + 1);
		for (const char* lang : kLanguages) {
			if (last == lang) {
				return last;
			}
		}
		const std::string head = Lower(a_utf8.substr(0, 400));
		if (const std::size_t at = head.find("language="); at != std::string::npos) {
			std::size_t e = at + 9;
			while (e < head.size() && std::isalpha(static_cast<unsigned char>(head[e]))) {
				++e;
			}
			return head.substr(at + 9, e - at - 9);
		}
		return {};
	}

	Settings ParseSettings(std::string_view a_utf8)
	{
		Settings    out;
		std::string section;
		std::size_t start = 0;
		while (start < a_utf8.size()) {
			std::size_t end = a_utf8.find('\n', start);
			if (end == std::string_view::npos) {
				end = a_utf8.size();
			}
			const std::string_view line = Trim(a_utf8.substr(start, end - start));
			start = end + 1;
			if (line.empty()) {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = std::string(line.substr(1, line.size() - 2));
				continue;
			}
			const std::size_t eq = line.find('=');
			if (eq != std::string_view::npos && !section.empty()) {
				out[section][std::string(Trim(line.substr(0, eq)))] = std::string(Trim(line.substr(eq + 1)));
			}
		}
		return out;
	}

	std::string StripMarkup(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size());
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const char c = a_text[i];
			if (c == '<') {
				const std::size_t end = a_text.find('>', i);
				if (end != std::string_view::npos) {
					out += ' ';   // <br> and friends separate words
					i = end;
					continue;
				}
			}
			if (c == '&') {
				static constexpr std::pair<const char*, char> kEntities[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
					{ "&quot;", '"' }, { "&apos;", '\'' }, { "&nbsp;", ' ' } };
				bool matched = false;
				for (const auto& [name, ch] : kEntities) {
					const std::size_t len = std::char_traits<char>::length(name);
					if (a_text.compare(i, len, name) == 0) {
						out += ch;
						i += len - 1;
						matched = true;
						break;
					}
				}
				if (matched) {
					continue;
				}
			}
			out += c;
		}
		// collapse runs of whitespace
		std::string tidy;
		bool        space = false;
		for (const char c : out) {
			if (std::isspace(static_cast<unsigned char>(c))) {
				space = !tidy.empty();
			} else {
				if (space) {
					tidy += ' ';
				}
				tidy += c;
				space = false;
			}
		}
		return tidy;
	}

	std::string Humanize(std::string_view a_id)
	{
		std::string out;
		char        prev = 0;
		for (const char c : a_id) {
			const unsigned char u = static_cast<unsigned char>(c);
			if (c == '_' || c == '.' || c == '-') {
				if (!out.empty() && out.back() != ' ') {
					out += ' ';
				}
				prev = ' ';
				continue;
			}
			if (std::isupper(u) && (std::islower(static_cast<unsigned char>(prev)) || std::isdigit(static_cast<unsigned char>(prev)))) {
				out += ' ';
			}
			out += c;
			prev = c;
		}
		while (!out.empty() && out.back() == ' ') {
			out.pop_back();
		}
		if (!out.empty()) {
			out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
		}
		return out;
	}

	int SliderDecimals(const Var& a_var)
	{
		if (a_var.steps <= 0) {
			return 2;
		}
		const double step = (a_var.max - a_var.min) / a_var.steps;
		for (int d = 0; d <= 4; ++d) {
			const double scaled = step * std::pow(10.0, d);
			const double minScaled = a_var.min * std::pow(10.0, d);
			if (std::fabs(scaled - std::round(scaled)) < 1e-6 && std::fabs(minScaled - std::round(minScaled)) < 1e-6) {
				return d;
			}
		}
		return 4;
	}
	namespace
	{
		std::string Find(const StringTable& a_strings, std::initializer_list<std::string> a_keys, bool* a_found)
		{
			for (const std::string& k : a_keys) {
				const std::string* raw = nullptr;
				if (const auto it = a_strings.byKey.find(Lower(k)); it != a_strings.byKey.end()) {
					raw = &it->second;
				} else if (const std::uint32_t h = KeyHash(k); h != 0) {
					if (const auto ht = a_strings.byHash.find(h); ht != a_strings.byHash.end()) {
						raw = &ht->second;
					}
				}
				if (raw) {
					std::string text = StripMarkup(*raw);
					if (!text.empty()) {
						if (a_found) {
							*a_found = true;
						}
						return text;
					}
				}
			}
			if (a_found) {
				*a_found = false;
			}
			return {};
		}
	}

	std::string PanelLabel(const std::string& a_segment, const StringTable& a_strings, bool* a_found)
	{
		std::string text = Find(a_strings, { "panel_" + a_segment }, a_found);
		return text.empty() ? Humanize(a_segment) : text;
	}

	std::string VarLabel(const Var& a_var, const StringTable& a_strings, bool* a_found)
	{
		const std::string& name = a_var.label.empty() ? a_var.id : a_var.label;
		std::string        text = Find(a_strings, { "option_" + name }, a_found);
		return text.empty() ? Humanize(name) : text;
	}

	std::string OptionLabel(const Option& a_option, const StringTable& a_strings, bool* a_found)
	{
		std::string text = Find(a_strings, { a_option.label, "preset_value_" + a_option.label, "option_" + a_option.label }, a_found);
		return text.empty() ? Humanize(a_option.label.empty() ? a_option.value : a_option.label) : text;
	}
}
