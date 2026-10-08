untyped
// Server-side bHaptics hooks for Titanfall 2 VR (campaign).
//
// In the campaign the server runs inside the game process, so the BH_*
// natives are available here as well and play directly.
//
// None of these hooks are verified for the campaign yet (they come from
// Northstar's multiplayer server scripts), so all of them are looked up by
// name at runtime. A missing one shows a warning in the plugin log and is
// skipped, instead of breaking the level load with a compile error.

global function TF2VR_BH_ServerInit

#if TF2VR_BHAPTICS

// ---- Tuning ----
const float BH_ZIPLINE_INTERVAL = 0.2 // seconds between "zipline" events

struct
{
	int           ziplineGeneration = 0
	bool          ziplineActive     = false
	array<entity> hookedPlayers
} file

#endif

void function TF2VR_BH_ServerInit()
{
#if TF2VR_BHAPTICS
	BH_Debug( "server script init" )

	BH_Hook( "AddCallback_OnPlayerKilled",           BH_OnPlayerKilled )
	BH_Hook( "AddCallback_ZiplineStart",             BH_OnZiplineStart )
	BH_Hook( "AddCallback_ZiplineStop",              BH_OnZiplineStop )
	BH_Hook( "AddCallback_OnTitanBecomesPilot",      BH_OnTitanBecomesPilot )
	BH_Hook( "AddCallback_OnTitanHealthSegmentLost", BH_OnTitanHealthSegmentLost )
	BH_Hook( "AddSoulDeathCallback",                 BH_OnSoulDeath )

	// Per-player hooks (shield, movement) need the player entity:
	// register them when it spawns. Both sources are deduplicated.
	var addSpawn = BH_FindFunction( "AddSpawnCallback" )
	if ( addSpawn != null )
		addSpawn( "player", BH_OnPlayerEntitySpawned )
	BH_Hook( "AddCallback_OnPlayerRespawned", BH_OnPlayerEntitySpawned )
#endif
}

#if TF2VR_BHAPTICS

// ===================================================================
//  Helpers
// ===================================================================

var function BH_FindFunction( string name )
{
	if ( name in getroottable() )
		return getroottable()[ name ]

	BH_Warn( "Hook not available in this game, skipped: " + name )
	return null
}

void function BH_Hook( string registerFunctionName, var callback )
{
	var registerFunction = BH_FindFunction( registerFunctionName )
	if ( registerFunction != null )
		registerFunction( callback )
}

// Value of an enum member, or -1 if the enum/member doesn't exist here.
int function BH_EnumValue( string enumName, string memberName )
{
	table consts = getconsttable()
	if ( !( enumName in consts ) )
	{
		BH_Warn( "Enum not available in this game: " + enumName )
		return -1
	}

	var members = consts[ enumName ]
	if ( !( memberName in members ) )
	{
		BH_Warn( "Enum member not available: " + enumName + "." + memberName )
		return -1
	}
	return expect int( members[ memberName ] )
}

bool function BH_IsOurPlayer( entity ent )
{
	return IsValid( ent ) && ent.IsPlayer()
}

// ===================================================================
//  Per-player hooks: 2. shield damage, 6. movement
// ===================================================================

void function BH_OnPlayerEntitySpawned( entity player )
{
	if ( !BH_IsOurPlayer( player ) || file.hookedPlayers.contains( player ) )
		return
	file.hookedPlayers.append( player )
	BH_Debug( "Registering per-player hooks" )

	var addShield = BH_FindFunction( "AddEntityCallback_OnPostShieldDamage" )
	if ( addShield != null )
		addShield( player, BH_OnPostShieldDamage )

	var addMovement = BH_FindFunction( "AddPlayerMovementEventCallback" )
	if ( addMovement != null )
	{
		BH_AddMovement( addMovement, player, "JUMP",          BH_OnJump )
		BH_AddMovement( addMovement, player, "DOUBLE_JUMP",   BH_OnJump )
		BH_AddMovement( addMovement, player, "DODGE",         BH_OnDodge )
		BH_AddMovement( addMovement, player, "TOUCH_GROUND",  BH_OnLand )
		BH_AddMovement( addMovement, player, "MANTLE",        BH_OnMantle )
		BH_AddMovement( addMovement, player, "BEGIN_WALLRUN", BH_OnBeginWallrun )
		BH_AddMovement( addMovement, player, "END_WALLRUN",   BH_OnEndWallrun )
	}
}

void function BH_AddMovement( var addMovement, entity player, string eventName, var callback )
{
	int eventId = BH_EnumValue( "ePlayerMovementEvents", eventName )
	if ( eventId >= 0 )
		addMovement( player, eventId, callback )
}

// ===================================================================
//  2. Shield damage
// ===================================================================

void function BH_OnPostShieldDamage( entity ent, var damageInfo, float shieldDamage )
{
	if ( !BH_IsOurPlayer( ent ) || shieldDamage <= 0.0 )
		return

	BH_Debug( "Shield damage " + shieldDamage )
	BH_Play( "shield_damage" )
}

// ===================================================================
//  4. Death
// ===================================================================

void function BH_OnPlayerKilled( entity victim, entity attacker, var damageInfo )
{
	if ( !BH_IsOurPlayer( victim ) )
		return

	BH_StopZipline( "player killed" )
	BH_Play( "player_killed" )
	ServerToClientStringCommand( victim, "BH_PlayerKilled" ) // stops the client heartbeat
}

// ===================================================================
//  6. Movement
// ===================================================================

void function BH_OnJump( entity player )         { BH_Play( "player_jump" ) }
void function BH_OnDodge( entity player )        { BH_Play( "player_dodge" ) }
void function BH_OnLand( entity player )         { BH_Play( "player_land" ) }
void function BH_OnMantle( entity player )       { BH_Play( "player_mantle" ) }
void function BH_OnBeginWallrun( entity player ) { BH_Play( "begin_wallrun" ) }
void function BH_OnEndWallrun( entity player )   { BH_Play( "end_wallrun" ) }

// Zipline loop

void function BH_OnZiplineStart( entity player, entity zipline )
{
	if ( !BH_IsOurPlayer( player ) || file.ziplineActive )
		return

	file.ziplineActive = true
	file.ziplineGeneration++
	BH_Debug( "Zipline loop started" )
	thread BH_ZiplineThread( player, file.ziplineGeneration )
}

void function BH_OnZiplineStop( entity player )
{
	if ( BH_IsOurPlayer( player ) )
		BH_StopZipline( "zipline stop" )
}

void function BH_StopZipline( string reason )
{
	if ( !file.ziplineActive )
		return

	file.ziplineActive = false
	file.ziplineGeneration++ // ends the running thread at its next iteration
	BH_Debug( "Zipline loop stopped (" + reason + ")" )
}

void function BH_ZiplineThread( entity player, int generation )
{
	while ( generation == file.ziplineGeneration && IsValid( player ) )
	{
		BH_Play( "zipline" )
		wait BH_ZIPLINE_INTERVAL
	}
}

// ===================================================================
//  8.-10. Titan
// ===================================================================

void function BH_OnTitanBecomesPilot( entity pilot, entity npcTitan )
{
	if ( BH_IsOurPlayer( pilot ) )
		BH_Play( "player_exit_titan" )
}

void function BH_OnTitanHealthSegmentLost( entity victim, entity attacker )
{
	// victim is the player entity while embarked
	if ( BH_IsOurPlayer( victim ) )
		BH_Play( "titan_hit" )
}

void function BH_OnSoulDeath( entity soul, var damageInfo )
{
	if ( !IsValid( soul ) )
		return

	// Our Titan: either we are in it, or it is BT running as auto-titan.
	entity titan = soul.GetTitan()
	bool ours = BH_IsOurPlayer( titan ) || BH_IsOurPlayer( soul.GetBossPlayer() )
	if ( ours )
		BH_Play( "titan_destroyed" )
}

#endif
