#pragma once

// AMF's OWN Font Awesome icons (W3 1.0.5, from Skyrim 2.1.5 - the owner, 2026-10-07: "I just want AMF to include Font Awesome for the generated
// menus"). Font Awesome Free 6.7.2 solid glyphs, merged into the TEXT face by Renderer.cpp BuildFonts (only these
// codepoints), so a label can carry an icon inline: std::string(icons::kInfo) + " " + text. UTF-8 of U+Fxxx. The file
// ships in bin/x64_dx12/AMF/icons/ under SIL OFL 1.1 (icons CC BY 4.0), the licence beside it.
// A missing file leaves these as "?" - the text still reads.

namespace icons
{
	inline constexpr const char* kInfo = "\xEF\x81\x9A";      // U+F05A circle-info: help text, page notes
	inline constexpr const char* kKeyboard = "\xEF\x84\x9C";  // U+F11C keyboard: a key binding button
	inline constexpr const char* kClear = "\xEF\x80\x8D";     // U+F00D xmark: clear / unmap a key
	inline constexpr const char* kReset = "\xEF\x8B\xAA";     // U+F2EA rotate-left: reset to default
	inline constexpr const char* kPen = "\xEF\x8C\x84";       // U+F304 pen: a text field
	inline constexpr const char* kPalette = "\xEF\x94\xBF";   // U+F53F palette: a colour
	inline constexpr const char* kLock = "\xEF\x80\xA3";      // U+F023 lock: an option the mod has disabled
	inline constexpr const char* kLoading = "\xEF\x89\x92";   // U+F252 hourglass-half: loading / waiting
	inline constexpr const char* kWarning = "\xEF\x81\xB1";   // U+F071 triangle-exclamation: not drawable here
	inline constexpr const char* kAction = "\xEF\x81\x94";    // U+F054 chevron-right: a row that runs something

	// The codepoint ranges BuildFonts merges live beside it in Renderer.cpp (as ImWchar) - keep them in step with this list.
}
