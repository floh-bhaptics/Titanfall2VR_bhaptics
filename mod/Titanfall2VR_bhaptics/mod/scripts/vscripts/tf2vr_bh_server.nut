// Server-side bHaptics hooks for Titanfall 2 VR (campaign).
//
// In the campaign the server runs inside the game process, so the BH_*
// natives are available here as well and play directly.
//
// These hooks come from Northstar's multiplayer server scripts. If one of
// them doesn't exist in the campaign, the game reports a SERVER script
// COMPILE ERROR naming it, and that hook has to be removed or replaced.

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

	AddCallback_OnPlayerKilled( BH_OnPlayerKilled )
	AddCallback_ZiplineStart( BH_OnZiplineStart )
	AddCallback_ZiplineStop( BH_OnZiplineStop )
	AddCallback_OnTitanBecomesPilot( BH_OnTitanBecomesPilot )
	AddCallback_OnTitanHealthSegmentLost( BH_OnTitanHealthSegmentLost )
	AddSoulDeathCallback( BH_OnSoulDeath )

	// Per-player hooks (shield, movement) need the player entity:
	// register them when it spawns. Both sources are deduplicated.
	AddSpawnCallback( "player", BH_OnPlayerEntitySpawned )
	AddCallback_OnPlayerRespawned( BH_OnPlayerEntitySpawned )
#endif
}

#if TF2VR_BHAPTICS

// ===================================================================
//  Helpers
// ===================================================================

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

	AddEntityCallback_OnPostShieldDamage( player, BH_OnPostShieldDamage )

	AddPlayerMovementEventCallback( player, ePlayerMovementEvents.JUMP,          BH_OnJump )
	AddPlayerMovementEventCallback( player, ePlayerMovementEvents.DOUBLE_JUMP,   BH_OnJump )
	AddPlayerMovementEventCallback( player, ePlayerMovementEvents.DODGE,         BH_OnDodge )
	AddPlayerMovementEventCallback( player, ePlayerMovementEvents.TOUCH_GROUND,  BH_OnLand )
	AddPlayerMovementEventCallback( player, ePlayerMovementEvents.MANTLE,        BH_OnMantle )
	AddPlayerMovementEventCallback( player, ePlayerMovementEvents.BEGIN_WALLRUN, BH_OnBeginWallrun )
	AddPlayerMovementEventCallback( player, ePlayerMovementEvents.END_WALLRUN,   BH_OnEndWallrun )
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
