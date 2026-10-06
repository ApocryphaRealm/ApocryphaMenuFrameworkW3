# Changelog - Apocrypha Menu Framework (The Witcher 3 Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.0 - 2026-10-05 - untested

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
- **"Apocrypha Menu Framework" in the game's own menu**, just above Options on the title screen and in the pause menu.
  That's the way in with a controller. It's added by a small script using the game's script annotations, so no game
  script is replaced and nothing needs merging.

### The menu list
- Rename, reorder, favourite, separators you can name and fold, and a right-click (or controller Y) menu on every row.
- **Layout presets**: save the list's order, separators, favourites and names under a name, load them back, or delete
  them.

### Options
- **Pause the game while the menu is open** (off by default): time, actors and weather stop behind it, as in the game's
  own menus.
- **Skip the intro videos** (off by default): the game goes straight to its main menu from the next start, without the
  disclaimer, legal and logo videos or the story recap. They are left out of the game's own start-up menus; no game
  file is replaced.
- **The window**: move it by its top row, resize it freely, see-through mode with a Window opacity slider. It always
  stays clear of the screen's edges.
- **Four themes** (Oathvein by default, Norden, Norden Black, Veldun), a choice of fonts, and a text size.
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
