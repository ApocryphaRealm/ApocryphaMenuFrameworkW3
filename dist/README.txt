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
  * Mouse, keyboard and controller. The menu follows whatever you last used; there is nothing to switch on. While it
    is open, the game doesn't see your keys, mouse or controller.

REQUIREMENTS
------------
  * The Witcher 3: Wild Hunt - Remastered, patch 5.0 or later, running in DirectX 12 (the only renderer since 5.0).
  * An ASI loader. AMF ships its own (Root\bin\x64_dx12\dinput8.dll); the Ultimate ASI Loader works as well.
  * With Mod Organizer 2: Mod Organizer 2 manages the install (the framework's own settings land in its overwrite
    folder), and Root Builder puts the loader, dinput8.dll, into the real game folder at launch and takes it out again
    afterwards. Windows loads dinput8.dll before Mod Organizer 2's virtual folder is in place, so without Root Builder
    that one file would never be found.

INSTALLING
----------
  * Mod Organizer 2 with Root Builder: install the download as a normal mod. Root Builder copies the Root folder's
    bin\x64_dx12\dinput8.dll into the game folder when the game starts and removes it when it closes; everything else
    reaches the game through Mod Organizer 2 as usual.
  * Vortex or by hand: copy the bin and Mods folders into the game folder, and ALSO the contents of the Root folder (its
    bin folder), so that bin\x64_dx12 holds dinput8.dll, ApocryphaMenuFramework.asi and the AMF folder, and Mods holds
    modApocryphaMenuFramework.
  * If you use the Ultimate ASI Loader already, keep it and leave AMF's dinput8.dll (the Root folder) out.
  * The game-menu entry and the intro switch use a small script (Mods\modApocryphaMenuFramework) written with the
    game's script annotations: no game script is replaced, so there is nothing to merge in Script Merger.
  * The log is written to Documents\The Witcher 3\AMF\ApocryphaMenuFramework.log.

FILES
-----
  Root\bin\x64_dx12\dinput8.dll              the ASI loader (forwards to Windows' own dinput8.dll); Root Builder puts it in
                                             the game folder, or copy it there yourself
  bin\x64_dx12\ApocryphaMenuFramework.asi    the framework
  bin\x64_dx12\ApocryphaMenuFramework.pdb    debug symbols, for crash reports
  bin\x64_dx12\AMF\                          the default settings (ApocryphaMenuFramework.ini), themes, fonts, translations
  bin\x64_dx12\AMF\User.ini, AMF\Presets\    YOUR settings and saved menu-list layouts, written by the game the first
                                             time you change something; never in the download, so an update keeps them
                                             (under Mod Organizer 2 they are in overwrite)
  bin\config\r4game\user_config_matrix\pc\ApocryphaMenuFramework.xml
                                             two hidden settings the script and the framework share (open request,
                                             skip intro); not shown in any menu
  Mods\modApocryphaMenuFramework\            the script for the game-menu entry and the intro switch

LICENCE
-------
GPL-3.0-or-later (LICENSE, NOTICE.md). Third-party components and their notices: THIRD_PARTY_NOTICES.md.
