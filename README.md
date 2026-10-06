# Apocrypha Menu Framework (The Witcher 3: Wild Hunt - Remastered)

One in-game settings menu for every mod - the Apocrypha Menu Framework, on The Witcher 3: Wild Hunt - Remastered
(patch 5.0, DirectX 12). An `.asi` plugin that draws Dear ImGui over the game's DirectX 12 renderer, with the same menu,
themes, controls and mod API as the [Skyrim framework](https://github.com/ApocryphaRealm/ApocryphaMenuFramework).

* What the player gets and where the files go: `dist/README.txt`
* What changed: `CHANGELOG.md` (the pre-release test builds: `DEV-HISTORY.md`)

## How it fits into The Witcher 3

* **Loading.** Witcher 3 5.0 has no script extender. `AMFLoader` builds `dinput8.dll`, which the game imports: it
  forwards to Windows' own DirectInput and loads every `.asi` beside it at the game's entry point.
* **Drawing.** The game's DXGI comes from NVIDIA Streamline (`sl.interposer.dll`). AMF draws when the game calls Present
  on the swap chain it was handed, before the frame reaches dxgi and the overlays hooked there, so Steam's F12
  screenshot includes the menu.
* **Mod menus.** Every `bin\config\r4game\user_config_matrix\pc\*.xml` the game reads becomes an AMF page. Values are
  read and written through the game's own `SetVarValue` / `SaveUserSettings`, found in the running exe by name and byte
  pattern (`src/Red3.cpp`), and called on the game thread. If they are not found, the pages are read-only.
* **Script.** `dist/Mods/modApocryphaMenuFramework` uses the game's script annotations only (`@wrapMethod`): the entry
  in the game's own menu, and the intro-video switch. No game script is replaced, so there is nothing to merge.

## For mod authors

`sdk/include/AMF.h` is the whole public API: one header, nothing to link, safe when the framework is not installed.
`sdk/example/main.cpp` is a complete mod with two pages built against it. Draw with the ordinary C++ Dear ImGui API
(1.90.8, docking branch) through the framework's context, or with the cimgui `ig*` functions the framework exports.

## Building

* [xmake](https://xmake.io) 3.0+, a C++23 compiler (MSVC), and the submodules: `git clone --recurse-submodules`.
* `xmake build ApocryphaMenuFramework` (the framework, `.asi`) and `xmake build AMFLoader` (`dinput8.dll`). Dear ImGui
  is vendored under `extern/imgui`; MinHook comes from xmake's package registry.
* `tools/red3_test.cpp` and `tools/modmenus_test.cpp` run the engine-function search and the mod-menu reader outside
  the game.
* The `.asi` and `dist/bin/x64_dx12/AMF` go to the game's `bin\x64_dx12`; `dist/bin/config` and `dist/Mods` to the
  game folder; `dinput8.dll` must be a real file in `bin\x64_dx12` (Mod Organizer 2's virtual folder is not in place
  that early).

## Licence

GPL-3.0-or-later (`LICENSE`, `dist/NOTICE.md`); third-party notices in `dist/THIRD_PARTY_NOTICES.md`. `sdk/` is MIT so
any mod can vendor it.
