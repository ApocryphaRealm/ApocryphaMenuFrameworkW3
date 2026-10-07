# Changelog - Apocrypha Menu Framework (The Witcher 3 Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.4 - 2026-10-07 - untested

### Added
- **[Display] uDrawPath (troubleshooting).** A Nexus report (2026-10-07, Vortex, 5.00c, DX12, frame generation off): the
  menu opened - the game stopped taking input, F1 / Esc closed it again, the log reported the window shown at a valid
  place - but nothing appeared on screen or in Steam's F12 screenshot. AMF draws before the game's Present reaches NVIDIA
  Streamline and dxgi, which is what lets Steam's screenshot see it; if something later in that chain covers or replaces
  the frame, the menu is lost. 0 (the default) keeps that; 1 draws at dxgi's own Present, after Streamline and anything
  inside it.
- **The first three frames AMF draws are logged in full:** the swap chain and back buffer (size, format), the draw
  lists, vertices and display rectangle, SDR or the HDR composite, the queue (type, its device, the module its object
  comes from - Streamline's proxy or D3D12), the thread, and which Present drew. Enough to tell a draw that never lands
  from one something later covers.
- **The loader also looks in the game's own bin\x64_dx12** when it was loaded from somewhere else (MO2's "Force load
  libraries" loads it from the mod's real folder, where the virtual folder shows no other mod's .asi - a player's
  question on Nexus, 2026-10-07). Each .asi is loaded once, by file name. Not yet tried in game with Force load.

### Fixed
- **D-pad right on the Mods row skipped its own controls.** On the row with the alphabetical tick box, the A-Z / Z-A
  switch and the Sort button, right went straight across to the options pane instead of to the next control (the
  owner, 2026-10-07: "pressing D-pad right skips past the toggle and sort button and goes to the right pane"). A
  right press in the list pane is now decided one frame late, the way the options pane's sideways press already
  is: if it moved the highlight to a control beside it, it stays in the list pane; a press that moved nothing
  (on a menu entry, or on the row's last control) goes across to the options as before. Renderer.cpp:
  g_pendingSideRight.

## 1.0.3 - 2026-10-06

- **An installer, so the loader works with Vortex.** AMF's ASI loader (dinput8.dll) has to be a real file in the
  game's bin\x64_dx12. 1.0.2 shipped it in a Root folder for Mod Organizer 2's Root Builder - and Vortex deploys a
  Root folder as it is, as <game>\Root\..., where the game never loads it. The download is now a FOMOD with one
  question: "Vortex, or installing by hand" (the loader goes to bin\x64_dx12), "Mod Organizer 2 with Root Builder"
  (Root\bin\x64_dx12, as before) or "I already have an ASI loader" (left out). Checked in Vortex 2.8.0: installed
  with the Vortex choice, the game started from its own folder with no Mod Organizer 2 - the loader attached, loaded
  ApocryphaMenuFramework.asi, and the menu drew.

## 1.0.2 - 2026-10-06 (posted)

- **The menu on an HDR screen.** With the game in HDR, the menu's colours were written as if for an ordinary screen
  and came out garish (AMF's own green toggle as pure green, item icons over-saturated - found in Item Explorer's item
  card, 2026-10-06). When the back buffer is not 8-bit, the menu now draws into an 8-bit image of its own and one pass
  puts it on the screen in the screen's own encoding: HDR10 (PQ, BT.2020) or scRGB, at the game's own paper white
  ([Visuals] HdrPaperWhite), or as it is in SDR. The game's colour space (SetColorSpace1) decides, else its HDR switch;
  `[Display] uHdrMode` = 0 auto / 1 always SDR / 2 always HDR overrides. An 8-bit screen is drawn as before. Checked
  outside the game with `tools/hdr_test.cpp` against `tools/hdr_expected.py` (within one 10-bit step).
- **The 3D preview for other mods** (for Item Explorer's turning item; the owner: "its appearance with rotation on
  controller and keyboard and mouse"): `AMF_PreviewCreate` (a model: vertices, indices, draws, block-compressed
  textures - `sdk/include/AMFPreview.h`), `AMF_PreviewRender` (draw it this frame at a size and from a view; returns
  an ImGui texture) and `AMF_PreviewRelease`. The framework draws it on its own: lit, normal-mapped, a soft shine, two
  times supersampled, into a target of its own before ImGui. The game's renderer is not involved. Checked outside the
  game with `tools/preview_test.cpp` (the same code on a device of its own).

### Also in 1.0.2

- **Textures for other mods:** `AMF_CreateTextureRGBA(rgba, w, h)` and `AMF_ReleaseTexture` (`AMF::CreateTextureRGBA` /
  `ReleaseTexture` in the SDK) turn a page's own pixels into an ImGui texture - for Item Explorer's item card, which
  decodes the game's item icons itself.

### Also in 1.0.2

- **The on-screen keyboard for pages drawn with the C++ ImGui.** The keyboard learns which items are text boxes from the
  framework's ig* exports, which a page on the shared C++ ImGui never calls - so A on Item Explorer's search boxes did
  nothing (the owner, 2026-10-06). New `AMF_NoteTextField(itemId)` (`AMF::NoteTextField` in the SDK): such a page names
  each text box right after drawing it.

### Also in 1.0.2

- **Game setting values for other mods** (for Item Explorer on Witcher 3): `AMF_SetGameVar`, `AMF_WatchGameVar` and
  `AMF_GetGameVar` (and `AMF::SetGameVar` / `WatchGameVar` / `GetGameVar` in the SDK header). A native mod cannot call
  WitcherScript, so it talks to its own script through a value in its own Options > Mods XML; the framework's engine bridge
  reads and writes that value on the game thread, so a second mod need not find the settings natives or hook the game's
  frame again.
- Writes from other mods wait until the game is settled: 30 s after the engine bridge is up and the game's menu has been
  shown once (120 s without that signal), and only for a var the game already reads back. Item Explorer's first test
  wrote two seconds into start-up; the game's SetVarValue faulted, the guard caught it, and the game hung before its
  window came up (Main Agent's run, 2026-10-06). AMF's own writes already waited 30 s for the same reason.

### Also in 1.0.2 - nothing freed under a frame that still shows it

- **Closing Item Explorer's 3D item card crashed the game** (the owner's run, 2026-10-06: the driver's bug reporter came
  up on Close). The card's Close freed the preview's target in the same frame whose draw list still showed it, and the
  GPU read a destroyed resource. Released previews and textures (`AMF_PreviewRelease`, `AMF_ReleaseTexture`) are now
  freed only once the frames that may still use them have finished on the GPU, descriptor slots included; nothing
  waits for the GPU on the spot any more. Checked outside the game: `preview_test loop` re-enacts it 40 times under the
  debug layer with GPU-based validation, clean; `loop-now` (free at once) dies with the same access violation. The HDR
  intermediate now follows the real back buffer's size, and says so if the swap chain's description ever differs.

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
