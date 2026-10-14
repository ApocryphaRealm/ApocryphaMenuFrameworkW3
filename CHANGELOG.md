# Changelog - Apocrypha Menu Framework (The Witcher 3 Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.6 - 2026-10-13 - untested

### Added
- **AMF does nothing while the game is loading** (the Skyrim framework's 2.2.0 loading guard; the owner: "amf shouldn't work during loading in as that can cause issues"). From start-up until the title screen is up, and during every loading screen after that (loading a save, a new game, fast travel, travelling to another region), the menu does not open, AMF's keys do nothing, the game gets every key, click and button press, and AMF waits before changing any game setting. If the menu is open when a load starts, it closes, and the log says "Framework window hidden (closed by the loading guard)". The signal comes from the game's own script events (the framework's script relays them through three hidden settings, annotations only - nothing to merge). If the game never says a load has ended, AMF works again after two minutes, so it is never left dead. DevBench amf.menu op=state carries loadingGuard {active, reason, since, edges, failOpens, lastEdge, signal}.
- **The mouse wheel moves a slider** (Skyrim 2.2.0). With the pointer on a slider, or while it is held, one notch moves it one step of the last digit shown: up increases, down decreases, and it stops at either end. The page does not scroll while the pointer is on a slider, and scrolls as before anywhere else. This works on the framework's own sliders, on the mod-menu pages (the change reaches the game as a drag does) and on other mods' sliders drawn through AMF. A slider that applies its value when let go takes the wheel too: a quarter of a second after the last notch, Text size resizes the text, and Pointer speed, Window opacity and the mod pages' column gap and row spacing save, once per run of notches.
- **Font Awesome icons on AMF's own pages** (Skyrim 2.2.0): the side list, the page headings, the Settings, Controls and Help tabs, the Appearance sub-tabs, the Help sections, the Search box, Filter, Fold, Rename and the buttons carry a small icon in front of their text. The translations are unchanged, and a remembered tab stays put whatever the language. Without the icon file the labels show as plain text.
- **Every Chinese, Japanese and Korean character a page draws has a glyph, with any font** (Skyrim 2.2.0, a Simplified Chinese player's report there). The text atlas asked for the 2,500 common Simplified hanzi only, so Traditional forms, most of Simplified and a mod page's own text could draw as "?". Now the atlas holds Latin Extended and Cyrillic always, the whole CJK block for Chinese, kana plus every kanji for Japanese and every Hangul syllable for Korean where it fits, plus every character a page actually draws that it lacked, added at the next frame. A Windows face for the script (YaHei, Yu Gothic, Malgun Gothic, Segoe UI) is merged into any face that lacks a character, a picked font included. The atlas is fitted to the DirectX 12 texture limit and a 64 MB budget. DevBench amf.menu op=font reports the fit step and the characters added.
- **Start-up diagnostics for graphics proxies:** the log names every dxgi / d3d12 / ReShade module loaded from outside System32 (with its product name and version), says whether a dxgi.dll or d3d12.dll sits in the game's folder, names the path each frame is drawn through ("game-facing slot", "dxgi Present via the creation hook", "dxgi Present via the start-up probe", "uDrawPath=1 at dxgi's Present"), and warns once if no Present has reached AMF 15 seconds after start-up. DevBench amf.menu op=state carries present {path, module, classesHooked, probe, queueFrom, anyPresent, inputSetUp}.

### Changed
- **A download for TW3SE players** (the owner, 2026-10-14: most Witcher 3 installers other than Mod Organizer 2 and Vortex can't read an installer, so "just have it as an additional file that says the no DLL or no dinput DLL version"). A new optional file, "Apocrypha Menu Framework - No dinput8 (for TW3SE)", holds everything except AMF's ASI loader, laid out like the game folder. TW3SE starts through its own dinput8.dll and loads ApocryphaMenuFramework.asi itself, at the moment AMF's loader would; AMF's dinput8.dll over it would stop TW3SE loading without a word. The main file and its installer choices are unchanged. The README says: using TW3SE, download the No dinput8 file instead of the main file (or delete AMF's dinput8.dll). AMF's own loader does not chain-load TW3SE. The package tool now writes both optional downloads (No Installer and No dinput8) beside the main package for every version.
- **The focus frame goes wherever the controller or keyboard highlight is, on every page** (the Skyrim framework's nav frame, 2.1.5 / 2.1.9; the owner, 2026-10-14: "there's a focus frame wherever the controller box is, especially over toggles"). An item got the frame only when it drew a highlight of its own, so the on/off switches other mods draw on their own pages (HUD Position Manager, Camera Configuration Menu, Item Explorer) showed nothing under the D-pad. Now, whenever the highlighted item draws no frame itself, AMF draws the same frame (the game's drawn frame, or the Highlight art picked on Appearance > Art) round ImGui's own record of where the highlight is, as the Skyrim framework does. Nothing changes in those mods. The frame sits tightly round the control the highlight is on, not round its row (the owner, the same day: it "selects an entire row instead of the toggle specifically"): round a switch's track (no longer the track and its label), a slider's bar, a dropdown's box, a button; on the mod-menu pages (the game's own mod menus) round the control, no longer the label and the control together. List rows with nothing inside them (the side list, a dropdown's options, Item Explorer's rows) keep the row frame. On other mods' pages the frame is the only one: mods that draw with their own copy of ImGui (HUD Position Manager, Camera Configuration Menu, Item Explorer) also showed ImGui's bright-blue nav box inside AMF's frame on their sliders, dropdowns and tabs; the box is now hidden while a mod's page draws, as AMF's own pages have no box. A shoulder button that opens another tab while the highlight is on that tab bar now moves the highlight onto the tab it opened - on AMF's tabs and sub-tabs and on a mod's own tab bar (the Skyrim framework's 2.1.5, extended to mods' bars); with the highlight down in the page, it starts on the new page's first control as before. Moving the mouse onto an item hides the controller highlight, so only one item is framed at a time (as a click already did); the next D-pad or arrow press brings it back, and A, Enter or Space lights it and acts. DevBench amf.menu op=state carries focusFrame {source (own, fallback, hidden, none), navId, fallbackRect, hoverMarks}.
- **Japanese and Chinese text wraps between characters** (Skyrim 2.1.8, a Japanese player's report there; the owner: "we can do the same fix for The Witcher 3 version in the next version"). The line breaker broke only at spaces, so a long run of kana or hanzi was pushed whole onto the next line, and a run longer than a line was cut anywhere, so a line could start with a comma or the long-vowel mark. A line may now break between any two characters when either is Japanese or Chinese, with the kinsoku rules: no line starts with closing punctuation, a small kana or the long-vowel mark, and none ends with an opening bracket. A number such as "1.00" is never split. Korean and English text wrap as before.
- **Every control has a visible outline in every theme** (Skyrim 2.2.0). Boxes, buttons, text boxes and tick boxes drawn with a theme's art had no outline, so on a dark panel they could not be seen until hovered. They now always draw a thin line (at least 1 px) in the theme's border colour, as do switches whose art has no outline; the art frame still appears round the control under the pointer or the controller. If a theme's border colour, or one you picked, would not reach 4.5:1 contrast against the panel or a control face, the text colour is used instead and the log says so. Norden's border is a little stronger (#989D9F), and so are the Oblivion theme's lines (#584734).
- **A slider's value reads over its handle in every theme** (HadToRegister): the number is drawn with a thin outline in the window's own colour, the same text in the same place.
- **On/off settings on other mods' pages drawn through AMF's C interface are AMF's switch**, not a stock tick box (rule 32, as the Skyrim framework's 2.1.9), with the same label, value and result. The Mods row keeps its tick box for alphabetical order beside the A-Z / Z-A switch, as the owner asked for it.
- AMF no longer loads a d3d12.dll from the game folder itself: d3d12.dll is loaded on first use, from Windows' own folder, as dxgi.dll already was.

### Fixed
- **AMF and Intuitive Menu (or any mod that replaces the Options menu's showOptionsPanel) stopped the game's scripts from compiling** (a player's report: "@replaceMethod duplicate found for 'CR4IngameMenu::showOptionsPanel'"). 1.0.5's "Here only" replaced that method, and two replacements of one method cannot compile. It is now a wrap: the game's method (or the other mod's replacement) runs as it is, and only when a mod is set to "here only" does AMF build the list again with the game's own function, take the hidden menus out and send it to the menu. With nothing set to "here only" AMF adds nothing to the game's code. With a mod set to "here only" beside a mod that builds its own Options list, the list AMF sends is the game's, so that mod's own changes to it are not kept. AMF's script now replaces no game method at all. Proven offline: AMF 1.0.5 beside a stub that replaces the same method fails with that error; this build beside it compiles, alone and with the full ticked mod set. The README and the Nexus page say the hundreds of "Warning ... not marked as abstract" lines in the compile window are the game's own; only "Error" lines matter.
- **The game could hang at the start-up splash when the game's menu entry was left switched on** (a test launch, 2026-10-14 00:23). The framework's entry in the game's menu works through a hidden game setting, ApocryphaMenuFramework.OpenRequest, which the game saves with its own settings. The game started with that setting saved as "true" from an earlier session; AMF read it about three seconds into start-up and set it back straight away, the game faulted inside its own settings code (it is not ready for a change that early), its crash reporter came up and the game hung. Now:
  - AMF makes no change to any game setting while the game is starting up or loading, whatever asks for it (the engine bridge refuses the write and logs it).
  - A request found already set when the game comes up is left over from an earlier session, not a pick: AMF leaves the window closed, clears the setting quietly 30 seconds after the title screen is up, and saves it, so the next start does not find it again. The log says "game menu entry: ApocryphaMenuFramework.OpenRequest was already true when the game came up" and then "the left-over OpenRequest cleared and saved".
  - Picking the entry in the game's menu opens AMF as before.
  - AMF's guard round each engine call only notes a fault in the log: the game's own crash handling sees the fault first. So what keeps the game up is never calling at a moment that can fault. Offline check: tools\open_request_check.cpp (xmake run open_request_check).
- **F1 did nothing with ReShade installed** (a Nexus report, 2026-10-12). AMF hooked only the first kind of swap chain it saw being created, and only one that it saw being created; with ReShade (a dxgi.dll or d3d12.dll proxy, or ReShade's own swap-chain wrapper) between the game and DirectX, the swap chain the game's frames go through could be one it never hooked, so no Present reached AMF, the menu was never set up and F1 was never read. Now every kind of swap chain and factory is hooked, not only the first; at start-up a throwaway swap chain (on its own thread, released at once) finds DirectX's own Present whatever path the game's swap chain is created on; and when the presenting command queue was not seen at creation, AMF takes it from the presenting thread's own submissions. The Steam F12 ordering, frame generation's own present thread and [Display] uDrawPath (with the 1.0.4 fix by 美猫少佐 (Bineko Major)) are unchanged.
- **A quick click or key tap is never lost** (Skyrim 2.2.0). A click or tap shorter than a frame, or two clicks inside one frame, could register as nothing (measured offline: 1 of 17 one-millisecond clicks at 60 fps). Now every press counts, on every page, with the mouse and the keyboard.

## 1.0.5 - 2026-10-11 - working

### Added
- Export check (plan row 60, Skyrim's): tools\exports.txt lists every name the .asi exports (1453 - the AMF_* API, the SKSE Menu Framework-compatible exports and cimgui), and tools\check-exports.ps1 fails when one of them is missing from a build (dumpbin found through vswhere, as the build finds its tools); tools\package.ps1 runs it first and packages nothing on a failure.
- Here only (plan row 52): Settings > Mod menus gives each mod three places - here and in the game's menu, the game's menu only, or here only, which leaves the mod out of the game's own Options > Mods from the next time it opens. The framework keeps the hidden setting ApocryphaMenuFramework.HiddenModMenus equal to those mods' menu paths, and its script replaces CR4IngameMenu.showOptionsPanel with the game's own body plus that one filter (no installed mod replaces or wraps that method). ModMenusImport.txt stores 2 for here only. DevBench op=modmenus placement; op=modsort action=set key place 0|1|2.
- Per-save values on The Witcher 3 (plan row 64): the framework's script wraps CR4Game.OnSaveCompleted (successful saves) and CR4Game.OnGameStarted, plus CR4IngameMenu.LoadSaveRequested for the save picked on the load page, and leaves hidden settings (SaveSeq/SaveName, LoadSeq/LoadName, PendingLoad in the hidden ApocryphaMenuFramework group); the framework reads them a few times a second and writes or reads the save's own <save>.amf-state.ini beside it in gamesaves. DevBench op=state perSave {saveSeq, loadSeq, saves, loads, saveName, loadName, lastWritten, lastRead}.
- Help bar (Skyrim 2.1.5, plan row 17): the highlighted or hovered setting's help on the framework's Settings page shows in a bar under the right pane; [Display] bHelpBar, on by default, with its switch on Appearance; off keeps each help as a line under its setting. A mod's page has no help text in The Witcher 3's menus, so the bar is hidden there. DevBench op=setting name helpBar; op=state helpBar {on, shown, key}.
- The on-screen keyboard follows the art: its keys take the Buttons art, its panel the Background and Frame. AMF_DrawThemeFrame draws the picked frame. Help > How it looks describes Art. DevBench: op=keyboard (action show / hide) and the keyboard's art in op=state.
- Norden, Norden - Black, Vel'dun and the built-in Skyrim and Oblivion themes name their own part in every kind, as on Skyrim 2.1.6; Skellige keeps its current look.
- Appearance > Art covers every kind (Skyrim 2.1.6): boxes, buttons, tick boxes, the switch and its knob, slider grabs and tracks, scroll bars and tracks, tabs, arrows, section lines and the mouse pointer, each picked per theme. The switch and knob art draw through the switch widget, the pointer in place of ImGui's arrow; what can be selected has no frame of its own - the hover frame goes round it. The Mouse pointer colour joins Appearance > Colours.
- DevBench amf.menu op=art (kind, set; no args reports every kind's part, texture size, mip levels and whether it loaded); op=state reports the Art sub-tab and every kind's part; op=skin reports the frame's band and each texture's mip levels.
- A framed pane's scroll bar sits inside its frame's band (uBand in the frame's .ini), so the frame no longer covers it.
- Appearance > Colours: a colour for each art kind (Skyrim 2.1.6) - buttons, tabs, slider tracks, tick marks, scroll bar and track, section lines, arrows, switch knob and the popup frame art, each following the colour it used to share until picked.
- Themes name their art by part (sFrameArt=...): Skellige keeps its frame (the Oathvein part, the same art), Norden, Norden - Black and Vel'dun their own; the built-in Skyrim theme uses the knotwork part, Oblivion the map-edge part and the parchment background.
- The art library (Skyrim 2.1.6): AMF\assets holds every theme's parts (Skyrim, Norden, Oathvein, Vel'dun, Oblivion - 159 files). Settings > Appearance > Art, a fourth sub-tab: pick the frame, background, the frame round popups and lists, and the highlight frame from any set, kept per theme as [Art.<theme id>] in User.ini. A missing file falls back to the theme's own art, then the built-in look.
- Dear ImGui art hooks (Skyrim 2.1.6's amf-art-hooks patch, every call site marked [AMF] in extern/imgui): boxes, buttons, tick boxes, slider grabs and tracks, scroll bars and tracks, section lines, tabs, arrows and popups can be drawn from the art library in place of ImGui's own shapes.
- Art textures carry a full mip chain built on the CPU (DX12 has no GenerateMips): each level a 2x2 box filter of the one above, colour weighted by alpha, down to 1x1; the menu's sampler reads the levels (MaxLOD), so art drawn far below its own size stays smooth - a one-level texture samples exactly as before.
- Help > How it looks describes the Colours page.
- Settings > Appearance > Colours: your own colour for each part of the menu (background, borders, frame art, boxes, text, selection, sliders, switches, headings, help text, hover highlight), kept for each theme, with a live preview. Picks show only in the preview until Apply, unless Show changes everywhere straight away is on.
- Spacing of mod pages: Settings > Mod menus has sliders for the gap between columns and the space between rows of the pages built from the game's Options > Mods menus ([ModMenus] uColumnGap / uRowSpacing).
- **Filter, on the Mods row** between Fold and Sort ([ListFilter] sWords, bMatchAll), as on the Skyrim framework: a small window of saved words, each with a coloured button that cycles as Mod Organizer 2's filters do (left click off -> green + -> red -, right click back). + words show only the mods whose name holds them (any one of them, or every one with 'Match every + word'); - words hide the mods that hold them. The button reads 'Filter (n)' while n words are in use, and the words are kept between games. The text box takes the on-screen keyboard. DevBench amf.menu op=filter (add / state / cycle / remove / clear / matchAll; no action reports the words and how many rows the list shows), and op=state carries filter {words, matchAll, shownRows}. Every string in the eleven languages (Japanese uses キーワード for 'word', from a player's report).
- **DevBench (amf.menu) for the new controls:** op=mousemove (a relative movement in raw counts through the real scaling); op=setting (pointerSpeed, wheelSwitchesTabs, foldAllSeparators, systemMenuRow, pauseGame - saved as the page saves them); op=inject device=mouse code=8 / 9 is one wheel click up / down; op=state carries pointer {speed, gameUiSensitivity, gameHardwareCursor, gameSettingsRead, scale, rawDrives}, wheelSwitchesTabs, foldAllSeparators, systemMenuRow and tabBarHasNav.
- **The entry in the game's menu has a working switch** (Settings > General; [Menus] bSystemMenuRow, on by default). AMF keeps the hidden game setting ApocryphaMenuFramework.MenuEntry equal to it, and the framework's script leaves the entry out of the title screen and pause menu while it is off. Up to 1.0.4 the switch was hidden and the INI key did nothing.
- **Fold, on the Mods row** between A-Z and Sort ([Display] bFoldAllSeparators): switched on, every separator in the list folds; switched off, every one opens. It acts when it is switched - a separator folded or opened by hand afterwards stays as it is.
- **Mouse wheel over a mod's tabs** ([Menu] bWheelSwitchesTabs, on by default): with the pointer over a mod's tab bar, rolling the wheel down opens the next tab and up the previous one, and the page under the bar does not scroll. Anywhere else the wheel scrolls as before.
- **Pointer speed** (Settings > General; [Menu] fPointerSpeed, 0.25-4.00, a precise slider): how fast the mouse moves the menu's pointer, where 1.00 is the game's own menu cursor. Measured in game at 3200x1800: the game moves its menu cursor one pixel per raw mouse count, with no acceleration, times its own UI mouse sensitivity (Settings > Controls) - 400 counts moved it 400 px at sensitivity 1, and 500 counts moved it 1000 px at 2. The pointer now moves by raw counts at that speed (the game's setting is re-read once a second; with the game's hardware cursor on, one pixel per count) times the slider. Up to 1.0.4 it followed the Windows pointer, with Windows' own speed and acceleration, so it did not move like the game's cursor.

### Changed
- The framework's own text can carry Font Awesome icons; the Filter popup's remove button on the Mods row is now a clear icon.
- Mod pages draw section headings in the heading colour, values shown as words in grey, and their notes in the help colour, following Settings > Appearance > Colours.
- Norden, Norden - Black and Vel'dun themes use a plain background in their panel colour instead of a grain texture, as Skellige already does.
- Skyrim theme: the knotwork frame no longer fills its middle with black, and its highlight, tick marks and slider grabs are the knotwork's white instead of gold.
- Settings > Appearance is split into two sub-tabs, Theme and text and Window, instead of one long page; the shoulder buttons walk them.
- **The game-menu switch's help text** describes the Witcher 3 entry (above Settings on the title screen and pause menu); it carried the Oblivion port's 'System page' wording. Skyrim's journal-row text and its 'next time the journal is opened' line are removed from the translation files. Every new and changed line is in all eleven languages.
- **Y on a controller is the framework's in its window.** It reached ImGui as its activate button, so on a page Y switched a toggle like A and opened a slider as a text box. Now, in the list, Y opens the highlighted row's options as before; in a page it puts the highlight on the page's main tabs (and the list no longer takes a Y pressed in a page). A mod's own window still gets Y, and the on-screen keyboard keeps it as backspace. The Controls page's text for the action says both jobs.

### Fixed
- **Per-save values named the wrong save** (found in the 1.0.5 test run): the game's recent-saves list is not newest-first, so a fresh manual save was filed under a three-day-old autosave. The framework now takes the newest .sav in gamesaves itself, for a save just made and for Continue; a save picked on the load page is still named by the game.
- **The help bar followed switches** (found in the 1.0.5 test run): a switch's own item is not the last one drawn (its label is), so the bar never saw a highlighted or hovered switch and kept the help of the last slider; and when the highlight moved to an option with no help, the previous option's help stayed. The bar now reads the switch itself and empties when the highlight is on an option without help.
- **The A-Z / Z-A switch on the Mods row took only one controller press:** its ImGui ID was made from its label, which flips between A-Z and Z-A, so each press made a new item and the next A did nothing until the highlight left and came back. It now has a fixed ID.

## 1.0.4 - 2026-10-11 - working

### Fixed
- **uDrawPath=1 drew nothing at all.** The game-facing Present marks the thread as "inside a Present" before passing the
  frame on, and with uDrawPath=1 it leaves the drawing to dxgi's own Present - which saw that mark and skipped the frame
  too. dxgi's Present now draws under uDrawPath=1 even when reached inside the game-facing one, with a guard of its own
  so a nested Present never draws twice; uDrawPath=0 is unchanged. Diagnosed and patched by the player who reported the
  invisible menu (美猫少佐 (Bineko Major), Nexus, 2026-10-11), whose patched build showed the menu on their DX12 + Vortex setup.

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
