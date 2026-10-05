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
