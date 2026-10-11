# Changelog - Apocrypha Menu Framework (The Witcher 3 Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.5 - 2026-10-11 - working

### Added
- Spacing of mod pages: Settings > Mod menus has sliders for the gap between columns and the space between rows of the pages built from the game's Options > Mods menus ([ModMenus] uColumnGap / uRowSpacing).
- **Filter, on the Mods row** between Fold and Sort ([ListFilter] sWords, bMatchAll), as on the Skyrim framework: a small window of saved words, each with a coloured button that cycles as Mod Organizer 2's filters do (left click off -> green + -> red -, right click back). + words show only the mods whose name holds them (any one of them, or every one with 'Match every + word'); - words hide the mods that hold them. The button reads 'Filter (n)' while n words are in use, and the words are kept between games. The text box takes the on-screen keyboard. DevBench amf.menu op=filter (add / state / cycle / remove / clear / matchAll; no action reports the words and how many rows the list shows), and op=state carries filter {words, matchAll, shownRows}. Every string in the eleven languages (Japanese uses キーワード for 'word', from a player's report).
- **DevBench (amf.menu) for the new controls:** op=mousemove (a relative movement in raw counts through the real scaling); op=setting (pointerSpeed, wheelSwitchesTabs, foldAllSeparators, systemMenuRow, pauseGame - saved as the page saves them); op=inject device=mouse code=8 / 9 is one wheel click up / down; op=state carries pointer {speed, gameUiSensitivity, gameHardwareCursor, gameSettingsRead, scale, rawDrives}, wheelSwitchesTabs, foldAllSeparators, systemMenuRow and tabBarHasNav.
- **The entry in the game's menu has a working switch** (Settings > General; [Menus] bSystemMenuRow, on by default). AMF keeps the hidden game setting ApocryphaMenuFramework.MenuEntry equal to it, and the framework's script leaves the entry out of the title screen and pause menu while it is off. Up to 1.0.4 the switch was hidden and the INI key did nothing.
- **Fold, on the Mods row** between A-Z and Sort ([Display] bFoldAllSeparators): switched on, every separator in the list folds; switched off, every one opens. It acts when it is switched - a separator folded or opened by hand afterwards stays as it is.
- **Mouse wheel over a mod's tabs** ([Menu] bWheelSwitchesTabs, on by default): with the pointer over a mod's tab bar, rolling the wheel down opens the next tab and up the previous one, and the page under the bar does not scroll. Anywhere else the wheel scrolls as before.
- **Pointer speed** (Settings > General; [Menu] fPointerSpeed, 0.25-4.00, a precise slider): how fast the mouse moves the menu's pointer, where 1.00 is the game's own menu cursor. Measured in game at 3200x1800: the game moves its menu cursor one pixel per raw mouse count, with no acceleration, times its own UI mouse sensitivity (Settings > Controls) - 400 counts moved it 400 px at sensitivity 1, and 500 counts moved it 1000 px at 2. The pointer now moves by raw counts at that speed (the game's setting is re-read once a second; with the game's hardware cursor on, one pixel per count) times the slider. Up to 1.0.4 it followed the Windows pointer, with Windows' own speed and acceleration, so it did not move like the game's cursor.

### Changed
- Skyrim theme: the knotwork frame no longer fills its middle with black, and its highlight, tick marks and slider grabs are the knotwork's white instead of gold.
- Settings > Appearance is split into two sub-tabs, Theme and text and Window, instead of one long page; the shoulder buttons walk them.
- **The game-menu switch's help text** describes the Witcher 3 entry (above Settings on the title screen and pause menu); it carried the Oblivion port's 'System page' wording. Skyrim's journal-row text and its 'next time the journal is opened' line are removed from the translation files. Every new and changed line is in all eleven languages.
- **Y on a controller is the framework's in its window.** It reached ImGui as its activate button, so on a page Y switched a toggle like A and opened a slider as a text box. Now, in the list, Y opens the highlighted row's options as before; in a page it puts the highlight on the page's main tabs (and the list no longer takes a Y pressed in a page). A mod's own window still gets Y, and the on-screen keyboard keeps it as backspace. The Controls page's text for the action says both jobs.

### Fixed
- **The A-Z / Z-A switch on the Mods row took only one controller press:** its ImGui ID was made from its label, which flips between A-Z and Z-A, so each press made a new item and the next A did nothing until the highlight left and came back. It now has a fixed ID.

## 1.0.4 - 2026-10-11 - working

### Fixed
- **uDrawPath=1 drew nothing at all.** The game-facing Present marks the thread as "inside a Present" before passing the
  frame on, and with uDrawPath=1 it leaves the drawing to dxgi's own Present - which saw that mark and skipped the frame
  too. dxgi's Present now draws under uDrawPath=1 even when reached inside the game-facing one, with a guard of its own
  so a nested Present never draws twice; uDrawPath=0 is unchanged. Diagnosed and patched by the player who reported the
  invisible menu (bineko, Nexus, 2026-10-11), whose patched build showed the menu on their DX12 + Vortex setup.

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
  question on Nexus, 2026-10-07). Each .asi is loaded once, by file name. Tried in game with Force load (2026-10-11): Mod Organizer 2's
  virtual folder reports the loader as sitting in bin\x64_dx12 itself, so the loader reads that one folder and
  loads every .asi from it (five, each once); the extra folder is read only where the loader really sits
  outside the game's folder.

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
