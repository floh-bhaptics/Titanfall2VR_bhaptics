untyped
// Client-side bHaptics hooks for Titanfall 2 VR (campaign).
//
// TF2VR_BHAPTICS is a compile-time constant from mod.json "PluginDependencies":
// true if Titanfall2VR_bhaptics.dll is loaded. The BH_* natives only exist
// then, so everything that uses them sits inside #if TF2VR_BHAPTICS.
//
// Hooks that are not verified for the campaign are looked up by name at
// runtime (BH_FindFunction). If one is missing, the plugin log shows a
// warning and that hook is skipped, instead of a script compile error.
//
// Event names are lower-cased by the plugin before they reach bHaptics.

global function TF2VR_BH_ClientInit

#if TF2VR_BHAPTICS

// ---- Tuning ----
const float BH_LOW_HEALTH_FRACTION = 0.25  // heartbeat below this share of max health
const float BH_HEARTBEAT_INTERVAL  = 1.0   // seconds between heartbeat starts
const float BH_HEALING_COOLDOWN    = 1.0   // min. seconds between two "healing" events
const int   BH_DAMAGE_SOURCE_MIN   = -1    // eDamageSourceId range to listen to
const int   BH_DAMAGE_SOURCE_MAX   = 511   // (generous; unknown ids simply never fire)

struct
{
	int   heartbeatGeneration = 0
	bool  heartbeatActive     = false
	int   lastHealth          = -1
	int   lastMaxHealth       = -1
	bool  wasHealing          = false
	float lastHealingTime     = -999.0
} file

#endif

void function TF2VR_BH_ClientInit()
{
#if TF2VR_BHAPTICS
	BH_Debug( "client script init" )

	// --- Verified in the campaign (used by CircuitLord's scripts) ---
	AddCreateTitanCockpitCallback( BH_OnTitanCockpitCreated )
	AddServerToClientStringCommandCallback( "BH_PlayerKilled", BH_OnPlayerKilledCommand )

	// --- Looked up at runtime ---
	var addTookDamage = BH_FindFunction( "AddLocalPlayerTookDamageCallback" )
	if ( addTookDamage != null )
	{
		for ( int id = BH_DAMAGE_SOURCE_MIN; id <= BH_DAMAGE_SOURCE_MAX; id++ )
			addTookDamage( id, BH_OnLocalPlayerTookDamage )
	}

	var addSpawned = BH_FindFunction( "AddCallback_LocalClientPlayerSpawned" )
	if ( addSpawned != null )
		addSpawned( BH_OnLocalPlayerSpawned )

	thread BH_HealthWatchThread()
#else
	printt( "[Titanfall2VR_bhaptics] client script loaded, native plugin NOT loaded - haptics disabled" )
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

// Angle of a damage source around the player, clockwise from the front
// (0 = front, 90 = right, 180 = back, 270 = left), as bHaptics expects for
// angleX. Source engine yaw grows counter-clockwise, hence view - source.
float function BH_HitAngle( entity player, vector sourceOrigin )
{
	vector toSource = sourceOrigin - player.CameraPosition()
	if ( toSource.x * toSource.x + toSource.y * toSource.y < 1.0 )
		return 0.0 // no usable direction (e.g. falling): front

	float angle = player.EyeAngles().y - VectorToAngles( toSource ).y
	angle = angle % 360.0
	if ( angle < 0.0 )
		angle += 360.0
	return angle
}

// ===================================================================
//  1. Taking damage (directional)
// ===================================================================

void function BH_OnLocalPlayerTookDamage( float damage, vector damageOrigin, int damageType, int damageSourceId, entity attacker )
{
	entity player = GetLocalViewPlayer()
	if ( !IsValid( player ) )
		return

	float angle = BH_HitAngle( player, damageOrigin )
	BH_Debug( "Took damage " + damage + " (source " + damageSourceId + ", type " + damageType + ") at angle " + angle )
	BH_PlayParam( "impact", 1.0, 1.0, angle, 0.0 )
}

// ===================================================================
//  3. Health: heartbeat loop and healing
// ===================================================================

void function BH_HealthWatchThread()
{
	while ( true )
	{
		WaitFrame()

		entity player = GetLocalClientPlayer()
		if ( !IsValid( player ) )
			continue

		if ( !IsAlive( player ) )
		{
			BH_StopHeartbeat( "player dead" )
			file.lastHealth = -1
			continue
		}

		int health    = player.GetHealth()
		int maxHealth = player.GetMaxHealth()
		if ( health == file.lastHealth && maxHealth == file.lastMaxHealth )
			continue

		BH_OnHealthChanged( player, file.lastHealth, health, file.lastMaxHealth, maxHealth )
		file.lastHealth    = health
		file.lastMaxHealth = maxHealth
	}
}

void function BH_OnHealthChanged( entity player, int oldHealth, int newHealth, int oldMax, int newMax )
{
	BH_Debug( "Health " + oldHealth + " -> " + newHealth + " (max " + newMax + ", titan " + player.IsTitan() + ")" )

	// First reading, or max health changed (embark/disembark): no healing event.
	if ( oldHealth < 0 || newMax != oldMax )
	{
		file.wasHealing = false
	}
	else if ( newHealth > oldHealth )
	{
		// Health regenerates in many small steps: only play at the start of a
		// healing phase, and not more often than BH_HEALING_COOLDOWN.
		if ( !file.wasHealing && Time() - file.lastHealingTime >= BH_HEALING_COOLDOWN )
		{
			BH_Play( "healing" )
			file.lastHealingTime = Time()
		}
		file.wasHealing = true
	}
	else
	{
		file.wasHealing = false
	}

	// Heartbeat only for the pilot, not for Titan health.
	bool low = !player.IsTitan() && newMax > 0 && newHealth > 0 && float( newHealth ) < float( newMax ) * BH_LOW_HEALTH_FRACTION
	if ( low )
		BH_StartHeartbeat()
	else
		BH_StopHeartbeat( "health above threshold" )
}

void function BH_StartHeartbeat()
{
	if ( file.heartbeatActive )
		return

	file.heartbeatActive = true
	file.heartbeatGeneration++
	BH_Debug( "Heartbeat loop started" )
	thread BH_HeartbeatThread( file.heartbeatGeneration )
}

void function BH_StopHeartbeat( string reason )
{
	if ( !file.heartbeatActive )
		return

	file.heartbeatActive = false
	file.heartbeatGeneration++ // ends the running thread at its next iteration
	BH_Debug( "Heartbeat loop stopped (" + reason + ")" )
}

void function BH_HeartbeatThread( int generation )
{
	while ( generation == file.heartbeatGeneration )
	{
		BH_Play( "heartbeat" )
		wait BH_HEARTBEAT_INTERVAL
	}
}

// ===================================================================
//  4. Death (sent by the server script)
// ===================================================================

void function BH_OnPlayerKilledCommand( array<string> args )
{
	BH_StopHeartbeat( "player killed" )
}

// ===================================================================
//  5. Spawn
// ===================================================================

void function BH_OnLocalPlayerSpawned( entity player )
{
	file.lastHealth = -1
	BH_StopHeartbeat( "player spawned" )
	BH_Play( "player_spawned" )
}

// ===================================================================
//  7. Entering the Titan
// ===================================================================

void function BH_OnTitanCockpitCreated( entity cockpit, entity player )
{
	BH_Play( "player_enter_titan" )
}

#endif
