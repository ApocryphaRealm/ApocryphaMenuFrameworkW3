// =====================================================================================================================
// Apocrypha Menu Framework - its entry in the game's own menu (the title screen and the pause menu).
//
// A controller player opens AMF from here, the way a Skyrim player opens it from the journal's System row (the owner,
// 2026-10-05: "it should just be on the menu or the in-game quick menu just like it is in Skyrim for controller").
//
// Annotations only: no game script FILE is replaced, so nothing needs merging in Script Merger and other mods that wrap the
// same functions keep working. One method is replaced (1.0.5): CR4IngameMenu.showOptionsPanel, for "here only" mod menus
// (at the end of this file) - another mod that replaces that same method would conflict with it.
//
// How it reaches AMF: picking the entry sets the hidden setting ApocryphaMenuFramework.OpenRequest (declared in
// bin\config\r4game\user_config_matrix\pc\ApocryphaMenuFramework.xml). AMF reads that setting through the game's own
// config code each frame, opens its window over this menu, and clears the setting again. The game's menu stays open
// behind it, so closing AMF returns the player to it.
// =====================================================================================================================

// An action type the game's menu has no case for: the game's own handler ignores it, and the wrapper below picks it up.
function AMF_OpenActionType() : int
{
	return 1077;
}

@wrapMethod(IngameMenuStructureCreator)
function PopulateMenuData() : CScriptedFlashArray
{
	var entries   : CScriptedFlashArray;
	var reordered : CScriptedFlashArray;
	var amf       : CScriptedFlashObject;
	var i         : int;
	var count     : int;
	var placed    : bool;

	entries = wrappedMethod();

	// 1.0.5: the framework's [Menus] bSystemMenuRow switch. The framework keeps ApocryphaMenuFramework.MenuEntry equal to it;
	// "false" leaves the entry out (a value never set reads "-1", which keeps it).
	if (theGame.GetInGameConfigWrapper().GetVarValue('ApocryphaMenuFramework', 'MenuEntry') == "false")
	{
		return entries;
	}

	amf = CreateMenuItem("amf_open", "panel_amf_open", NameToFlashUInt('ApocryphaMenuFramework'), AMF_OpenActionType(), true);
	// The framework's name, the same in every language (no string table entry needed for a proper name).
	amf.SetMemberFlashString("label", "Apocrypha Menu Framework");
	amf.SetMemberFlashString("listTitle", "Apocrypha Menu Framework");

	// Just above Options, where a player looks for settings; at the end if this menu has no Options entry.
	reordered = m_flashValueStorage.CreateTempFlashArray();
	count = entries.GetLength();
	for (i = 0; i < count; i += 1)
	{
		if (!placed && entries.GetElementFlashObject(i).GetMemberFlashString("id") == "mainmenu_options")
		{
			reordered.PushBackFlashObject(amf);
			placed = true;
		}
		reordered.PushBackFlashObject(entries.GetElementFlashObject(i));
	}
	if (!placed)
	{
		reordered.PushBackFlashObject(amf);
	}
	return reordered;
}

@wrapMethod(CR4IngameMenu)
function OnItemActivated(actionType : int, menuTag : int)
{
	if (actionType == AMF_OpenActionType() && !ignoreInput)
	{
		theGame.GetInGameConfigWrapper().SetVarValue('ApocryphaMenuFramework', 'OpenRequest', "true");
	}
	wrappedMethod(actionType, menuTag);
}

// SKIP INTRO (the owner, 2026-10-05: "instead of a black curtain, let's have an option for a toggle that turns off the
// intro to the game"). The framework keeps ApocryphaMenuFramework.SkipIntro equal to its [Menu] bSkipIntro switch; the game
// saved it with its settings, so it is read here before the framework is up.
function AMF_SkipIntro() : bool
{
	return theGame.GetInGameConfigWrapper().GetVarValue('ApocryphaMenuFramework', 'SkipIntro') == "true";
}

// The start-up videos (disclaimer, legal notice, logos): the game lists them here and closes the start-up menu at once
// when the list is empty.
@wrapMethod(CR4StartupMoviesMenu)
function SetupMoviesData()
{
	wrappedMethod();
	if (AMF_SkipIntro())
	{
		m_MovieData.Clear();
	}
}

// SKIP THE STORY RECAP ON LOADING SCREENS (the owner, 2026-10-05). The engine's [LoadingScreen/Debug] DisableVideos is not
// kept between sessions, so it is set here, when the main menu opens - after start-up and before any save can load -
// while the framework's switch (ApocryphaMenuFramework.SkipLoadingRecap) is on. With the switch off nothing is written,
// so a value set another way (an engine.ini tweak) stays as it is.
@wrapMethod(CR4CommonMainMenuBase)
function OnConfigUI()
{
	if (theGame.GetInGameConfigWrapper().GetVarValue('ApocryphaMenuFramework', 'SkipLoadingRecap') == "true")
	{
		theGame.GetInGameConfigWrapper().SetVarValue('ApocryphaMenuFramework', 'DisableVideos', "true");
	}
	wrappedMethod();
}

// The story recap the game plays once at every start (gamestart/recap_wip.usm, the owner: "not the storytelling movie at
// the very beginning"). Its menu reads its first video without checking the list, so it is not emptied: it closes before
// it is set up, as the start-up menu above closes when it has nothing to play.
@wrapMethod(CR4RecapMoviesMenu)
function OnConfigUI()
{
	// OnConfigUI is an event, so the wrapper returns a bool like it: a bare "return;" stopped the script compile (the
	// owner's 1.0.0 test: "Unable to convert from 'void' to 'Bool'").
	if (AMF_SkipIntro())
	{
		CloseMenu();
		return false;
	}
	wrappedMethod();
}

// THE GAME'S MENU IS OPEN (the owner, 2026-10-05: "snap AMF to the black main menu column and even the in-game column").
// The framework reads ApocryphaMenuFramework.GameMenuOpen and, with its "Sit beside the game's menu column" switch on,
// opens its window just right of this menu's column. The title screen and the pause menu are both this menu.
@wrapMethod(CR4IngameMenu)
function OnConfigUI()
{
	theGame.GetInGameConfigWrapper().SetVarValue('ApocryphaMenuFramework', 'GameMenuOpen', "true");
	wrappedMethod();
}

@wrapMethod(CR4IngameMenu)
function OnClosingMenu()
{
	theGame.GetInGameConfigWrapper().SetVarValue('ApocryphaMenuFramework', 'GameMenuOpen', "false");
	wrappedMethod();
}

// PER-SAVE SIGNALS (1.0.5, plan row 64). The framework keeps some values per save, as Skyrim's co-save does, but a native
// mod hears nothing when The Witcher 3 saves or loads. These wrappers leave the signal in hidden settings of the group
// ApocryphaMenuFramework, which the framework reads a few times a second: the save's file name first, then its counter
// raised by one. Nothing here changes what the game does.
function AMF_NewestSaveName() : string
{
	var saves : array< SSavegameInfo >;

	// The game's own save list, newest first (the list its load menu shows).
	theGame.GetRecentListSG( saves );
	if ( saves.Size() > 0 )
	{
		return saves[0].filename;
	}
	return "";
}

function AMF_RaiseCounter( varId : name )
{
	var cfg   : CInGameConfigWrapper;
	var count : int;

	cfg = theGame.GetInGameConfigWrapper();
	count = StringToInt( cfg.GetVarValue( 'ApocryphaMenuFramework', varId ), 0 );
	cfg.SetVarValue( 'ApocryphaMenuFramework', varId, IntToString( count + 1 ) );
}

// A save the player picked on the load page: its file name, for the game start that follows. A load without it (Continue,
// or the death screen's reload) is the newest save.
@wrapMethod(CR4IngameMenu)
function LoadSaveRequested( saveSlotRef : SSavegameInfo ) : void
{
	theGame.GetInGameConfigWrapper().SetVarValue( 'ApocryphaMenuFramework', 'PendingLoad', saveSlotRef.filename );
	wrappedMethod( saveSlotRef );
}

// Events: the wrappers end without a bare "return;" (the 1.0.0 compile stop: "Unable to convert from 'void' to 'Bool'").
@wrapMethod(CR4Game)
function OnSaveCompleted( type : ESaveGameType, succeeded : bool )
{
	var cfg : CInGameConfigWrapper;

	wrappedMethod( type, succeeded );
	if ( succeeded )
	{
		cfg = theGame.GetInGameConfigWrapper();
		cfg.SetVarValue( 'ApocryphaMenuFramework', 'SaveName', AMF_NewestSaveName() );
		AMF_RaiseCounter( 'SaveSeq' );
	}
}

@wrapMethod(CR4Game)
function OnGameStarted( restored : bool )
{
	var cfg      : CInGameConfigWrapper;
	var saveFile : string;

	wrappedMethod( restored );
	cfg = theGame.GetInGameConfigWrapper();
	saveFile = "";
	if ( restored )
	{
		saveFile = cfg.GetVarValue( 'ApocryphaMenuFramework', 'PendingLoad' );
		if ( saveFile == "" || saveFile == "-1" )
		{
			saveFile = AMF_NewestSaveName();
		}
	}
	cfg.SetVarValue( 'ApocryphaMenuFramework', 'PendingLoad', "" );
	cfg.SetVarValue( 'ApocryphaMenuFramework', 'LoadName', saveFile );
	AMF_RaiseCounter( 'LoadSeq' );
}

// "HERE ONLY" MOD MENUS (1.0.5, plan row 52). On the framework's Settings > Mod menus a mod's menu can be placed "here only":
// listed in the framework and left out of the game's own Options > Mods. The framework keeps the hidden setting
// ApocryphaMenuFramework.HiddenModMenus equal to those mods' menu paths, joined with ';' ("Mods.CRO;Mods.FaenMods.ATA_Name").
// The game fills Options in showOptionsPanel and has no way to hide one group from a script (IsGroupVisible is the XML's
// own visibilityCondition), so this one method is REPLACED: its body is the game's own (content0 ingameMenu.ws,
// CR4IngameMenu.showOptionsPanel, 2026-10-11) with only the filter added before the list goes to the menu.
// Conflict check (2026-10-11, every .ws in the owner's Mod Organizer 2 mods folder, UTF-8 and UTF-16): no other mod replaces
// or wraps showOptionsPanel. modFriendlyHUD (disabled in the profile) ships a whole old-generation ingameMenu.ws whose
// showOptionsPanel is the same apart from the Platform_PC_GDK check; with this replacement its copy is not used.
function AMF_SplitText( text : string, divider : string ) : array< string >
{
	var parts : array< string >;
	var left  : string;
	var right : string;
	var rest  : string;

	rest = text;
	while ( rest != "" )
	{
		left = "";
		right = "";
		if ( StrSplitFirst( rest, divider, left, right ) )
		{
			if ( left != "" )
			{
				parts.PushBack( left );
			}
			rest = right;
		}
		else
		{
			parts.PushBack( rest );
			rest = "";
		}
	}
	return parts;
}

// Takes the amfEntry at segments[depth] (and below) out of entries; an amfEntry left with nothing under it goes too.
function AMF_RemoveMenuPath( entries : CScriptedFlashArray, segments : array< string >, depth : int ) : bool
{
	var i       : int;
	var amfEntry   : CScriptedFlashObject;
	var below   : CScriptedFlashArray;
	var removed : bool;

	if ( !entries || depth >= segments.Size() )
	{
		return false;
	}
	for ( i = entries.GetLength() - 1; i >= 0; i -= 1 )
	{
		amfEntry = entries.GetElementFlashObject( i );
		if ( amfEntry && StrLower( amfEntry.GetMemberFlashString( "id" ) ) == StrLower( segments[depth] ) )
		{
			if ( depth == segments.Size() - 1 )
			{
				entries.RemoveElement( i );
				removed = true;
			}
			else
			{
				below = amfEntry.GetMemberFlashArray( "subElements" );
				if ( AMF_RemoveMenuPath( below, segments, depth + 1 ) )
				{
					removed = true;
					if ( below.GetLength() == 0 )
					{
						entries.RemoveElement( i );
					}
				}
			}
		}
	}
	return removed;
}

function AMF_LeaveOutHereOnlyMenus( entries : CScriptedFlashArray )
{
	var hidden : string;
	var paths  : array< string >;
	var i      : int;

	hidden = theGame.GetInGameConfigWrapper().GetVarValue( 'ApocryphaMenuFramework', 'HiddenModMenus' );
	if ( hidden == "" || hidden == "-1" )
	{
		return;
	}
	paths = AMF_SplitText( hidden, ";" );
	for ( i = 0; i < paths.Size(); i += 1 )
	{
		AMF_RemoveMenuPath( entries, AMF_SplitText( paths[i], "." ), 0 );
	}
}

@replaceMethod(CR4IngameMenu)
function showOptionsPanel() : void
{
	var l_DataFlashArray : CScriptedFlashArray;

	if (theGame.GetPlatform() == Platform_PC || theGame.GetPlatform() == Platform_PC_GDK)
	{
		m_fxSetHardwareCursorOn.InvokeSelfOneArg(FlashArgBool(mInGameConfigWrapper.GetVarValue('Rendering', 'HardwareCursor')));
	}

	l_DataFlashArray = IngameMenu_FillOptionsSubMenuData(m_flashValueStorage, isMainMenu);
	AMF_LeaveOutHereOnlyMenus( l_DataFlashArray );   // the framework's "here only" mods (the only line not the game's)

	m_initialSelectionsToIgnore = 1;
	OnPlaySoundEvent( "gui_global_panel_open" );

	m_flashValueStorage.SetFlashArray( "ingamemenu.options.entries", l_DataFlashArray );


	curMenuDepth = depthOptions;
	theTelemetry.NoticeMenuDepth(curMenuDepth);
}
