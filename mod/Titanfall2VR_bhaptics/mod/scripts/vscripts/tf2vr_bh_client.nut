// Client-side bHaptics hooks for Titanfall 2 VR (campaign).
//
// TF2VR_BHAPTICS is a compile-time constant from mod.json "PluginDependencies":
// true if Titanfall2VR_bhaptics.dll is loaded. The BH_* natives only exist
// then, so everything that uses them sits inside #if TF2VR_BHAPTICS.
//
// If a hook doesn't exist in the campaign, the game reports a CLIENT script
// COMPILE ERROR naming it, and that hook has to be removed or replaced.
//
// Event names are lower-cased by the plugin before they reach bHaptics.

global function TF2VR_BH_ClientInit
#if TF2VR_BHAPTICS
global function BH_TrackProjectile // called from tf2vr_bh_missiles_client.nut
#endif

#if TF2VR_BHAPTICS

// ---- Tuning ----
const float BH_LOW_HEALTH_FRACTION = 0.25  // heartbeat below this share of max health
const float BH_HEARTBEAT_INTERVAL  = 1.0   // seconds between heartbeat starts
const float BH_HEALING_COOLDOWN    = 1.0   // min. seconds between two "healing" events
const int   BH_DAMAGE_SOURCE_MIN   = -1    // eDamageSourceId range to listen to
const int   BH_DAMAGE_SOURCE_MAX   = 511   // (generous; unknown ids simply never fire)
const float BH_EXPLOSION_RANGE     = 1500.0 // game units (1 unit ~ 1 inch, so ~38 m / 125 ft)
const float BH_PROJECTILE_MIN_LIFE = 0.3   // seconds: shorter-lived projectiles are client-side
                                           // stand-ins (e.g. your own grenade at the moment you throw it)
const float BH_EXPLOSION_MIN_INTENSITY = 0.2 // intensity at the edge of the range
const float BH_EXPLOSION_DEDUPE    = 0.3   // seconds: a rocket salvo or a blast seen by both methods plays once

struct BH_Projectile
{
	entity ent
	vector lastOrigin
	float  createdTime
	string weaponName
}

struct
{
	array<BH_Projectile> projectiles
	int   heartbeatGeneration = 0
	bool  heartbeatActive     = false
	int   lastHealth          = -1
	int   lastMaxHealth       = -1
	bool  wasHealing          = false
	float lastHealingTime     = -999.0
	entity recoilWeapon
	int    recoilClip         = -1
	float  lastExplosionTime  = -999.0
	bool   meleeActive        = false
} file

#endif

void function TF2VR_BH_ClientInit()
{
#if TF2VR_BHAPTICS
	BH_Debug( "client script init" )

	AddCreateTitanCockpitCallback( BH_OnTitanCockpitCreated )
	AddServerToClientStringCommandCallback( "BH_PlayerKilled", BH_OnPlayerKilledCommand )
	AddCallback_LocalClientPlayerSpawned( BH_OnLocalPlayerSpawned )

	// Explosions, method 1: projectiles (grenades, rockets) are reported by
	// ClientCodeCallback_OnMissileCreation in tf2vr_bh_missiles_client.nut and
	// tracked until they disappear (= detonate).
	thread BH_ProjectileWatchThread()

	// Damage callbacks are registered per damage source id.
	for ( int id = BH_DAMAGE_SOURCE_MIN; id <= BH_DAMAGE_SOURCE_MAX; id++ )
		AddLocalPlayerTookDamageCallback( id, BH_OnLocalPlayerTookDamage )

	thread BH_HealthWatchThread()
	thread BH_RecoilWatchThread()
	thread BH_MeleeWatchThread()
#else
	printt( "[Titanfall2VR_bhaptics] client script loaded, native plugin NOT loaded - haptics disabled" )
#endif
}

#if TF2VR_BHAPTICS

// ===================================================================
//  Helpers
// ===================================================================

// Angle of a damage source around the player, counter-clockwise from the
// front (0 = front, 90 = left, 180 = back, 270 = right), as bHaptics expects
// for angleX (verified in game). Source engine yaw also grows
// counter-clockwise, hence source - view.
float function BH_HitAngle( entity player, vector sourceOrigin )
{
	vector toSource = sourceOrigin - player.CameraPosition()
	if ( toSource.x * toSource.x + toSource.y * toSource.y < 1.0 )
		return 0.0 // no usable direction (e.g. falling): front

	float angle = VectorToAngles( toSource ).y - player.EyeAngles().y
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

	// Explosions, method 2: explosive damage on the player.
	if ( ( damageType & DF_EXPLOSION ) != 0 )
		BH_PlayExplosion( "damage", 1.0, angle )
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

// ===================================================================
//  11. Recoil
//
//  Shots are detected by watching the active weapon's magazine: when the
//  clip count drops, a shot was fired. Reloads (count goes up) and weapon
//  switches only reset the baseline. The hand comes from CircuitLord's
//  TF2VR_WeaponHand() (0 = left, 1 = right; main hand when two-handed).
//  Weapons without a magazine (charge weapons) don't trigger this.
// ===================================================================

void function BH_RecoilWatchThread()
{
	while ( true )
	{
		WaitFrame()

		entity player = GetLocalClientPlayer()
		if ( !IsValid( player ) || !IsAlive( player ) )
		{
			file.recoilWeapon = null
			continue
		}

		entity weapon = player.GetActiveWeapon()
		if ( !IsValid( weapon ) )
		{
			file.recoilWeapon = null
			continue
		}

		int clip = weapon.GetWeaponPrimaryClipCount()

		if ( weapon != file.recoilWeapon )
		{
			// Weapon switch or pickup: new baseline, no shot.
			file.recoilWeapon = weapon
			file.recoilClip   = clip
			continue
		}

		if ( clip < file.recoilClip )
		{
			if ( player.IsTitan() )
				BH_PlayTitanRecoil( weapon )
			else
				BH_PlayRecoil( weapon )
		}

		file.recoilClip = clip
	}
}

// Titan weapons with a magazine (XO-16, 40mm, Leadwall, ...). No hand: the
// Titan fires, not the player. Weapons without a magazine don't trigger this.
void function BH_PlayTitanRecoil( entity weapon )
{
	BH_Debug( "Titan shot: " + weapon.GetWeaponClassName() )
	BH_Play( "recoil_titan" )
}

void function BH_PlayRecoil( entity weapon )
{
	string side  = TF2VR_WeaponHand() == 0 ? "l" : "r"
	string group = BH_RecoilGroup( weapon.GetWeaponClassName() )
	BH_Play( "recoil_" + group + "_" + side )
}

// pistol  = one-handed pistols and SMGs
// rifle   = two-handed rifles and LMGs
// shotgun = shotguns, snipers and launchers (the heavy kick)
string function BH_RecoilGroup( string weaponClass )
{
	switch ( weaponClass )
	{
		case "mp_weapon_semipistol":      // P2016
		case "mp_weapon_autopistol":      // RE-45
		case "mp_weapon_wingman":         // Wingman
		case "mp_weapon_wingman_n":       // Wingman Elite
		case "mp_weapon_smart_pistol":    // Smart Pistol
		case "mp_weapon_gibber_pistol":
		case "mp_weapon_alternator_smg":  // Alternator
		case "mp_weapon_car":             // CAR
		case "mp_weapon_r97":             // R-97
		case "mp_weapon_hemlok_smg":      // Volt
		case "sp_weapon_arc_tool":        // Arc Tool (campaign)
			return "pistol"

		case "mp_weapon_rspn101":         // R-201
		case "mp_weapon_rspn101_og":      // R-101
		case "mp_weapon_hemlok":          // Hemlok
		case "mp_weapon_g2":              // G2A5
		case "mp_weapon_vinson":          // Flatline
		case "mp_weapon_lmg":             // Spitfire
		case "mp_weapon_lstar":           // L-STAR
		case "mp_weapon_esaw":            // Devotion
			return "rifle"

		case "mp_weapon_shotgun":         // EVA-8
		case "mp_weapon_mastiff":         // Mastiff
		case "mp_weapon_shotgun_pistol":  // Mozambique
		case "mp_weapon_shotgun_doublebarrel":
		case "mp_weapon_sniper":          // Kraber
		case "mp_weapon_doubletake":      // Double Take
		case "mp_weapon_dmr":             // Longbow DMR
		case "mp_weapon_epg":             // EPG
		case "mp_weapon_smr":             // Sidewinder
		case "mp_weapon_softball":        // Softball
		case "mp_weapon_pulse_lmg":       // Cold War
		case "mp_weapon_rocket_launcher": // Archer
		case "mp_weapon_arc_launcher":    // Thunderbolt
		case "mp_weapon_mgl":             // MGL
		case "mp_weapon_defender":        // Charge Rifle
			return "shotgun"
	}

	// Fallback for anything not listed above.
	if ( weaponClass.find( "shotgun" ) != null || weaponClass.find( "sniper" ) != null )
		return "shotgun"
	if ( weaponClass.find( "pistol" ) != null || weaponClass.find( "smg" ) != null )
		return "pistol"

	BH_Debug( "Recoil: unmapped weapon class " + weaponClass + ", using rifle" )
	return "rifle"
}

// ===================================================================
//  12. Explosions
// ===================================================================

// Method 1: track projectiles from creation until they disappear. When one
// vanishes near the player, that is where it detonated.
void function BH_TrackProjectile( entity ent, string weaponName )
{
	if ( !IsValid( ent ) )
		return

	foreach ( BH_Projectile p in file.projectiles )
	{
		if ( p.ent == ent )
			return // already tracked (the callback can fire more than once)
	}

	BH_Projectile p
	p.ent         = ent
	p.lastOrigin  = ent.GetOrigin()
	p.createdTime = Time()
	p.weaponName  = weaponName
	file.projectiles.append( p )

	BH_Debug( "Projectile tracked: " + ent.GetClassName() + " (" + weaponName + ")" )
}

void function BH_ProjectileWatchThread()
{
	while ( true )
	{
		WaitFrame()

		for ( int i = file.projectiles.len() - 1; i >= 0; i-- )
		{
			if ( IsValid( file.projectiles[ i ].ent ) )
			{
				file.projectiles[ i ].lastOrigin = file.projectiles[ i ].ent.GetOrigin()
				continue
			}

			BH_Projectile gone = file.projectiles[ i ]
			file.projectiles.remove( i )
			BH_OnProjectileGone( gone )
		}
	}
}

void function BH_OnProjectileGone( BH_Projectile p )
{
	float life = Time() - p.createdTime
	entity player = GetLocalViewPlayer()
	if ( !IsValid( player ) )
		return

	float dist = Distance( p.lastOrigin, player.GetOrigin() )
	BH_Debug( "Projectile gone: " + p.weaponName + " after " + life + " s at distance " + dist )

	if ( life < BH_PROJECTILE_MIN_LIFE || dist > BH_EXPLOSION_RANGE )
		return

	// Linear falloff: full strength at the player, minimum at the range edge.
	float intensity = 1.0 - ( dist / BH_EXPLOSION_RANGE ) * ( 1.0 - BH_EXPLOSION_MIN_INTENSITY )
	BH_PlayExplosion( "projectile", intensity, BH_HitAngle( player, p.lastOrigin ) )
}

// Shared by both methods. "method" only goes to the debug log, to compare
// which method catches which blasts.
void function BH_PlayExplosion( string method, float intensity, float angle )
{
	// Explosions only block other explosions; impact and everything else
	// always play.
	if ( Time() - file.lastExplosionTime < BH_EXPLOSION_DEDUPE )
	{
		BH_Debug( "Explosion via " + method + " skipped (another explosion within " + BH_EXPLOSION_DEDUPE + " s)" )
		return
	}
	file.lastExplosionTime = Time()

	BH_Debug( "Explosion via " + method + ", intensity " + intensity + ", angle " + angle )
	BH_PlayParam( "explosion", intensity, 1.0, angle, 0.0 )
}

// ===================================================================
//  13. Titan melee (punch and sword)
//
//  Polls the player's melee state; a change to "attack active" is a swing.
// ===================================================================

void function BH_MeleeWatchThread()
{
	while ( true )
	{
		WaitFrame()

		entity player = GetLocalClientPlayer()
		if ( !IsValid( player ) || !IsAlive( player ) )
		{
			file.meleeActive = false
			continue
		}

		bool active = player.PlayerMelee_IsAttackActive()
		if ( active && !file.meleeActive && player.IsTitan() )
			BH_Play( "titan_melee" )

		file.meleeActive = active
	}
}

#endif
