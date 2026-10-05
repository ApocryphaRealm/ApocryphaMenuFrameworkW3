// The System row (save / load / quit inside the game's own menu) - not yet wired for Witcher 3. M3 re-points it at the
// game's own functions through the Red3 engine layer (4. plans\amf-witcher3\PLAN.md); until then every call answers
// "nothing there" so the renderer and DevBench draw and report without it.
#include "SystemRow.h"

namespace systemrow
{
	void Tick() {}
	bool Install() { return false; }
	bool WasInjected() { return false; }
	const char* FoundPath() { return ""; }
	std::string ListJson() { return "[]"; }
	bool GetPanelRect(float&, float&, float&, float&) { return false; }
	float PaneLeft() { return 0.0f; }
	const char* ArtKey() { return ""; }
	bool MeasurePath(const std::string&, float&, float&, float&, float&) { return false; }
}
