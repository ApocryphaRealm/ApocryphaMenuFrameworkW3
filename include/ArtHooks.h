#pragma once

// THE ART HOOK (Skyrim 2.1.6; Witcher 3 1.0.5). The in-tree Dear ImGui (extern/imgui, every call site marked [AMF]) carries
// Skyrim's amf-art-hooks.patch, which calls one function
// pointer just before it draws a box, button, tick box, slider grab, scroll bar, section line, tab, arrow or popup. This
// is that function: when the player (or the theme) has picked a part of that kind on Appearance > Art, it draws the
// part in place of ImGui's shape and ImGui skips its own; otherwise ImGui draws as always. Because it sits inside
// ImGui, it reaches every page alike - the framework's own, converted MCM pages, every mod's page and the on-screen
// keyboard - with no change to any of them.
namespace arthooks
{
	// Points ImGui's hook at ours. Call once, after the ImGui context exists.
	void Install();

	// The mouse pointer (the Cursor kind): when a cursor part is picked and the menu wants a drawn pointer, draws the
	// part at the mouse and returns true so the caller leaves ImGui's own pointer off. Call between NewFrame and Render.
	bool DrawCursor(bool a_wanted);
}
