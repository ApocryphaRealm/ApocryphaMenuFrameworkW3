// The general name rules: MO2 Modlist Manager's TEXT_SIGNALS (our own plugin, GPL-3.0), as generated for the Skyrim AMF's
// MCM sort (ApocryphaMenuFramework source/McmCategoryRules.inc, from tools/gen_mcm_categories.py). Copied unchanged;
// the Witcher 3 rules in ModMenusSort.cpp are weighted above them. Each row: a name pattern (std::regex, ECMAScript,
// case-insensitive), the group it votes for, and its weight.
{ R"(\b(address library|engine fixes|ssedisplaytweaks|display tweaks|crash logger|backported extended esl|bees|sse fixes|scrambled bugs|net script framework|preloader)\b)", "Utility", 3.6f },  // Essential Engine Fixes
{ R"(\b(skyui|racemenu|race menu|showracemenu|mcm helper|uiextensions|ui extensions|knotwork|item explorer)\b)", "Interface", 6.0f },  // User Interface
{ R"(\b(nemesis|pandora|fnis|open animation replacer|oar|dynamic animation replacer|dar|behavior data injector|bfco)\b)", "Animation", 6.0f },  // Animation - Character
{ R"(\b(security overhaul|lock variations|regional locks|extra locks|more locks)\b)", "WorldVisuals", 3.61f },  // Models and Textures - Clutter
{ R"(\b(revoiced|voiced|voice acting|voice ?overs?|audio output|audio device)\b)", "Audio", 3.61f },  // Audio
{ R"(\b(bethesda logo|logo remover|skip (the )?(bethesda )?logo|intro logos?|skip intro)\b)", "Utility", 3.61f },  // Essential Engine Fixes
{ R"(\b(scripting enhancements?|scripting|script enhancements?)\b)", "Utility", 3.61f },  // Script Fixes
{ R"(\b(no)?cam ?coll(ision)?(?![a-z])|\bcamera collision\b)", "Camera", 3.61f },  // Camera
{ R"(\b(strongbox(es)?)\b)", "WorldVisuals", 3.61f },  // Models and Textures - Furniture
{ R"(\b(npcs? learn to aim|learn to aim|npc aiming)\b)", "NpcsCreatures", 3.61f },  // NPC - AI and Behaviour
{ R"(\b(one click)\b)", "Controls", 3.61f },  // Improved Controls
{ R"(\b(downscaler|downscal(e|ing))\b)", "Utility", 3.61f },  // Performance Optimization
{ R"(\b(crime|bounty|bounties|arrest(s|ed)?|jail ?time|crimegold)\b)", "Gameplay", 3.61f },  // Gameplay - General
{ R"(\b(character creation|chargen|fins|(argonian|khajiit|beast) (crests?|horns|feathers|manes?|frills?))\b)", "Characters", 6.0f },  // Face
{ R"(\b(class overhaul|classes|races? overhaul|racial overhaul|integrated standing stones|standing stones? overhaul|birthsigns?)\b)", "Characters", 6.0f },  // Races, Classes, and Birthsigns
{ R"(\b(inverse kinematics)\b)", "Animation", 3.61f },  // Animation - Character
{ R"(\bload(ing)? ?screens?\b)", "Interface", 3.61f },  // User Interface
{ R"(\bcollisions?\b|\bcollide\b)", "WorldVisuals", 6.0f },  // Collision
{ R"(\b(debug(ging|ger)?|sentinel|diagnostics?|recursion monitor)\b)", "Utility", 6.0f },  // Debugging
{ R"(^scar\b|\bskyrim combos ai\b)", "NpcsCreatures", 6.0f },  // NPC - AI and Behaviour
{ R"(\b(diverse dragons|dragons? (replacers?|collection|overhaul|variants?|retextures?|remodels?)|got hotd|house of the dragon)\b)", "NpcsCreatures", 6.0f },  // Creatures - Monster Appearance
{ R"(\b(barking|howls?|howling|growls?|growling)\b)", "NpcsCreatures", 6.0f },  // NPC - AI and Behaviour
{ R"(\b(weightless|carry ?weight|encumbrance|item weights?)\b)", "Gameplay", 3.61f },  // Gameplay - General
{ R"(\b(papyrusutil|papyrus extender|powerofthree|po3|skypatcher|spell perk item distributor|spid|keyword item distributor|kid|jcontainers|consoleutil|community shaders|dynamic string distributor|sound record distributor|payload interpreter|xpmsse|xp32|scaleform translation|inventory injector|framework|distributor|injector|extender|loader|kiloader|runtime|sdk|api|hooks?)\b)", "Utility", 3.6f },  // Frameworks
{ R"(\b(texture set|assets? pack|tools?|utilit(y|ies)|generator|xedit|lodgen|texgen|dyndolod resources)\b)", "Utility", 1.0f },  // Utilities
{ R"(\b(resources?|modder'?s? resources?|library|tool ?kit|sdk|papyrus functions|function library)\b)", "Utility", 2.0f },  // Utilities
{ R"(\b(animation motion revolution|amr|key ?utils?|utils?)\b)", "Utility", 3.6f },  // Utilities
{ R"(\b(fix(es|ed|er)?|bug ?fix(es|er)?|corrections?|navmesh|hotfix)\b)", "Utility", 1.0f },  // Bug Fixes
{ R"(\b(efps|optimi[sz](ation|ed|er)|performance|insignificant object remover|lightened skyrim|occlusion|fps|shadow boost|object remover|culling|skyrim priority|cpu)\b)", "Utility", 3.6f },  // Performance Optimization
{ R"(\b(load ?times?|loadscreens?|loading (times?|screens?)|profilers?|cell lookups?|decompression|file copy|game start|ini file cacher)\b)", "Utility", 2.0f },  // Performance Optimization
{ R"(\b(unofficial .*patch|ussep|usmp)\b)", "Utility", 3.6f },  // Unofficial Patches
{ R"(\b(ui|hud|menus?|interface|widgets?|fonts?|compass|mcm|cursor|loading screens?|main menu|bestiary|character menu|follower stats|stats? menu|journal|inventory ?(menu|ui)|icons?)\b)", "Interface", 1.0f },  // User Interface
{ R"(\b(immersive ?hud|ihud)\b)", "Interface", 3.6f },  // User Interface
{ R"(\b(maps?|map markers?|world maps?|paper maps?|local maps?|minimaps?|mini maps?|atlas|cartograph(y|ers?)|fwmf|flat world map)\b)", "Interface", 3.6f },  // Maps
{ R"(\b(ui overhaul|nordic ui|untarnished ui|norden ui|dear diary|edge ui|skyhud|interface overhaul|reskin|smooth ui|dragonbreaker|dwemer ui)\b)", "Interface", 3.6f },  // UI Overhaul
{ R"(\b(read or take|better grabbing|btps|better third person selection|step up|quick ?loot|controls?(?! panel)|controller|gamepad|hotkeys?|keybinds?|keybinding|grab|activate|activation|interaction|pick ?up|take all|jump|sprint|whistle|auto ?(equip|unequip|loot|sort|draw)|unbind|bindings?|wheeler|wheel menu|back pocket)\b)", "Controls", 3.6f },  // Improved Controls
{ R"(\b(caps ?lock|alt ?-?tab|keyboard|mouse)\b)", "Controls", 1.5f },  // Improved Controls
{ R"(\b(cam|camera|cameras|smoothcam|fov|field of view|headtracking|head tracking)\b)", "Camera", 3.6f },  // Camera
{ R"(\b(dialogue|dialog|persuasion|conversations?|talk|speech|voice ?lines?|subtitles?)\b)", "Interface", 3.6f },  // Dialogue
{ R"(\b(alternate start|alternate perspective|realm of lorkhan|live another life|skyrim unbound|new game start|character creation start)\b)", "Utility", 3.6f },  // Alternate Start
{ R"(\b(saves?|autosaves?|save ?games?|save system)\b)", "Utility", 1.0f },  // Save Games
{ R"(\b(smp|hdt|cbpc|fsmp|physics|jiggle|bounce|cloth physics)\b)", "Characters", 3.6f },  // Physics
{ R"(\b(bod(y|ies)|skins?|cbbe|himbo|unp|3ba|bhunp|tbd|muscle|nipple|feet|hands|complexion|bodypaint|tattoos?|texture ?set)\b)", "Characters", 1.0f },  // Body
{ R"(\b(obody|autobody|body ?morphs?|body ?presets?|bodyslide presets?|body types?|body distribution)\b)", "Characters", 3.6f },  // Body
{ R"(\b(faces?|heads?|eyes?|brows?|eyebrows?|teeth|mouth|freckles?|scars|scar|warpaints?|makeup|blush(ing)?|tint|high poly head|expressions?|lips|complexions?|overlays?|tattoos?|bodypaints?|facegen|horns?)\b)", "Characters", 3.6f },  // Face
{ R"(\b(hairs?|hairdos?|hairstyles?|beards?|khisart[ai]n|stubble|ks hairdos|apachii|salt and wind|hairline)\b)", "Characters", 3.6f },  // Hair
{ R"(\b(races?|khajiit|argonians?|orcs?|orsimer|dunmer|altmer|bosmer|nords?|imperials?|bretons?|redguards?|birthsigns?|racial)\b)", "Characters", 1.0f },  // Races, Classes, and Birthsigns
{ R"(\b(animations?|animated|idles?|mco|bfco|skysa|adxp|locomotion|movement|dodge|tk dodge|true directional|tdm|diving|dive|swim|sprint animation|attack animations?|combos?)\b)", "Animation", 1.0f },  // Animation - Character
{ R"(\b(player animations?|first person animations?|pca)\b)", "Animation", 2.0f },  // Animation - Player
{ R"(\b(npc animations?|idle animations?|gesture|conversation animations?|citizen animations?)\b)", "Animation", 2.0f },  // Animation - NPC
{ R"(\b(enemy animations?|bandit animations?|draugr animations?|boss animations?)\b)", "Animation", 2.0f },  // Animation - Enemy
{ R"(\b(creature animations?|animal animations?|horse animations?|dragon animations?|wolf animations?|bear animations?)\b)", "Animation", 2.0f },  // Animation - Creature
{ R"(\b(lights?|lighting|lux|elfx|luminosity|window shadows|torch(es)?|lanterns?|candles?|illumination|shadows?|relighting|enb light)\b)", "WorldVisuals", 3.6f },  // Lighting
{ R"(\b(effects?|vfx|fx|particles?|blood|fire|smoke|spell effects?|impacts?|embers|lens|flares?|glow|magic effects?|mist|fog|explosions?|footprints|splash(es)?|sparks)\b)", "WorldVisuals", 1.5f },  // Visual Effects
{ R"(\b(enb|reshade)\b)", "WorldVisuals", 1.5f },  // Presets - ENB and ReShade
{ R"(\b(landscapes?|terrain|ground|rocks?|mountains?|cliffs?|dirt|complex parallax|parallax|tundra|mud|gravel|stones?)\b)", "WorldVisuals", 1.0f },  // Environment - Landscape
{ R"(\b(grass|grasses|folkvangr|veydosebrom|cathedral grass|landscape fixes for grass)\b)", "WorldVisuals", 2.0f },  // Environment - Grass
{ R"(\b(trees?|forests?|pines?|aspens?|birch(es)?|bark|happy little trees|nature of the wild lands|dead trees)\b)", "WorldVisuals", 2.0f },  // Environment - Trees
{ R"(\b(plants?|flora|flowers?|mushrooms?|shrubs?|bush(es)?|ferns?|ivy|moss|lichen|nirnroot|saplings?|blooms?|thickets?)\b)", "WorldVisuals", 2.0f },  // Environment - Plants
{ R"(\b(water|rivers?|waterfalls?|ocean|lakes?|realistic water|water for enb|shores?|foam|puddles)\b)", "WorldVisuals", 2.0f },  // Environment - Water
{ R"(\b(weathers?|sky|skies|clouds?|aurora|storms?|rain|climate|obsidian|cathedral weathers|azurite|nat|vivid weathers|wind)\b)", "WorldVisuals", 2.0f },  // Environment - Weather
{ R"(\b(seasons?|seasonal|turn of the seasons|winter|summer|autumn|spring|snowy)\b)", "WorldVisuals", 3.6f },  // Environment - Seasons
{ R"(\b(architecture|farmhouses?|chimneys?|exteriors?|rooftops|thatch|stonework|windmills?)\b)", "WorldVisuals", 3.6f },  // Environment - Architecture
{ R"(\b((hagraven|witch) (houses?|huts?)|huts?|shacks?|hovels?)\b)", "WorldVisuals", 3.0f },  // Environment - Architecture
{ R"(\b(roofs?|brick|city walls|fences?|docks|bridges?|wells?|signs?|signposts?|market stalls?)\b)", "WorldVisuals", 1.5f },  // Environment - Architecture
{ R"(\b(roads?|northern roads|pathways?|cobblestone|trails?|paths?)\b)", "WorldVisuals", 2.0f },  // Environment - Roads
{ R"(\b(re-?textures?|textures?|[1248]k|meshes?|hd|uhd|remesh|remodel|models?|retex)\b)", "WorldVisuals", 1.0f },  // Mesh Improvements
{ R"(\b(items?|potions? (models?|meshes|textures)|food|ingots?|gems?|soul ?gems?|scrolls? (models?|textures)|books? (models?|textures|covers?)|coins?|septims?|gold|weapon (models?|meshes)|armou?r (models?|meshes))\b)", "WorldVisuals", 1.0f },  // Models and Textures - Items
{ R"(\b(clutter|knapsacks?|sacks?|barrels?|crates?|baskets?|pottery|tankards?|junk|hay|firewood|buckets?|coins? of interesting nature)\b)", "WorldVisuals", 3.6f },  // Models and Textures - Clutter
{ R"(\b(bottles?|cups?|plates?|ropes?|jars?|bowls?|goblets?|candlesticks?|lamps?)\b)", "WorldVisuals", 1.5f },  // Models and Textures - Clutter
{ R"(\b(furniture|thrones?|chairs?|beds?|bench(es)?|shel(f|ves)|cabinets?|dressers?|wardrobes?|nightstands?|stools?)\b)", "WorldVisuals", 3.6f },  // Models and Textures - Furniture
{ R"(\b(tables?|desks?|counters?|bookcases?)\b)", "WorldVisuals", 1.5f },  // Models and Textures - Furniture
{ R"(\b(interiors?|snazzy|inn interiors?|tavern|shop|trader|store|rooms?|indoors?)\b)", "WorldVisuals", 3.6f },  // Models and Textures - Interiors
{ R"(\b(pbr|rmaos|parallax ?gen|pgpatcher|complex material)\b)", "WorldVisuals", 3.6f },  // PBR Textures
{ R"(\b(sounds?|audio|music|voices?|voiced|soundtrack|ambience|footsteps?|sfx)\b)", "Audio", 1.0f },  // Audio
{ R"(\b(gameplay|mechanics?|balance|difficulty|encounters?|survival|needs|hunger|thirst|frostfall|camping|campfire|durability|degrad(e|es|ation))\b)", "Gameplay", 1.0f },  // Gameplay - General
{ R"(\b(restrict(s|ion|ions|or|ors|ed)?)\b)", "Gameplay", 2.0f },  // Gameplay - General
{ R"(\bpress \w+ to\b)", "Gameplay", 3.6f },  // Gameplay - General
{ R"(\b(combat|parry|block(ing)?|stagger|killmoves?|poise|stamina ?regen|hit ?stop|melee|archery|damage|localized damage|localised damage|resistances?|weakness(es)?|armou?r rating|enemies|enemy)\b)", "Combat", 1.5f },  // Gameplay - Combat
{ R"(\b(stealth|sneak(ing)?|thie(f|ves)|pickpocket(ing)?|lockpick(ing)?|detection)\b)", "Gameplay", 1.5f },  // Gameplay - Stealth
{ R"(\b(economy|trade|trading|merchants?|prices?|barter|gold sink|taxes?)\b)", "Gameplay", 1.5f },  // Gameplay - Economy
{ R"(\b(immersive|immersion|realistic|wearable|bathing|sleep|eating|drinking|carry weight|carryweight|weight)\b)", "Gameplay", 1.0f },  // Immersion
{ R"(\b(alchemy|potions?|poisons?|apothecary|brewing)\b)", "Gameplay", 1.5f },  // Alchemy - Potions
{ R"(\b(ingredients?|reagents?|herbs?|harvest(ing)?)\b)", "Gameplay", 1.5f },  // Alchemy - Ingredients
{ R"(\b(alchemy ingredients?|alchemical ingredients?|drops? (alchemy )?ingredients?)\b)", "Gameplay", 2.0f },  // Alchemy - Ingredients
{ R"(\b(craft(ing)?|smithing|forge|tanning|cooking|recipes?|tempering|workbench|blacksmith)\b)", "Gameplay", 1.5f },  // Crafting - General
{ R"(^(base object swapper|bos)\b)", "Utility", 3.6f },  // Frameworks
{ R"(\b(atronach forge|forges?|workbench(es)?|tanning racks?|smelters?|crafting stations?|offering box)\b)", "Gameplay", 2.0f },  // Crafting - Stations
{ R"(\b(enchant(ing|ments?|ed)?|disenchant|enchanter)\b)", "Gameplay", 1.5f },  // Enchanting - Enchantments
{ R"(\b(spells?|magic|magicka|scrolls?|wards?|destruction|conjuration|illusion|restoration|alteration|summon(s|ing)?|rituals?|tomes?|staff|staves|mysticism|apocalypse|odin|arcanum|invisibility|reanimation)\b)", "MagicSkills", 1.0f },  // Magic - Spells & Enchantments
{ R"(\b(perks?|classes?|standing stones?|blessings?|powers?|shrine blessings?|ordinator|adamant|vokrii|apprentice|mannaz|aetherius|andromeda|paragon|custom skills?|skill trees?|skills?|level(l)?ing|experience|xp|attributes?)\b)", "MagicSkills", 1.5f },  // Class, Perks, Powers and Blessings
{ R"(\b(shouts?|thu'?um|word walls?|dragon ?souls?)\b)", "MagicSkills", 1.5f },  // Shouts
{ R"(\b(clothing|clothes|outfits?|dress(es)?|robes?|cloaks?|capes?|jewell?ery|amulets?|rings?|necklaces?|circlets?|earrings?|glasses|hoods?|scarf|scarves)\b)", "Gameplay", 1.0f },  // Clothing and Accessories
{ R"(\b(armou?rs?|cuirass|helmets?|boots|gauntlets|greaves|pauldrons|plate|mail)\b)", "Gameplay", 1.0f },  // Armour
{ R"(\b(shields?|bucklers?)\b)", "Gameplay", 1.0f },  // Armour - Shields
{ R"(\b(swords?|weapons?|weaponry|bows?|daggers?|axes?|maces?|greatswords?|warhammers?|blades?|arrows?|bolts?|crossbows?|spears?|katanas?|halberds?)\b)", "Gameplay", 1.0f },  // Weapons
{ R"(\b(shape data|bodyslide|outfit studio|refits?|conversions?)\b)", "Characters", 1.5f },  // Shape
{ R"(\b(dual sheathe?|equipment displays?|immersive equipment displays|ied|all geared up|equipment positioning|shields? on (the )?back|weapons? on (the )?back|back shields?|sheathe? positions?|holsters?)\b)", "Characters", 3.6f },  // Equipment Positioning
{ R"(\b(objects?|misc|containers?|chests?|displays?|book ?shel(f|ves)|lootable|placed items?)\b)", "WorldVisuals", 0.8f },  // Items and Objects - World
{ R"(\b(collectables?|collectibles?|treasure|treasure hunts?|puzzles?|collectables helper)\b)", "QuestsPlaces", 1.5f },  // Quests and Adventures
{ R"(\b(creatures?|animals?|beasts?|mihail|spiders?|trolls?|giants?|draugr|falmer|dwarven automatons?|monsters?|wildlife|deer|elk|rabbits?|foxes|chickens?|hawks?|birds?|fish)\b)", "NpcsCreatures", 1.0f },  // Creatures - New Creatures
{ R"(\b(wolves|dire wol(f|ves)|bears|sabre ?cats?|saber ?cats?|mammoths?|horkers?|skeevers?|mudcrabs?|chaurus|boars?|wolf packs?|prehistoric|dinosaurs?|megafauna)\b)", "NpcsCreatures", 2.0f },  // Creatures - New Creatures
{ R"(\b(creature (re)?textures?|animal (re)?textures?|dragon (re)?textures?|wolf (re)?textures?|bear (re)?textures?|hd creatures|bellyaches)\b)", "NpcsCreatures", 2.0f },  // Creatures - Appearance
{ R"(\b(creature (ai|behaviou?r)|animal (ai|behaviou?r)|predators?|prey|animal aggression)\b)", "NpcsCreatures", 2.0f },  // Creatures - Behaviour
{ R"(\b(horses?|mounts?|mounted|riding|steeds?|saddles?|convenient horses|horse power|immersive horses)\b)", "NpcsCreatures", 2.0f },  // Creatures - Mounts
{ R"(\b(npcs?|citizens|villagers|guards|jarls?|children|overhauled npcs|character overhaul)\b)", "NpcsCreatures", 1.0f },  // NPC - Appearance
{ R"(\b(bijin|pandorable|high poly npcs?|npc (overhaul|replacer|faces)|facegen|rs children|the ordinary women|males of skyrim|beards of power|npc hair)\b)", "NpcsCreatures", 2.0f },  // NPC - Appearance
{ R"(\b(ai overhaul|ai|behaviou?rs?|behaviou?r edits?|routines?|schedules?|sandbox(ing)?|pathing|combat ai|smart npcs?|reactions?|immersive citizens|npc (ai|behaviou?r)|take cover|circl(e|ing)|attackers?|surround(ing)?|flank(ing)?|take turns|wait your turn)\b)", "NpcsCreatures", 2.0f },  // NPC - AI and Behaviour
{ R"(\b(living ai|npc routines?|daily routines?|vivid routines|lifelike npcs?|living world)\b)", "NpcsCreatures", 3.6f },  // NPC - AI and Behaviour
{ R"(\b(followers?(?! stats?)|hirelings?)\b)", "NpcsCreatures", 3.6f },  // NPC - Followers
{ R"(\b(followers?|companions?|hirelings?|inigo|lucien|serana|nether'?s follower|ufo|eff|aft|nff|follower framework)\b)", "NpcsCreatures", 2.0f },  // NPC - Followers
{ R"(\b(npc friends|(summon|recall|teleport|gather) (your )?(friends|followers|allies|companions)|allies)\b)", "NpcsCreatures", 3.6f },  // NPC - Followers
{ R"(\b(player (appearance|preset|character)|racemenu presets?|character presets?|my character)\b)", "Characters", 2.0f },  // Player - Appearance
{ R"(\b(quests?|questing|adventures?|questline|storyline|campaign|quest tracking|quest tracker)\b)", "QuestsPlaces", 1.2f },  // Quests and Adventures
{ R"(\b(quest items?|unusual gems?|stones of barenziah|crown of barenziah)\b)", "QuestsPlaces", 2.0f },  // Quests and Adventures
{ R"(\b(quest expansions?|questlines?|quest mods?|alternate questline)\b)", "QuestsPlaces", 3.6f },  // Quests and Adventures
{ R"(\b(player ?homes?|homes?|houses?|manor|cabin|estate|abode|hideout|residence|cottage|lodge|sanctuary)\b)", "Characters", 1.5f },  // Player homes
{ R"(\b(vlindrel hall|breezehome|hjerim|honeyside|proudspire|lakeview|windstad|heljarchen|severin manor|myrwatch|tundra homestead|hendraheim|goldenhills)\b)", "Characters", 3.6f },  // Player homes
{ R"(\b(inns?|taverns?|temples?|shrines?|farms?|mills?|lighthouses?|forts?|castles?|palaces?|keeps?|stables?|jails?|prisons?|towers?|chapels?|halls?|guildhalls?)\b)", "WorldVisuals", 1.0f },  // Buildings
{ R"(\b(cit(y|ies)|whiterun|riften|solitude|windhelm|markarth|falkreath|dawnstar|morthal|winterhold)\b)", "WorldVisuals", 1.0f },  // Location Overhauls - City
{ R"(\b(towns?|villages?|hamlets?|settlements?|riverwood|rorikstead|ivarstead|shor'?s stone|kynesgrove|dragon bridge|karthwasten|helgen|skaal|stonehills|raven rock|darkwater|half-?moon mill|old hroldan|anga'?s mill|mixwater mill)\b)", "WorldVisuals", 1.0f },  // Location Overhauls - Town
{ R"(\b(dungeons?|caves?|ruins?|tombs?|barrows?|crypts?|mines?|nordic ruins?|dwemer ruins?|delves?)\b)", "QuestsPlaces", 1.0f },  // Dungeons
{ R"(\b(worldspace|new lands?|island|province|beyond skyrim|bruma|wyrmstooth|falskaar|expansion)\b)", "QuestsPlaces", 1.2f },  // Locations - New
{ R"(\bnew (dungeons?|locations?|lands?|areas?|worldspaces?|islands?|caves?|ruins?)\b)", "QuestsPlaces", 3.6f },  // Locations - New
{ R"(\b(vanilla locations?|location overhauls?|landmarks?|points? of interest|poi|environs|lost places)\b)", "WorldVisuals", 1.0f },  // Location Overhauls - General
{ R"(\b(guilds?|factions?|vigilants?( of stendarr)?|hall of the vigilant|thieves guild|dark brotherhood|college of winterhold|bards? college|dawnguard|stormcloaks?|imperial legion|civil war|companions guild)\b)", "QuestsPlaces", 1.0f },  // Guilds/Factions
{ R"(\b(cheats?|god ?(mode|items?)|infinite|unlimited|op)\b)", "Gameplay", 1.0f },  // Cheats and God items
{ R"(\b(patch(es|ed)?|compatibility|synergy|consistency)\b)", "Utility", 1.0f },  // Patches
// AMF's own rules, taught from menus the sort put under Other (tools/gen_mcm_categories.py AMF_RULES).
{ R"(\bC[\s.]?O[\s.]?I[\s.]?N\b|Coins of Interesting Nature)", "Gameplay", 8.0f },  // AMF: C.O.I.N. - coin economy
{ R"(Tag\s*(&|and)\s*Track)", "Interface", 8.0f },  // AMF: CS' Tag & Track - item tagging and tracking
{ R"(\bClamber\b)", "Animation", 8.0f },  // AMF: EVG Clamber - climbing
{ R"(Equipment\s*Manager)", "Controls", 8.0f },  // AMF: Outfit Wheeler - Dynamic Equipment Manager
{ R"(\bFEC\b|Frozen\s*Electrocuted\s*Combustion)", "WorldVisuals", 8.0f },  // AMF: FEC - death effects
{ R"(Favou?r\s*Jobs)", "QuestsPlaces", 8.0f },  // AMF: Favor Jobs Overhaul
{ R"(Honed\s*Metal)", "Gameplay", 8.0f },  // AMF: Honed Metal - smithing services
{ R"(\bIni\s*Editor\b)", "Utility", 8.0f },  // AMF: Custom INI Editor
{ R"(Renamer)", "Utility", 8.0f },  // AMF: Jaxonz Renamer
{ R"(Jewel+e?ry\s*Limiter)", "Gameplay", 8.0f },  // AMF: Jewelry Limiter
{ R"(Knock\s*back)", "Combat", 8.0f },  // AMF: Knockback
{ R"(\bRecorder\b)", "Utility", 8.0f },  // AMF: MCM Recorder
{ R"(\bSWL\b|Wearable\s*Lanterns)", "Gameplay", 8.0f },  // AMF: New SWL Toggle - Simple Wearable Lanterns key
{ R"(No\s*Fast\s*Travel)", "Gameplay", 8.0f },  // AMF: No Fast Travel
{ R"(Photo\s*Mode)", "Camera", 8.0f },  // AMF: Photo Mode
{ R"(Poisoner)", "Gameplay", 8.0f },  // AMF: Poisoner's Aid
{ R"(\bPrecision\b)", "Combat", 8.0f },  // AMF: Precision - melee hit collision
{ R"(Pumping\s*Iron)", "Characters", 8.0f },  // AMF: Pumping Iron - muscle growth
{ R"(Sanguine\s*Symphony|Blood\s*pool)", "WorldVisuals", 8.0f },  // AMF: Sanguine Symphony - blood pools
{ R"(Offen[cs]e\s*Suppression|Friendly\s*Fire)", "Combat", 8.0f },  // AMF: Simple Offence Suppression - friendly fire
{ R"(Strange\s*Runes)", "MagicSkills", 8.0f },  // AMF: Strange Runes - spell runes
{ R"(\bT[\s.]?N[\s.]?G\b|New\s*Gentleman)", "Characters", 8.0f },  // AMF: T.N.G. - The New Gentleman
{ R"(Timing\s*is\s*Everything)", "QuestsPlaces", 8.0f },  // AMF: Timing is Everything - quest start levels
{ R"(Wet\s*(and|&)\s*Cold)", "WorldVisuals", 8.0f },  // AMF: Wet and Cold - weather effects
{ R"(Wet\s*Function)", "WorldVisuals", 8.0f },  // AMF: WetFunction Redux - wet skin
{ R"(Acquisitive\s*Soul\s*Gem|\bASG\b)", "MagicSkills", 8.0f },  // AMF: ASG Multithreaded - soul gems (owner, 2026-10-05)
{ R"(\bGrass\b)", "WorldVisuals", 8.0f },  // AMF: grass mods
{ R"(\bOCPA\b|One\s*Click\s*Power\s*Attack)", "Gameplay", 8.0f },  // AMF: OCPA (owner, 2026-10-05)
{ R"(Read\s*the\s*Room)", "Animation", 8.0f },  // AMF: Read the Room (owner, 2026-10-05)
{ R"(Stendarr\s*Rising|Hall\s*of\s*the\s*Vigilant)", "QuestsPlaces", 8.0f },  // AMF: Stendarr Rising - Hall of the Vigilant (owner, 2026-10-05)
{ R"(Dynamic\s*Looting)", "Animation", 8.0f },  // AMF: Dynamic Looting - loot animations
{ R"(\bI[\s.]?C[\s.]?O[\s.]?W\b)", "Gameplay", 8.0f },  // AMF: I.C.O.W.
{ R"(\bP[\s.]?W[\s.]?E[\s.]?R\b)", "Gameplay", 8.0f },  // AMF: P.W.E.R
{ R"(Order\s*Squad)", "NpcsCreatures", 8.0f },  // AMF: Swiftly Order Squad - followers
{ R"(\bUBG\b)", "WorldVisuals", 8.0f },  // AMF: UBG Redone
