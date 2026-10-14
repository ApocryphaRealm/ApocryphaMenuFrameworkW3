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
	AMF_LoadGuardEdge( "menu", false );   // 1.0.6: the title screen is up (THE LOADING GUARD, below)
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
	AMF_LoadGuardEdge( "menu", false );   // 1.0.6: the title's or the pause menu's list is up - nothing loads behind it
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
		cfg.SetVarValue( 'ApocryphaMenuFramework', 'SaveName', "?newest" );   // the framework finds the newest .sav itself
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
			saveFile = "?newest";   // the framework finds the newest .sav itself (GetRecentListSG is not newest-first)
		}
	}
	cfg.SetVarValue( 'ApocryphaMenuFramework', 'PendingLoad', "" );
	cfg.SetVarValue( 'ApocryphaMenuFramework', 'LoadName', saveFile );
	AMF_RaiseCounter( 'LoadSeq' );
}

// THE LOADING GUARD (1.0.6; the owner, 2026-10-12: "Honestly amf shouldn't work during loading in as that can cause issues so
// build that in"). The framework does nothing while the game loads, but a native mod hears nothing when a loading screen
// starts or ends, so these wrappers say it: each writes ApocryphaMenuFramework.LoadGuard ("loading", "game" or "menu"), raises
// LoadGuardBegins when a load begins, and then raises LoadGuardSeq - the counter the framework watches (include// LoadGuardCore.h has the whole list and why each event was chosen). Nothing here changes what the game does; every wrapper
// calls the game's own code exactly as before.
function AMF_LoadGuardEdge( amfEdge : string, amfBegins : bool )
{
	theGame.GetInGameConfigWrapper().SetVarValue( 'ApocryphaMenuFramework', 'LoadGuard', amfEdge );
	if ( amfBegins )
	{
		AMF_RaiseCounter( 'LoadGuardBegins' );
	}
	AMF_RaiseCounter( 'LoadGuardSeq' );
}

// A save load is going ahead (the load page, Continue, the death screen, the engine's own quick load): the game closes its
// menus right after this, and the loading screen follows.
@wrapMethod(CR4Game)
function OnGameLoadInitFinishedSuccess()
{
	AMF_LoadGuardEdge( "loading", true );
	wrappedMethod();
}

// A session ends: quit to the main menu, or the start of a load or a world change from in game.
@wrapMethod(CR4Game)
function OnGameEnded()
{
	AMF_LoadGuardEdge( "loading", true );
	wrappedMethod();
}

// A new game, a load or a world change is loading (the loading screen is up).
@wrapMethod(CR4Game)
function OnGameStarting( restored : bool )
{
	AMF_LoadGuardEdge( "loading", true );
	wrappedMethod( restored );
}

// Fast travel to another region, or a boat or quest crossing to another world.
@wrapMethod(CR4Game)
function OnBeforeWorldChange( worldName : string )
{
	AMF_LoadGuardEdge( "loading", true );
	wrappedMethod( worldName );
}

// The loading screen after a game start (a new game, a load, a world change) has gone: the game is up.
@wrapMethod(CR4Game)
function OnAfterLoadingScreenGameStart()
{
	wrappedMethod();
	AMF_LoadGuardEdge( "game", false );
}

// The title screen closes - to start or load a game (or to quit).
@wrapMethod(CR4CommonMainMenuBase)
function OnClosingMenu()
{
	AMF_LoadGuardEdge( "loading", true );
	wrappedMethod();
}

// Fast travel inside the region: the game sets the loading screen and teleports the player behind it, with no game start
// after it - so the player's own timer says when it is over: the world ticks again and the screen is no longer black.
@wrapMethod(CCommonMapManager)
function PerformLocalFastTravelTeleport( destinationPinTag : name )
{
	AMF_LoadGuardEdge( "loading", true );
	wrappedMethod( destinationPinTag );
	if ( thePlayer )
	{
		thePlayer.AMF_WatchLocalTravel();
	}
}

@addField(CR4Player)
var amfTravelTicks : int;

@addField(CR4Player)
var amfTravelDark : bool;

@addMethod(CR4Player)
function AMF_WatchLocalTravel()
{
	amfTravelTicks = 0;
	amfTravelDark = false;
	RemoveTimer( 'AMF_LocalTravelDone' );
	AddTimer( 'AMF_LocalTravelDone', 0.25, true );
}

// Runs only while the world ticks (never under a loading screen). Waits for the screen to go dark and come back (or, if it
// never went dark, about two seconds), and ends the wait after about a minute of play whatever it saw.
@addMethod(CR4Player)
timer function AMF_LocalTravelDone( dt : float, id : int )
{
	var amfDark : bool;

	amfTravelTicks += 1;
	amfDark = theGame.IsBlackscreen() || theGame.IsFading() || theGame.IsLoadingScreenVideoPlaying();
	if ( amfDark )
	{
		amfTravelDark = true;
	}
	if ( amfTravelTicks < 240 && ( amfDark || ( !amfTravelDark && amfTravelTicks < 8 ) ) )
	{
		return;
	}
	RemoveTimer( 'AMF_LocalTravelDone' );
	AMF_LoadGuardEdge( "game", false );
}

// "HERE ONLY" MOD MENUS (1.0.5, plan row 52). On the framework's Settings > Mod menus a mod's menu can be placed "here only":
// listed in the framework and left out of the game's own Options > Mods. The framework keeps the hidden setting
// ApocryphaMenuFramework.HiddenModMenus equal to those mods' menu paths, joined with ';' ("Mods.CRO;Mods.FaenMods.ATA_Name").
// The game fills Options in showOptionsPanel and has no way to hide one group from a script (IsGroupVisible is the XML's
// own visibilityCondition), so showOptionsPanel is WRAPPED (1.0.6): the game's method - or another mod's replacement of
// it - runs first, as it is; then, only when the player has a "here only" mod, the list is built again with the game's own
// IngameMenu_FillOptionsSubMenuData, the hidden menus are taken out and the list is sent to the menu again. With nothing
// hidden the wrap does nothing at all.
// Why a wrap (1.0.6, a player's report): 1.0.5 REPLACED the method, and two @replaceMethod of one method do not compile -
// "Intuitive Menu" (modIntuitiveMenu, local\intuitivelayout.ws) replaces it too, and the game refused to start its scripts
// ("@replaceMethod duplicate found for 'CR4IngameMenu::showOptionsPanel'"). A wrap sits beside any number of wraps and one
// replacement. The one trade-off: with a "here only" mod AND another mod that builds its own Options list in its
// replacement, the list the menu ends up with is the game's, filtered - that mod's own changes to the list are not kept.
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

// The player's "here only" menus, or "" when there are none (the setting unset reads "" or "-1").
function AMF_HereOnlyMenus() : string
{
	var hidden : string;

	hidden = theGame.GetInGameConfigWrapper().GetVarValue( 'ApocryphaMenuFramework', 'HiddenModMenus' );
	if ( hidden == "-1" )
	{
		return "";
	}
	return hidden;
}

function AMF_LeaveOutHereOnlyMenus( entries : CScriptedFlashArray, hidden : string )
{
	var paths  : array< string >;
	var i      : int;

	if ( hidden == "" )
	{
		return;
	}
	paths = AMF_SplitText( hidden, ";" );
	for ( i = 0; i < paths.Size(); i += 1 )
	{
		AMF_RemoveMenuPath( entries, AMF_SplitText( paths[i], "." ), 0 );
	}
}

@wrapMethod(CR4IngameMenu)
function showOptionsPanel() : void
{
	var amfHidden : string;
	var amfList   : CScriptedFlashArray;

	wrappedMethod();   // the game's Options panel (or another mod's replacement of it), unchanged

	amfHidden = AMF_HereOnlyMenus();
	if ( amfHidden == "" )
	{
		return;   // no "here only" mod: nothing beyond the game's own code
	}
	amfList = IngameMenu_FillOptionsSubMenuData( m_flashValueStorage, isMainMenu );
	AMF_LeaveOutHereOnlyMenus( amfList, amfHidden );
	m_flashValueStorage.SetFlashArray( "ingamemenu.options.entries", amfList );
}
