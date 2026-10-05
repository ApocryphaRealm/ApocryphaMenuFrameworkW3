Apocrypha Menu Framework - The Witcher 3: Wild Hunt
====================================================
Version 1.0.1

An in-game menu framework (embedding Dear ImGui) for The Witcher 3: Wild Hunt - Complete Edition, patch 5.0 or later
(DirectX 12). It is the same framework as Apocrypha Menu Framework for Skyrim and for Oblivion Remastered: the same
menu, themes and controls, drawn over The Witcher 3's DirectX 12 renderer.

WHAT YOU GET
------------
  * F1 opens the menu in a window of its own; F1 or Escape closes it. The key can be rebound on the Controls page.
  * Your mods' own settings menus as AMF pages. Every mod that adds a menu to the game's Options > Mods gets an entry
    of its own, with each of its menu pages as a tab, labelled in the mod's own words. In this version the pages show
    the settings and their current values; change them in the game's Options > Mods for now. Changing them from AMF
    comes in a later update.
  * Themes - Oathvein (the default: grey lines, charcoal and blood red), Untarnished (clean lines) and more - plus a
    font picker (drop a .ttf into bin\x64_dx12\AMF\fonts) and a text-size slider.
  * Steam's own screenshot key (F12) catches the menu.
  * Mouse, keyboard and controller. The menu follows whatever you last used; there is nothing to switch on.

REQUIREMENTS
------------
  * The Witcher 3: Wild Hunt, patch 5.0 or later, running in DirectX 12 (the only renderer since 5.0).
  * An ASI loader. AMF ships its own (dinput8.dll); the Ultimate ASI Loader works as well.

INSTALLING
----------
  * Copy the bin folder into the game folder, so that bin\x64_dx12 holds dinput8.dll, ApocryphaMenuFramework.asi and
    the AMF folder.
  * Mod Organizer 2: install as a normal mod for everything EXCEPT dinput8.dll. That one file must be a real file in the
    game's bin\x64_dx12 folder: Windows loads it before Mod Organizer 2's virtual folder is in place. (If you use the
    Ultimate ASI Loader already, keep it and leave AMF's dinput8.dll out.)
  * The log is written to Documents\The Witcher 3\AMF\ApocryphaMenuFramework.log.

FILES
-----
  bin\x64_dx12\dinput8.dll                   the ASI loader (forwards to Windows' own dinput8.dll)
  bin\x64_dx12\ApocryphaMenuFramework.asi    the framework
  bin\x64_dx12\ApocryphaMenuFramework.pdb    debug symbols, for crash reports
  bin\x64_dx12\AMF\                          settings (ApocryphaMenuFramework.ini), themes, fonts, translations

LICENCE
-------
GPL-3.0-or-later (LICENSE, NOTICE.md). Third-party components and their notices: THIRD_PARTY_NOTICES.md.
