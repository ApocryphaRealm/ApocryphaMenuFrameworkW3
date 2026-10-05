# Changelog - Apocrypha Menu Framework (The Witcher 3 Remastered)

Newest first. Versions are issued by the version gate; a number here is one a build earned by working in game.

## 1.0.5 - 2026-10-05 - untested

### Added
- **Pause the game while the menu is open** (Settings > General; off by default, as in Skyrim). AMF holds the game's own
  pause, the one its menus use, with a reason of its own ("ApocryphaMenuFramework"), and releases it when the menu
  closes. World time, actors and weather stop behind the menu.
  - It is found in the running game like the mod-settings code: the game object and CGame's Pause / Unpause. Each must
    check out, or the setting stays hidden and the log says why.
  - The call is made on the game thread and guarded.

### Fixed
- The controller's Y menu (right-click menu) on a Menu-list row or separator opens beside the highlighted row, not
  where the mouse was last left.
- Dragging the window no longer writes a "widened to ..." log line every frame; once per press.

## 1.0.4 - 2026-10-05 - working

### Fixed (from the 1.0.3 test runs)
- **No more marks along the screen's edges while the window is dragged.** The Witcher 3 never repaints a thin band
  round its picture. A corner or edge drag drew the resize grip and border there for the frames before the edge clamp
  caught up, and the mouse cursor drew wherever it was.
  - Every draw AMF makes is now cut to the area inside the band (the window, pop-ups, the cursor, the on-screen
    keyboard, and other mods' windows).
  - A resize is held inside it on the frame it happens.
  - The on-screen keyboard sits above the band.
- **A mod page's controller frame takes in the whole row**: the setting's name and its control together. Before, it
  covered only the control.
- **One Escape (or B) closes the rename box**, even while you are typing; the edit is discarded. Enter / A still
  confirms.
- **Start closes the menu on the first controller press after using the keyboard.** Before, that first press only
  switched to controller mode.
- While the menu is open, raw keyboard/mouse input AMF cannot read is kept from the game rather than passed to it.
- The Mod menus tab's status line clears after a sort or undo run another way (DevBench).
- DevBench `amf.menu op=state` names the open Settings, Controls and Help tab in "page".

## 1.0.3 - 2026-10-05 - working

The Skyrim AMF's newer features, brought to the Witcher 3 build (the owner, 2026-10-05: "just start building it and
testing it").

### Added
- **An entry in the game's own menu opens the framework.** "Apocrypha Menu Framework" sits just above Options on the
  title screen and in the pause menu. A controller player reaches AMF there, the way a Skyrim player does from the
  journal (the owner: "Controller shouldn't have a button for it it should just be on the menu ... just like it is in
  Skyrim").
  - A small script, `Mods\modApocryphaMenuFramework`, adds it using the game's script annotations only. No game script
    is replaced, so nothing needs merging in Script Merger.
  - Picking the entry sets a hidden setting (`bin\config\r4game\user_config_matrix\pc\ApocryphaMenuFramework.xml`). AMF
    reads that setting through the game's own config code, opens over the game's menu, and clears it. Closing AMF goes
    back to the game's menu.
- **Your settings survive updates** (Skyrim 2.0.3/2.0.5).
  - Everything you set lives in `bin\x64_dx12\AMF\User.ini`: renames, order, favourites, separators, keys, theme, text
    size and the window. The download never contains that file, so an update can't replace it. Under Mod Organizer 2 it
    is written to overwrite.
  - `ApocryphaMenuFramework.ini` beside it holds only the defaults, and User.ini holds only what differs from them.
  - Settings saved by an earlier build into the defaults file are moved into User.ini on first start. The defaults file
    is then restored, and the earlier copy kept as `ApocryphaMenuFramework.ini.migrated`.
- **Layout presets: save, load and delete** (Skyrim 2.0.3). Under Menu list, save the list's order, separators,
  favourites and names under a name, load it back, or delete it. Presets live in `bin\x64_dx12\AMF\Presets\`.
- **A see-through window** (Skyrim 2.1.1): a See-through switch and a Window opacity slider (5-100 %, one step per
  press). Backgrounds fade the most, boxes and borders less, text least, and right-click menus stay solid.
- **Separators show in your language** (Skyrim 2.1.1): a separator still called "New separator" follows the menu's
  language, and a name you typed stays as you typed it.
- **Choose which mods' menus are listed, and sort them into categories** (Skyrim 2.1.0's MCM choice and sort, for the
  game's Options > Mods menus). It's on a new **Mod menus** settings tab:
  - one switch per mod, All on / All off, a name filter, and "List new mods' menus as they appear". A mod switched off
    stays in the game's own menu;
  - **Sort into categories** puts each listed mod that isn't already under a separator under one for its kind
    (Interface, Combat, Gameplay, Quests and Places, ...), judged by its name. Witcher words decide first, then the same
    name rules as the Skyrim sort;
  - **Undo the sort** restores the list from before it;
  - a mod you move by hand under another category is remembered for the next sort.
  - The choices and the learned placements are kept in `ModMenusImport.txt` and `ModMenusSortLearned.txt` beside
    User.ini, so they survive updates.
  - DevBench: `amf.process op=modsort`.
- **Move the window by its top row, and resize it freely** (Skyrim 2.1.1). Its place is remembered. Settings
  `bMovable` and `bFreeResize` turn each off.
- **Framework Settings in tabs**: General, Appearance, Mod menus and Menu list (Skyrim 2.1.0). The bumpers and Page Up /
  Page Down walk them.
- **Other mods' windows**, from Skyrim 2.0.4-2.0.8:
  - a mod's own window gets the keyboard and mouse while AMF's menu is closed;
  - Font Awesome icon faces ship in `AMF\icons`;
  - collapsing headers in a mod's window line up with its other rows;
  - the menu follows the image the game draws.

### Fixed
- **One menu key.** The Settings page's Rebind changed the label but not the key that opens the menu, and `uToggleKey`
  in the INI was ignored. Now there is one key (Skyrim 2.0.5/2.1.1): Controls > Open and close the menu, the same value
  as `uToggleKey`, with the same clash check as every other binding. The Settings page row is gone, the Help text points
  to Controls, and `SMF_GetReservedKeyCodes` reports the live key to other mods.
- **A mod passing an empty text-box name can no longer crash the game.** The null guard in four text-input exports sat
  after the `return` and never ran.
- Mod pages read the game's own slider format (`SLIDER:min:max:steps`) as well as the mods' (`SLIDER;min;max;steps`).
- **A rebind could take the click on the Rebind button as the new key.** After a refused key (one another function
  already uses) the capture stayed armed, so the next click, on Rebind itself, became the menu key ("mouse 1"), and the
  menu lost its key (first 1.0.3 test run). A capture never takes the left mouse button now, and the menu key takes no
  mouse button at all: it is a keyboard key.
- **Text that fits** (Skyrim 2.1.1):
  - the Controls page's names and notes wrap instead of running off the window;
  - the Theme, Font and Language lists have no empty band at the top;
  - the rename box has no empty title strip;
  - the Menu list numbers only the rows shown.
- **The controller's highlight shows on switches.** A switch drew no highlight at all when the D-pad reached it; it now
  gets the same blue frame a slider does.
- The defaults file AMF restores is byte for byte the one in the download (CRLF), not only line for line.
- **"Sort into categories" closed the game** (second 1.0.3 test run). The tab held its lock while calling the sort,
  which takes the same lock; a std::mutex taken twice on one thread throws, and the throw out of the frame ended the
  game with nothing logged. The lock is now released first, and a failing sort is logged rather than fatal.
- **The controller's highlight starts on the page's first control.** Entering the page pane (and switching pages with
  the bumpers) used to score the first press from where the highlight was on the previous page: on Settings > General
  it did not move at all, and on a long mod page it jumped near the end. A mod page's row is now framed as one row,
  label and control together.
- **A slider clicked with the mouse takes A / Space / Enter** at once, as after a D-pad move.
- **No marks along the screen's edges.** The Witcher 3 never repaints a thin band round its picture, so frame corners
  drawn there after a resize stayed on screen even with the menu closed. The window, its saved place and every popup
  now keep 2.5 % of the screen height inside each edge. Marks already on screen go at the next game start.
- **Escape with a list open closes the list, not the menu.** Escape and B also close the rename box.
- The start-up log no longer warns that the game-menu entry's setting is missing while the game is still loading it.
  It retries quietly and warns only if the setting is still missing after two minutes.
- DevBench `amf.menu op=state`'s displayOrder leaves out a mod whose menus are switched off, as the side list does.

### Removed
- The startup black curtain (its settings, INI section and help). The Witcher 3 has no modlist-scale start-up lag for it
  to hide (the owner, 2026-10-05).

## 1.0.2 - 2026-10-05 - working

### Added
- **The mod pages are editable.** A toggle, slider or choice changed in AMF is set through the game's own mod-settings code,
  the same code Options > Mods uses:
  - each mod sees the change the way it would from the game's menu;
  - the settings are saved to `dx12user.settings` a moment after the last change.
  The values shown are read live from the game.
  - Nothing is found at a fixed address. The functions are found at start-up by name and byte pattern, and each must
    start a function in the exe's own unwind table.
  - If anything is not found, or a call ever faults, the pages fall back to read-only and the log says why. A future
    patch can't make AMF crash the game here.
  - Engine calls run only on the game's main thread.
  - `amf.process op=modmenus` now reports the bridge too (`engine`: found, faulted, and how many reads, writes and saves).
  - `tools\red3_test.cpp` runs the same search over witcher3.exe outside the game, with none of its code running.

### Changed
- **The framework's own text follows the game's text language** (TextLanguage in the game's settings). A language AMF has
  no file for falls back to the Windows display language, then English. `sLanguage` still overrides it.
- **Skyrim and Oblivion wording removed** from the help, the Controls page, the Theme and Language help, and the INI
  comments, in all 11 languages. Paths now name `bin\x64_dx12\AMF` and `Documents\The Witcher 3`. Steam's F12 is described
  as catching the menu.
- The System-menu row setting is hidden: The Witcher 3 has no journal row.
- An English-only persistence test panel is no longer shown on the Settings page.

### Fixed
- **The pad gate covers every path, not only the exe's import slot.** A test run showed the game's menu moving with AMF
  open. The caller log added for it proved the game reads the pad only through its import slot, which AMF already gated:
  the movement came from the test tool's virtual pad, which connected afresh on each press (fixed in TestBench). The
  wider gate stays as cover for a second controller or another reader:
  - XInputGetState is gated at the function itself in every XInput DLL the game has loaded, as well as in the import
    slot;
  - the undocumented XInputGetStateEx (ordinal 100, the Guide button) is gated too;
  - all four controller slots are neutralised, not only slot 0;
  - AMF's own reads pass every gate untouched;
  - while the menu is open, the log names each distinct caller that reads the pad (module and offset, slot, thread).
- **Choice lists.**
  - A list the mod never set reads "not set yet", not "-1".
  - Opening a list puts the highlight on the current choice. Before, it went to the first, so Up wrapped to the last.
  - The list no longer has an empty row above its first option. Its padding no longer comes from the framed window's.
- **Mod page labels are no longer cut off.** Each setting is now a row, with its label wrapped on the left and the
  control filling the right. Before, the label sat right of a slider or list and ran off the window ("Environmental
  Setting : The ...").

## 1.0.1 - 2026-10-05 - untested

### Added
- **The Witcher 3 mods' own settings menus are AMF pages** (the owner, 2026-10-05: *"make AMF read the config menus to
  make AMF pages so I can see them"*).
  - Every mod menu the game reads from `bin\config\r4game\user_config_matrix\pc` becomes a page, including menus that
    Mod Organizer 2 supplies.
  - Each mod gets one AMF entry, and each of its menu pages is a tab.
  - Labels come from the mods' own string files, in English. Where a mod ships none, the setting's id is made readable
    instead.
  - Toggles, sliders and choice lists draw as AMF controls, and section headings and dividers are kept.
  - Values are read from `Documents\The Witcher 3\dx12user.settings` and read again when the game saves them.
  - **Read-only for now.** Settings are still changed in the game's Options > Mods. Changing them in AMF needs the
    engine layer that a later update adds.
- `amf.process op=modmenus` reports what was read: menu files, pages, how many labels came from a string file, and which
  settings files exist.
- `tools\modmenus_test.cpp` runs the same reader over a Mod Organizer 2 mods folder outside the game.

### Fixed
- The window no longer opens filling the whole screen. With the game minimised or in the background, the screen size
  reads as 0, and the window's size was saved as a fraction of it (`inf`). A size like that is now never saved, and one
  already saved counts as unset.
- AMF's own screenshot is named "The Witcher 3 ...", not "Oblivion Remastered ...".

## 1.0.0 - 2026-10-05 - working

The first Witcher 3 build: the Oblivion Remastered framework carried to Witcher 3 5.0 (DX12 only).

### Added
- **Its own loader, `dinput8.dll` (AMFLoader).** Witcher 3 5.0 has no script extender. The game imports `dinput8.dll`,
  so the loader takes that name, forwards to the system DirectInput and loads every `.asi` beside it at the game's entry
  point. It must be a real file in `bin\x64_dx12`: Mod Organizer 2's virtual folder does not supply it early enough
  (first run, 2026-10-05). The `.asi` itself is found through Mod Organizer 2 as normal.
- **Oathvein is the default theme** (the owner, 2026-10-05).
- **Steam's F12 screenshot shows the AMF window** (the owner, 2026-10-05: no repeat of Oblivion, where the capture
  missed the overlay).
  - The game does not call dxgi directly. Its swap chain comes from NVIDIA Streamline (`sl.interposer.dll`).
  - AMF now draws when the game calls Present on that swap chain, before the frame reaches dxgi and before Steam's hook
    there copies it.
  - The log names the module each Present lands in, so it shows which path the game took.
  - The dxgi-level hook still captures the command queue and handles resizing. It draws only if the earlier path never
    fires, so a frame is never drawn twice.

### Fixed (first in-game run, 2026-10-05)
- Themes, translations, fonts, settings and the window layout are found beside the `.asi` (`bin\x64_dx12\AMF`). They
  were looked for under the game's working folder, which is `bin\`, so Oathvein was not found.
- The frame tick hooks `PeekMessageA`, the message call the game really uses.

# History of the Oblivion Remastered build this was carried from

## 1.0.6 - 2026-10-02 - untested

### Changed
- **No title bar; the frame runs round all four sides** (the owner, 2026-10-02: *"do the same thing that AMF for Skyrim
  did by removing the top bar and extending, or rather connecting, the frame on all four sides"*). The collapse arrow is
  gone and the window no longer moves: without a title bar ImGui would let any empty part of the window drag it. Each
  way in keeps its own place. The key-opened window opens where it was last left; the System-row window opens on the
  right of the screen so the System page's rows stay in view. Both still resize from their edges.
- **Oblivion, this framework's own look, is the new default theme** (the owner: *"give it a frame art similar to how
  Skyrim has a frame art, except this frame will be more like an embroidered map's edge. Something a bit decorative in
  a gold or brown color"*).
  - The frame: an embroidered map's edge - a gold couched cord, a compass star in a stitched ring at each corner, and a
    brown running stitch.
  - The colours: parchment with brown ink.
  - The art is original, drawn from shapes by `tools/make_mapedge_frame.py`, so no game file ships.
  - The edges tile rather than stretch, so the stitches keep their size.
  - The frame scales with the UI: 26 px at 1080p, 43 px at 1800 px tall.
  - Skyrim's knotwork stays as the "Skyrim" theme, and every other theme is unchanged.
  - First built as "Cyrodiil Map", then renamed (the owner: *"just rename the current theme to Oblivion, and we can get
    rid of the old Oblivion paper theme"*). A saved `cyrodiil` or `oblivion-paper` theme id now opens as `oblivion`.

### Added
- **A Screenshot control that catches this menu: F11 on the keyboard, View on the controller** (the owner, 2026-10-02:
  *"I'd rather just have a hotkey that takes the same screenshot that yours does"*, then *"make it a rebindable option
  ... it can be something that only works with AMF open because regular Steam screenshot works in the game itself"*).
  - Steam's F12 copies the frame before this framework draws, so it shows the game without the menu.
  - This control copies what the screen shows, menu and all, and saves it as a PNG on a worker thread. A rising double
    beep says it was saved.
  - How and when the framework draws is unchanged.
  - It is one of the framework's own controls, rebindable on Controls with press-to-bind, and it works while the menu
    is open.
  - F11 rather than F12, since Steam would also save a picture without the menu.
  - View is shared only with the on-screen keyboard's Done; while that keyboard is up, the press goes to Done.
  - The pictures go to the game's `Data\AMF Screenshots`, which Mod Organizer 2 puts in its overwrite folder (the owner:
    *"have it kicked out through MO2 into the overwrite"*). `[Screenshot] sFolder` names another folder.
  - Two new strings in all eleven languages.

### Added - the Skyrim framework's 2.0 (the owner: *"update the Oblivion AMF to the most up-to-date version in line with
AMF for Skyrim. So it has the separators and all that"*)
- **Separators in the mod list**, like MO2's.
  - Y (or a right-click) on a mod gives *New separator above* and *Send to* (a separator, or No separator).
  - A separator folds and unfolds with A or a click, and shows a count when folded.
  - Separators can be renamed, deleted and favourited.
  - A-Z sorts the loose mods only.
- **Move to the top** goes to the top of the mod's own separator.
- **Reorder** in a mod's options opens a small up / down box. Each press moves the mod one place, and the box stays open.
- **Grab and move**: R3 picks up the highlighted mod and the right stick steps it up or down; R3 again, or B, puts it
  down. All three controls are rebindable on Controls. R3 is the target lock only in gameplay; the game takes no input
  while this menu is open (the owner: *"Obviously, you're not going to target lock while in the menu"*).
- **The side pane fits its names**, and the window widens once when the names and the page do not both fit. The
  System-row window widens leftwards, since it sits against the right edge.
- 18 new strings in all eleven languages, taken from the Skyrim framework's translations.
- amf.menu ops `separator` and `stick` for headless tests. The DevBench argument reader now matches keys only: a value
  equal to a key name had been read as that key.

### Not taken from Skyrim 2.0
- **One centred window for both ways in.** Oblivion keeps its System-row placement on the right.
- **The start-up curtain's splash shapes (2.0.2).** Oblivion's curtain is plain black, with no modlist splash to fit.

## 1.0.5 - 2026-09-30 - untested

### Added
- **The controller triggers reach the menus** (the owner, 2026-09-30: *"mirror the more up-to-date AMF for Skyrim with
  better controller navigation ... and any other convenient controller navigation that's in the Skyrim AMF"*). The
  Skyrim framework's 1.9.9 fix, for Oblivion Remastered's pad: XInput reports L2 and R2 as analog values, not buttons, so
  each now becomes a press past XInput's own threshold and arrives as ImGui's GamepadL2 / GamepadR2 - a mod's page can
  use them. While a mod's Rebind button is waiting, the triggers stay with that capture as before.
- **AMF_IsMenuOpen** (AMF::IsMenuOpen in sdk/include/AMF.h): true while the framework's window is up. A mod that
  reads the controller itself - Improved Wheel Menu reads the D-pad before the pad gate empties the game's reads - stands
  down on it, so the D-pad no longer opens its wheel while the menu is open.

### Fixed
- **A crash when the game rebuilt its menus** (the owner, 2026-09-30: Oblivion crashed equipping the second loadout of
  Simple Loadout System). The System row kept the System page, its own row and the row above it as raw pointers and
  asked each tick whether they were still alive by reading the object's own slot index - from memory the garbage
  collector had already freed (access violation in reflect::IsLive, called from systemrow::Tick). They are now kept as
  handles (the object array's slot, class and name recorded while live; the slot is asked first and the object read
  only while the array still holds it - HUD Position Manager's handle, logic library 8032), the pause code's camera
  manager and controller likewise, and IsLive itself reads the slot index under a fault guard.
- Every engine call that takes a world context (the System row's widget Create, the pause calls) runs fault-guarded
  and refuses a context that is being destroyed. Minimap Menu crashed on quitting to the menu when its world context
  outlived its world (2026-09-30); gate rule or-world-context-calls-are-guarded now refuses the pattern in every package.

### Checked against the Skyrim framework
- The rest of the Skyrim framework's controller navigation was already in this port (it was taken from Skyrim 1.9.8):
  L1 / R1 walk the tabs - a mod's own tab bar when its page declares one (AMF::DeclareInnerTabs), otherwise the
  framework's page bar - Y opens a mod's options, L3 favourites the highlighted mod, the D-pad never steps a tab, the
  on-screen keyboard, the thumbstick API. The bumpers reach a mod's inner tabs only when that mod declares them:
  HUD Position Manager does from its next build.

## 1.0.4 - 2026-09-29 - untested (the System row proven in game 12:5x; this build with the pause row hidden not yet run)

### Added
- The idle vanity camera never takes over while the window is open ([Menu] bKeepCameraAwake, on): the camera manager's own idle timer is stopped while the window is up and restarted when it closes, the way the game does after its pause menu, and a vanity camera already running is left. It used to rotate the view and hide the HUD under a HUD mod's page. A stopgap (the owner, 2026-09-29: "once AMF can properly stop time, there won't be a need for it to interact with the vanity camera") - to be removed when the pause works.
- A row in the game's own System page: "Apocrypha Menu Framework", under Save, Load and Quit, reached with the D-pad like the game's rows and opening the framework when pressed. The row is created in the live page from the same widget class as the game's rows (its look and sound are the game's), added to the page's panel with the last row's layout, and spliced into the rows' controller navigation; nothing bound on the page is touched and no game file is replaced, so it works with any menu artwork. The "Mod settings in the game's System menu" switch on the Settings page turns it off (Menus.bSystemMenuRow; a change takes effect at the next launch). Opened from the row, the window sits on the right of the screen so the page's rows stay in view; drag it and the position is remembered for that way in, as before.
- A per-frame game-thread tick (the message pump's PeekMessageW import, chained) and the reflection helpers the row needs (ProcessEvent watch by class through a vtable-slot swap, property offsets by name), brought over from Tween Menu for Oblivion Remastered.

### Not in this version
- Pausing the game while the menu is open. It was wired through the engine's GameplayStatics::SetGamePaused (Pause.cpp) and the call went through, but the world kept running; the owner had it left out of 1.0.4 ("finalize AMF in its current form without the time stop feature for now because it doesn't currently work"). [Menu] bPauseGame is still read and kept, and does nothing.

## 1.0.3 - 2026-09-29 - working

### Fixed
- Closing the menu now cancels a mod's bind-button capture that is still waiting. Before, a keyboard-side capture left armed by closing the menu from the controller took the next key or click in the game (within the mod's timeout, 8 s for Ultimate Combat Redux) as the binding and hid that press from the game. The mod's next poll reports cancelled, so its Rebind button simply comes back.

## 1.0.2 - 2026-09-29 - working

### Added
- Key capture for other mods' bind buttons: AMF_BeginKeyCapture, AMF_PollKeyCapture and AMF_CancelKeyCapture (in sdk/include/AMF.h as AMF::BeginKeyCapture, PollKeyCapture, CancelKeyCapture and HasKeyCapture). A mod's Rebind button arms the capture and the next press becomes the binding - any key, mouse button, mouse wheel, controller button, trigger or stick direction. The press is swallowed: the menu does not navigate on it and the game never sees it, so B and A can be bound on a controller without backing out of the page. Esc cancels a keyboard capture; a timeout ends either side. Ultimate Combat Redux's Rebind buttons use it.

### Fixed
- The DLL no longer carries the build machine's folder paths (rule 45): /d1trimfile strips the project folder from __FILE__ and std::source_location, including CommonLibOB64's OBSE/Interfaces.h reached through the precompiled header, and /PDBALTPATH records only the PDB's file name. 1.0.1 carried both paths.

## 1.0.1 - 2026-09-26

First release. (0.1.0 was the first working build of the night; the owner set the release number to 1.0.1.)

The Skyrim Apocrypha Menu Framework brought to The Elder Scrolls IV: Oblivion Remastered as an OBSE64 plugin.

Added
- The framework window over the game's DirectX 12 renderer: the DXGI factory is hooked before the game's WinMain, the
  swap chain and its presenting command queue are taken as the game creates them, and Dear ImGui 1.90.8 (docking)
  draws on every Present.
- The Skyrim core, unchanged where the game did not force a change: the Mod Control Panel (side list, content pane,
  tabs), the six themes and theme files with their art, the knotwork frame, the TrueType font atlas, the Controls page
  with rebinding, the Help pages, the on-screen keyboard, personalization (aliases, order, favourites), per-save state,
  the eleven translations, the hang watchdog and fast exit.
- Input from the game window's messages (keyboard, mouse) and a per-frame XInput read (controller), feeding the same
  decision and record queue the Skyrim engine hook fed; the game sees none of it while the menu is open, and a key
  held across the opening still gets its release.
- The pad gate: the game's own XInput reads are routed through the framework and answered with an empty pad while
  the menu is open (and until every button is up after it closes), so A on a menu entry cannot also fire in the game.
- The public API: `sdk/include/AMF.h`, a header-only, dependency-free way for other mods to add pages - safe without
  the framework, C++ Dear ImGui through the shared context (`AMF_GetImGuiContext`, `AMF_GetImGuiAllocatorFunctions`,
  `AMF_CheckImGuiABI`) or the cimgui `ig*` exports - and `sdk/example`, a complete mod built against it.
- The C API and the complete cimgui 1.90.8dock export surface, so a mod written for the Skyrim framework registers
  and draws the same way.
- The driving tools `amf.menu`, `amf.process` and `amf.keybind`, registered with TestBench when it is present.
- Settings, themes, fonts and translations live beside the plugin under `OBSE\Plugins\ApocryphaMenuFramework`; the log
  is `Documents\My Games\Oblivion Remastered\OBSE\Logs\ApocryphaMenuFramework.log`, at info by default.

Not yet (see PLAN.md, M3)
- Opening from the game's pause menu; pausing the game while the menu is open; a controller button that opens the
  menu; the game's HUD opacity; the startup curtain. Their settings rows are not drawn until each is wired.
- In-process screen capture (the driving tool's `capture` op) on D3D12.

Fixed during the night's testing
- The controller was dead in the game for the whole session: the framework loaded xinput1_4.dll at plugin load, before
  the game and Steam had set up their controller path. XInput is now resolved when the menu first opens, through the
  game's own import.
- Theme art (frame, background) did not load: the path still had Skyrim's `Data\` root. Two gate rules now refuse a
  Skyrim root in an Oblivion repository's sources and shipped text.
- The first D-pad press after opening only selected the list pane; navigation now starts on the open entry, and the
  press that switches to controller mode counts.
- A second mouse pointer over the menu (Unreal re-setting its arrow) and the menu's cursor jumping from the centre
  on the first movement.
