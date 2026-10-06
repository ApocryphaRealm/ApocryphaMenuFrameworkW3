# Changelog - Apocrypha Menu Framework (The Witcher 3 Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## Unreleased (next: 1.0.4) - 2026-10-06

- **Textures for other mods:** `AMF_CreateTextureRGBA(rgba, w, h)` and `AMF_ReleaseTexture` (`AMF::CreateTextureRGBA` /
  `ReleaseTexture` in the SDK) turn a page's own pixels into an ImGui texture - for Item Explorer's item card, which
  decodes the game's item icons itself.

## Unreleased (next: 1.0.3) - 2026-10-06

- **The on-screen keyboard for pages drawn with the C++ ImGui.** The keyboard learns which items are text boxes from the
  framework's ig* exports, which a page on the shared C++ ImGui never calls - so A on Item Explorer's search boxes did
  nothing (the owner, 2026-10-06). New `AMF_NoteTextField(itemId)` (`AMF::NoteTextField` in the SDK): such a page names
  each text box right after drawing it.

## Unreleased (next: 1.0.2) - 2026-10-06

- **Game setting values for other mods** (for Item Explorer on Witcher 3): `AMF_SetGameVar`, `AMF_WatchGameVar` and
  `AMF_GetGameVar` (and `AMF::SetGameVar` / `WatchGameVar` / `GetGameVar` in the SDK header). A native mod cannot call
  WitcherScript, so it talks to its own script through a value in its own Options > Mods XML; the framework's engine bridge
  reads and writes that value on the game thread, so a second mod need not find the settings natives or hook the game's
  frame again.
- Writes from other mods wait until the game is settled: 30 s after the engine bridge is up and the game's menu has been
  shown once (120 s without that signal), and only for a var the game already reads back. Item Explorer's first test
  wrote two seconds into start-up; the game's SetVarValue faulted, the guard caught it, and the game hung before its
  window came up (Main Agent's run, 2026-10-06). AMF's own writes already waited 30 s for the same reason.

## 1.0.1 - 2026-10-06 - untested

- **The loader moves to `Root\bin\x64_dx12\dinput8.dll`, for Root Builder.** Windows loads `dinput8.dll` beside the
  game's exe before Mod Organizer 2's virtual folder is in place, so under Mod Organizer 2 the loader never loaded from
  the mod (the 2026-10-05 test runs: no AMFLoader.log until it was copied into the real game folder). Root Builder copies
  a mod's `Root` folder into the game folder at launch and removes it after, so with it the download installs as an
  ordinary Mod Organizer 2 mod. Mod Organizer 2 and Root Builder are now listed as requirements, with the reasons
  (the owner, 2026-10-06: "Repackage as 1.0.1"). Vortex and manual installs copy the `Root` folder's contents into the
  game folder as well. The framework itself is unchanged.

## 1.0.0 - 2026-10-05

The first release: one in-game settings menu for The Witcher 3: Wild Hunt - Remastered (patch 5.0, DirectX 12), with
the same menu, themes and controls as the Apocrypha Menu Framework on Skyrim and Oblivion Remastered.

### Your mods' settings in one menu
- **Every mod menu the game reads becomes an AMF page**: the menus a mod adds to the game's Options > Mods. Each mod
  gets one entry, and each of its menu pages is a tab. Toggles, sliders and choice lists draw as AMF controls, with
  their section headings, in the mod's own wording.
- **Changes go through the game's own settings code**, the same code Options > Mods uses, so each mod sees them as if
  made there. They are saved to the game's settings a moment after the last change.
  - AMF finds that code in the running game. If it ever can't, the pages turn read-only and the log says why. AMF never
    crashes the game over it.
- **Choose which mods are listed, and sort them** (Settings > Mod menus): a switch per mod, a name filter, Sort into
  categories (Interface, Combat, Gameplay, Quests and Places, ...), Undo, and a placement you change by hand is
  remembered for the next sort.

### Opening it
- **F1** opens and closes it. The key can be changed under Controls.
- **"Apocrypha Menu Framework" in the game's own menu**, just above Settings on the title screen and in the pause menu.
  That's the way in with a controller. It's added by a small script using the game's script annotations, so no game
  script is replaced and nothing needs merging.

### The menu list
- Rename, reorder, favourite, separators you can name and fold, and a right-click (or controller Y) menu on every row.
- **The Mods row**: a tick box for alphabetical order, an A-Z / Z-A switch (it orders the mods within each
  separator), and a Sort button that sorts every mod menu into categories. Undo is on Settings > Mod menus.
- **Layout presets**: save the list's order, separators, favourites and names under a name, load them back, or delete
  them.

### Options
- **Pause the game while the menu is open** (off by default): time, actors and weather stop behind it, as in the game's
  own menus.
- **Skip the intro videos** (off by default): the game goes straight to its main menu from the next start, without the
  disclaimer, legal and logo videos or the story recap. They are left out of the game's own start-up menus; no game
  file is replaced.
- **Skip the story recap on loading screens** (off by default): loading a save shows the plain loading screen, without
  the narrated recap. AMF sets the game's own loading-screen setting through its settings code; switching it off in
  AMF turns the recap back on.
- **The window**: by default it sits just right of the game menu's black column; move it by its top row, resize it
  freely, see-through mode with a Window opacity slider. It always stays clear of the screen's edges. **Sit beside
  the game's menu column** (on by default) puts it there whenever the title screen or pause menu is open.
- **The game's own look**: the highlighted row - under the mouse or the controller - gets the frame the game draws
  round its own menu entries.
- **Seven themes**: Skellige (the default, on the black of the game's menu), Norden, Norden - Black, Vel'dun and Untarnished, plus the Skyrim and
  Oblivion looks of the framework's other builds. Also a choice of fonts and a text size.
- **Eleven languages**, following the game's text language.

### Controller, keyboard and mouse
- Full controller support: the D-pad walks the menu, the bumpers change tabs, A picks, B goes back, Y opens a row's
  menu, and Start closes. Every button can be rebound under Controls.
- While the menu is open, the game doesn't see your keys, mouse or controller. The press that closes the menu doesn't
  reach it either.

### Good to know
- **Your settings survive updates.** Everything you change lives in `bin\x64_dx12\AMF\User.ini`, which the download
  never contains. With Mod Organizer 2 it goes to overwrite.
- **Steam's F12 screenshot shows the menu.**
- **Other mods can add their own pages and windows** through the framework's API (`sdk/include/AMF.h`).
- **The loader**: `bin\x64_dx12\dinput8.dll` loads the framework when the game starts. It must be a real file in the
  game's `bin\x64_dx12` folder. Mod Organizer 2's virtual folder can't supply it early enough, so with MO2 copy that one
  file into the game folder; everything else installs as a normal mod.
