// =====================================================================================================================
// Apocrypha Menu Framework - its entry in the game's own menu (the title screen and the pause menu).
//
// A controller player opens AMF from here, the way a Skyrim player opens it from the journal's System row (the owner,
// 2026-10-05: "it should just be on the menu or the in-game quick menu just like it is in Skyrim for controller").
//
// Annotations only: no game script is replaced, so nothing needs merging in Script Merger and other mods that wrap the
// same functions keep working.
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
