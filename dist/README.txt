Apocrypha Menu Framework - The Witcher 3: Wild Hunt - Remastered
================================================================
Version 1.0.0

One in-game settings menu for every mod, for The Witcher 3: Wild Hunt - Remastered (patch 5.0 or later, DirectX 12).
It is the same framework as the Apocrypha Menu Framework for Skyrim and for Oblivion Remastered: the same menu, themes
and controls, drawn over The Witcher 3's DirectX 12 renderer.

WHAT YOU GET
------------
  * Your mods' own settings menus as AMF pages. Every mod that adds a menu to the game's Options > Mods gets an entry
    of its own, with each of its menu pages as a tab, labelled in the mod's own words. A change made here goes through
    the game's own settings code and is saved the way Options > Mods saves it, so the mod sees it exactly as it would
    from the game's menu. (If a future game patch moves that code, the pages show the values read-only and the log
    says why - nothing breaks.)
  * Two ways in: F1 (rebind it on the Controls page), or the "Apocrypha Menu Framework" entry in the game's own menu,
    just above Settings on the title screen and in the pause menu - the way in with a controller. F1, Escape, B or
    Start closes it.
  * The menu list: rename, reorder, favourite, separators you can name and fold, and layout presets you can save,
    load and delete.
  * Settings > Mod menus: choose which mods are listed, and sort them into categories with one button (and undo it).
    The Mods row has the same Sort button, a tick box for alphabetical order and an A-Z / Z-A switch.
  * The window sits beside the game menu's black column by default, and returns there whenever the title screen or
    pause menu is open ("Sit beside the game's menu column", on by default). The highlighted row gets the frame the
    game draws round its own menu entries.
  * Pause the game while the menu is open (off by default).
  * Skip the intro videos (off by default): the game goes straight to its main menu from the next start, without the
    disclaimer, legal and logo videos or the story recap.
  * Skip the story recap on loading screens (off by default).
  * Seven themes - Skellige (the default, on the black of the game's menu), Norden, Norden - Black, Vel'dun and Untarnished, plus the Skyrim and
    Oblivion looks of the framework's other builds - and a font picker (drop a .ttf into
    bin\x64_dx12\AMF\fonts), a text-size slider, a see-through window and eleven languages.
  * Steam's own screenshot key (F12) catches the menu.
  * Works beside ReShade and other graphics injectors: AMF finds the picture the game really presents, whatever sits
    between the game and DirectX, and its log names what it found.
  * AMF does nothing while the game is loading: the menu does not open and AMF's keys go to the game until the
    loading screen is gone.
  * Japanese and Chinese text wraps between characters, and every Chinese, Japanese and Korean character has a glyph
    with any font.
  * The mouse wheel moves a slider one step; every control has a visible outline in every theme.
  * Mouse, keyboard and controller. The menu follows whatever you last used; there is nothing to switch on. While it
    is open, the game doesn't see your keys, mouse or controller.

REQUIREMENTS
------------
  * The Witcher 3: Wild Hunt - Remastered, patch 5.0 or later, running in DirectX 12 (the only renderer since 5.0).
  * An ASI loader. AMF ships its own (dinput8.dll); the Ultimate ASI Loader and TW3SE (Witcher 3 Script Extender) work
    as well.
  * The loader has to be a real file in the game's bin\x64_dx12 - Windows loads it before anything else. With Vortex,
    or installing by hand, that is where it goes. With Mod Organizer 2 it needs Root Builder: Mod Organizer 2's virtual
    folder is not in place yet when Windows loads dinput8.dll, so Root Builder copies it into the real game folder at
    launch and takes it out again afterwards.

INSTALLING
----------
  The download is an installer: it asks one question - where the ASI loader should go - and installs the rest as is.
  * Vortex: install the download; choose "Vortex, or installing by hand". The loader goes to bin\x64_dx12.
  * Mod Organizer 2 with Root Builder: install the download as a mod; choose "Mod Organizer 2 with Root Builder". Root
    Builder copies the mod's Root\bin\x64_dx12\dinput8.dll into the game folder when the game starts and removes it when
    it closes; everything else reaches the game through Mod Organizer 2 as usual.
  * Already using the Ultimate ASI Loader (or another dinput8.dll ASI loader): choose "I already have an ASI loader" and
    keep yours - it loads ApocryphaMenuFramework.asi the same way.
  * Using TW3SE (Witcher 3 Script Extender)? Download the "No dinput8 (for TW3SE)" file instead of the main file (or
    delete AMF's dinput8.dll). It holds everything except AMF's ASI loader, laid out like the game folder: copy its bin
    and Mods folders into the game folder. TW3SE starts through its own dinput8.dll and loads
    ApocryphaMenuFramework.asi itself; AMF's dinput8.dll would replace TW3SE's, and TW3SE would stop loading without a
    word.
  * By hand: copy the bin and Mods folders into the game folder, and loader\dinput8.dll into bin\x64_dx12, so that
    bin\x64_dx12 holds dinput8.dll, ApocryphaMenuFramework.asi and the AMF folder, and Mods holds
    modApocryphaMenuFramework. (The fomod folder is the installer's own; it is not copied.)
  * The game-menu entry and the intro switch use a small script (Mods\modApocryphaMenuFramework) written with the
    game's script annotations, and every game method it touches is wrapped (@wrapMethod), never replaced: no game script
    file is replaced, nothing needs merging in Script Merger, and it combines with other mods that wrap or replace the
    same methods.
  * The log is written to Documents\The Witcher 3\AMF\ApocryphaMenuFramework.log.

LINUX (WINE / PROTON)
---------------------
  * AMF finds out by itself that it runs under Wine or Proton (from 1.0.7) and draws the menu the way that works there.
    The log's first lines say so: "platform: running under Wine ..." and "uDrawPath auto -> 1".
  * With the Amethyst mod manager, use the "No Installer" download (Amethyst does not run the installer), and put AMF at
    the TOP of the load order: other mods can ship their own dinput8.dll, and AMF's has to be the one that is used.
  * If the menu opens (the game stops taking your keys) but nothing shows, set it by hand: in User.ini beside
    bin\x64_dx12\AMF\ApocryphaMenuFramework.ini, add the two lines [Display] and uDrawPath=1 (or change uDrawPath=auto
    to uDrawPath=1 in ApocryphaMenuFramework.ini itself). uDrawPath=0 in User.ini forces the Windows path instead.
  * Players have seen the very first launch fail and every launch after it work. Proton builds its shader caches on
    that first run, so the first start can take a long time; AMF's hang watchdog now stays off under Wine so that a
    long first start is not taken for a hang. If a first launch still fails, start the game again.

FILES
-----
  loader\dinput8.dll                         the ASI loader (forwards to Windows' own dinput8.dll); the installer puts it in
                                             bin\x64_dx12 (Vortex) or Root\bin\x64_dx12 (Mod Organizer 2 + Root Builder)
  fomod\                                     the installer
  bin\x64_dx12\ApocryphaMenuFramework.asi    the framework
  bin\x64_dx12\ApocryphaMenuFramework.pdb    debug symbols, for crash reports
  bin\x64_dx12\AMF\                          the default settings (ApocryphaMenuFramework.ini), themes, fonts, translations
  bin\x64_dx12\AMF\User.ini, AMF\Presets\    YOUR settings and saved menu-list layouts, written by the game the first
                                             time you change something; never in the download, so an update keeps them
                                             (under Mod Organizer 2 they are in overwrite)
  bin\config\r4game\user_config_matrix\pc\ApocryphaMenuFramework.xml
                                             hidden settings the script and the framework share (open request,
                                             skip intro, save and load signals, loading screens); not shown in any menu
  Mods\modApocryphaMenuFramework\            the script for the game-menu entry and the intro switch

COMPATIBILITY
-------------
  * Other script mods: AMF's script only wraps game methods, so it starts beside mods that wrap or replace the same ones.
    Until 1.0.6 the "Here only" feature REPLACED the Options menu's showOptionsPanel, and a mod that replaces it too
    (Intuitive Menu, for one) stopped the game's scripts from starting ("@replaceMethod duplicate found for
    'CR4IngameMenu::showOptionsPanel'"). From 1.0.6 it is a wrap. If you set a mod to "here only" while another mod
    builds its own Options list, AMF sends the game's list without the hidden mods, so that mod's own changes to the
    list are not kept; with nothing set to "here only", AMF leaves the list exactly as the game (or that mod) made it.
  * The script compile window: the game prints hundreds of "Warning ... not marked as abstract" lines while it compiles
    the scripts. Those are the game's own and harmless. Only lines that say "Error" matter - an error names the file
    and line that stopped the compile.

CREDITS
-------
  * Level12Lobster - how to run AMF on Linux with the Amethyst mod manager (Wine / Proton), which 1.0.7 made automatic.
  * 美猫少佐 (Bineko Major) - diagnosed and patched uDrawPath=1 in 1.0.4, the path the menu is drawn through on Linux.

LICENCE
-------
GPL-3.0-or-later (LICENSE, NOTICE.md). Third-party components and their notices: THIRD_PARTY_NOTICES.md.
