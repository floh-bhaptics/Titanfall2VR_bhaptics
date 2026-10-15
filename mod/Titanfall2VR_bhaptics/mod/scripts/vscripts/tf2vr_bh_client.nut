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

// Titan footsteps (technique from the Titanfall2-SimHub-Motion-Addon by nicotrial):
// a footfall is a crossing of 0.0 or 0.5 in the Titan's animation cycle,
// gated by horizontal speed and being on the ground. Speeds in game units/s.
const float BH_STEP_SPEED_MIN      = 20.0  // ~0.5 m/s: below this, no steps
const float BH_STEP_SPEED_FULL     = 118.0 // ~3 m/s: full walking blend
const float BH_STEP_VERTICAL_MAX   = 79.0  // ~2 m/s: faster vertical motion = not grounded
const float BH_STEP_BLEND_MIN      = 0.2   // walking * grounded blend needed for a step
const float BH_STEP_MAX_ADVANCE    = 0.25  // larger cycle jumps = animation switch, not a step
const float BH_STEP_LEAD_TIME      = 0.25  // seconds: play this much before the footfall
                                           // (converted to a cycle offset from the current animation speed)

// Titanfall: a Titan falling fast and then stopping abruptly.
const float BH_TITANFALL_FALL_SPEED = 600.0  // ~15 m/s downwards counts as falling
const float BH_TITANFALL_STOP_SPEED = 150.0  // slower than this after falling = landed
const float BH_TITANFALL_MIN_FALL   = 0.15   // seconds of falling needed (filters teleports)
const float BH_TITANFALL_RANGE      = 4000.0 // ~100 m: farther landings are ignored
const float BH_TITANFALL_MIN_INTENSITY = 0.3

// Shoulder rockets: one effect per salvo. Further detections are ignored
// until no rocket activity was seen for this long (salvos in testing were
// >= 5 s apart, detections within one salvo up to ~2 s apart).
const float BH_ROCKET_SALVO_QUIET  = 3.0

// Timeshift ("Effect and Cause"): past and present are two copies of the level
// in the same map, and a time jump moves the player between them. Moving this
// far within one frame can only be such a jump (~38 m; sprinting covers a few
// units per frame).
const float BH_TIMESHIFT_MIN_JUMP  = 1500.0

// Titan flight and core
const float BH_FLIGHT_INTERVAL     = 0.5  // seconds between "titan_flight" while Flight Core is active
const float BH_CORE_CHARGE_INTERVAL = 0.2 // seconds between "titan_core_charge" during the core wind-up
const float BH_SHIELD_DEDUPE       = 1.0  // seconds: one "titan_shield_start" per use
const float BH_LOAD_QUIET_TIME     = 3.0  // seconds after level load without "player_enter_titan"

struct BH_Projectile
{
	entity ent
	vector lastOrigin
	float  createdTime
	string weaponName
}

struct BH_FallTracker
{
	entity ent
	float  lastZ
	float  lastTime
	float  fallTime     // seconds spent falling fast so far
	float  maxFallSpeed // fastest downward speed seen in this fall
}

struct
{
	array<BH_Projectile> projectiles
	array<BH_FallTracker> titans
	BH_FallTracker& playerFall // reference: struct fields can only be reassigned as references
	bool   playerFallInit     = false
	float  lastStepCycle      = -1.0
	float  lastStepTime       = 0.0
	entity ordnanceWeapon
	int    ordnanceClip       = -1
	float  ordnanceCooldown   = 0.0
	float  lastRocketActivity = -999.0
	int   heartbeatGeneration = 0
	bool  heartbeatActive     = false
	int   lastHealth          = -1
	int   lastMaxHealth       = -1
	bool  wasHealing          = false
	float lastHealingTime     = -999.0
	entity recoilWeapon
	int    recoilClip         = -1
	// VR mod interaction counters (convars set by the VR mod's native code)
	float  initTime            = 0.0
	int    gunAck              = 0
	int    grenadeAck          = 0
	int    grenadeThrowTicket  = 0
	int    reloadMagazineOut   = 0
	int    reloadEjections     = 0
	int    reloadRounds        = 0
	float  lastExplosionTime  = -999.0
	bool   meleeActive        = false
} file

#endif

void function TF2VR_BH_ClientInit()
{
#if TF2VR_BHAPTICS
	BH_Debug( "client script init" )
	file.initTime = Time()

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
	thread BH_TitanStepWatchThread()
	thread BH_TitanRocketWatchThread()

	// Titanfall: track every Titan that appears (plus our own Titan when we
	// are inside, see BH_TitanfallWatchThread).
	AddCreateCallback( "npc_titan", BH_OnNpcTitanCreated )
	thread BH_TitanfallWatchThread()

	// VR mod interactions: guns drawn from a holster, grenades taken from the
	// chest, grenade throws and reload steps (VR mod convars).
	thread BH_InteractionWatchThread()

	// Titan VTOL hover, Flight Core, core ready / wind-up / active.
	thread BH_TitanAbilityWatchThread()

	// Timeline switches, only in the two "Effect and Cause" maps. The device's
	// wind-up ("timeshift_charge") is played by tf2vr_bh_timeshift_server.nut.
	string map = GetMapName()
	if ( map == "sp_hub_timeshift" || map == "sp_timeshift_spoke02" )
		thread BH_TimeshiftWatchThread()
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
	// Levels that start inside the Titan create the cockpit while loading.
	if ( Time() - file.initTime < BH_LOAD_QUIET_TIME )
	{
		BH_Debug( "player_enter_titan skipped (level load)" )
		return
	}
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
			{
				// Abilities (sonar pulse, particle wall, ...) briefly become the
				// active weapon and use ammo too, but they aren't shots.
				if ( !BH_IsTitanAbility( weapon.GetWeaponClassName() ) )
					BH_PlayTitanRecoil( weapon )
			}
			else
				BH_PlayRecoil( weapon )
		}
		else if ( clip > file.recoilClip && !player.IsTitan() )
		{
			// Magazine inserted (or one shell, for shell-by-shell reloads)
			BH_Debug( "reload: clip " + file.recoilClip + " -> " + clip )
			BH_Play( "reload_magazine_in" )
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

// ===================================================================
//  14. Titan footsteps (own Titan only)
// ===================================================================

void function BH_TitanStepWatchThread()
{
	while ( true )
	{
		WaitFrame()

		entity player = GetLocalViewPlayer()
		if ( !IsValid( player ) || !IsAlive( player ) || !player.IsTitan() )
		{
			file.lastStepCycle = -1.0
			continue
		}

		float cycle = player.GetCycle()
		float previous = file.lastStepCycle
		float now = Time()
		float dt = now - file.lastStepTime
		file.lastStepCycle = cycle
		file.lastStepTime  = now
		if ( previous < 0.0 || dt <= 0.0 )
			continue

		vector velocity = player.GetVelocity()
		float speed = sqrt( velocity.x * velocity.x + velocity.y * velocity.y )
		float vertical = velocity.z < 0.0 ? -velocity.z : velocity.z

		float walking  = BH_Clamp01( ( speed - BH_STEP_SPEED_MIN ) / ( BH_STEP_SPEED_FULL - BH_STEP_SPEED_MIN ) )
		float grounded = 1.0 - BH_Clamp01( vertical / BH_STEP_VERTICAL_MAX )
		if ( walking * grounded <= BH_STEP_BLEND_MIN || !player.IsOnGround() )
			continue

		float advance = cycle >= previous ? cycle - previous : cycle + 1.0 - previous
		if ( advance <= 0.0001 || advance > BH_STEP_MAX_ADVANCE )
			continue

		// Footfalls are at cycle 0.0 and 0.5. Trigger BH_STEP_LEAD_TIME earlier:
		// convert the time into a cycle offset using the current animation
		// speed (cycles per second), capped well below half a cycle.
		float lead = ( advance / dt ) * BH_STEP_LEAD_TIME
		if ( lead > 0.4 )
			lead = 0.4

		if ( BH_CycleCrossed( previous, cycle, 1.0 - lead ) || BH_CycleCrossed( previous, cycle, 0.5 - lead ) )
		{
			BH_Debug( "Titan footstep (cycle " + previous + " -> " + cycle + ", lead " + lead + ", speed " + speed + ")" )
			BH_Play( "titan_footstep" )
		}
	}
}

// True if the animation cycle passed "phase" between two samples (wraps at 1.0).
bool function BH_CycleCrossed( float previous, float current, float phase )
{
	if ( current >= previous )
		return previous < phase && current >= phase
	return phase > previous || phase <= current
}

float function BH_Clamp01( float value )
{
	if ( value < 0.0 )
		return 0.0
	if ( value > 1.0 )
		return 1.0
	return value
}

// ===================================================================
//  15. Titanfall (any Titan landing from a drop, incl. our own)
// ===================================================================

void function BH_OnNpcTitanCreated( entity titan )
{
	if ( !IsValid( titan ) )
		return

	BH_FallTracker t
	t.ent      = titan
	t.lastZ    = titan.GetOrigin().z
	t.lastTime = Time()
	file.titans.append( t )
	BH_Debug( "Titan tracked for titanfall (" + file.titans.len() + " tracked)" )
}

void function BH_TitanfallWatchThread()
{
	while ( true )
	{
		WaitFrame()

		entity player = GetLocalViewPlayer()
		if ( !IsValid( player ) )
			continue

		// NPC Titans (enemies, BT as a separate Titan)
		for ( int i = file.titans.len() - 1; i >= 0; i-- )
		{
			if ( !IsValid( file.titans[ i ].ent ) )
			{
				file.titans.remove( i )
				continue
			}
			if ( BH_UpdateFall( file.titans[ i ] ) )
				BH_PlayTitanfall( player, file.titans[ i ], file.titans[ i ].ent.GetOrigin() )
		}

		// Our own Titan while we are inside it
		if ( player.IsTitan() && IsAlive( player ) )
		{
			if ( !file.playerFallInit || file.playerFall.ent != player )
			{
				BH_FallTracker t
				t.ent      = player
				t.lastZ    = player.GetOrigin().z
				t.lastTime = Time()
				file.playerFall     = t
				file.playerFallInit = true
			}
			else if ( BH_UpdateFall( file.playerFall ) )
			{
				BH_PlayTitanfall( player, file.playerFall, player.GetOrigin() )
			}
		}
		else
		{
			file.playerFallInit = false
		}
	}
}

// Updates a tracker with the entity's current height. Returns true once when
// a fast fall ends abruptly (= landed).
bool function BH_UpdateFall( BH_FallTracker t )
{
	float now = Time()
	float dt = now - t.lastTime
	if ( dt <= 0.0 )
		return false

	float z = t.ent.GetOrigin().z
	float vz = ( z - t.lastZ ) / dt
	t.lastZ    = z
	t.lastTime = now

	if ( -vz > BH_TITANFALL_FALL_SPEED )
	{
		t.fallTime += dt
		if ( -vz > t.maxFallSpeed )
			t.maxFallSpeed = -vz
		return false
	}

	bool landed = t.fallTime >= BH_TITANFALL_MIN_FALL && -vz < BH_TITANFALL_STOP_SPEED
	if ( landed || -vz < BH_TITANFALL_STOP_SPEED )
	{
		if ( landed )
			BH_Debug( "Titan landed after falling " + t.fallTime + " s (max " + t.maxFallSpeed + " units/s)" )
		t.fallTime     = 0.0
		t.maxFallSpeed = 0.0
	}
	return landed
}

void function BH_PlayTitanfall( entity player, BH_FallTracker t, vector origin )
{
	bool ours = t.ent == player
	float dist = ours ? 0.0 : Distance( origin, player.GetOrigin() )
	if ( dist > BH_TITANFALL_RANGE )
	{
		BH_Debug( "Titanfall at distance " + dist + " ignored (out of range)" )
		return
	}

	float intensity = 1.0 - ( dist / BH_TITANFALL_RANGE ) * ( 1.0 - BH_TITANFALL_MIN_INTENSITY )
	float angle = ours ? 0.0 : BH_HitAngle( player, origin )
	BH_Debug( "Titanfall (" + ( ours ? "our Titan" : "distance " + dist ) + "), intensity " + intensity + ", angle " + angle )
	BH_PlayParam( "titanfall", intensity, 1.0, angle, 0.0 )
}

// ===================================================================
//  16. Titan shoulder rockets (ordnance slot)
//
//  Technique from the Titanfall2-SimHub-Motion-Addon: rocket ordnance is
//  recognized by class name. Activity is a drop in its clip or the attack
//  cooldown starting (shoulder rockets: twice per salvo). Only the first
//  activity of a salvo plays the effect.
// ===================================================================

void function BH_TitanRocketWatchThread()
{
	while ( true )
	{
		WaitFrame()

		entity player = GetLocalViewPlayer()
		if ( !IsValid( player ) || !IsAlive( player ) || !player.IsTitan() )
		{
			file.ordnanceWeapon = null
			continue
		}

		// Rocket ordnance that becomes the active weapon when fired (e.g. Tone's
		// tracker rockets) is already covered by the normal Titan recoil.
		entity ordnance = player.GetOffhandWeapon( OFFHAND_ORDNANCE )
		if ( !IsValid( ordnance ) || !BH_IsTitanRocketWeapon( ordnance.GetWeaponClassName() )
			|| ordnance == player.GetActiveWeapon() )
		{
			file.ordnanceWeapon = null
			continue
		}

		int   clip     = ordnance.GetWeaponPrimaryClipCount()
		float cooldown = ordnance.GetNextAttackAllowedTime() - Time()
		if ( cooldown < 0.0 )
			cooldown = 0.0

		if ( ordnance != file.ordnanceWeapon )
		{
			// New Titan or loadout: baseline only.
			file.ordnanceWeapon   = ordnance
			file.ordnanceClip     = clip
			file.ordnanceCooldown = cooldown
			BH_Debug( "Titan rockets: " + ordnance.GetWeaponClassName() + " clip=" + clip )
			continue
		}

		string detected = ""
		if ( clip < file.ordnanceClip )
			detected = "clip " + file.ordnanceClip + " -> " + clip
		else if ( file.ordnanceCooldown < 0.05 && cooldown > 0.2 && clip == file.ordnanceClip )
			detected = "cooldown started: " + cooldown + " s"

		if ( detected != "" )
		{
			// Only the first detection of a salvo plays; the pattern in the
			// bHaptics portal covers the whole salvo.
			bool newSalvo = Time() - file.lastRocketActivity > BH_ROCKET_SALVO_QUIET
			file.lastRocketActivity = Time()
			BH_Debug( "Titan rocket activity (" + detected + ")" + ( newSalvo ? ", new salvo" : ", same salvo, skipped" ) )
			if ( newSalvo )
				BH_Play( "recoil_titan_rockets" )
		}

		file.ordnanceClip     = clip
		file.ordnanceCooldown = cooldown
	}
}

bool function BH_IsTitanRocketWeapon( string weaponClass )
{
	switch ( weaponClass )
	{
		// not "mp_titanweapon_tracker_rockets": fired as the active weapon,
		// so the normal Titan recoil handles it
		case "mp_titanweapon_shoulder_rockets":
		case "mp_titanweapon_homing_rockets":
		case "mp_titanweapon_salvo_rockets":
		case "mp_titanweapon_dumbfire_rockets":
			return true
	}
	return false
}

// ===================================================================
//  Timeshift: timeline switch
//
//  Plays "timeshift" whenever the player jumps between past and present,
//  for manual jumps (after the device's wind-up) as well as scripted ones.
//  The debug log shows the jump's offset: its height (z) sign tells which
//  timeline the player arrived in, for separate past/present effects later.
// ===================================================================

void function BH_TimeshiftWatchThread()
{
	BH_Debug( "timeshift: watching for timeline switches" )

	entity lastPlayer = null
	vector lastOrigin = < 0, 0, 0 >

	while ( true )
	{
		WaitFrame()

		entity player = GetLocalClientPlayer()
		if ( !IsValid( player ) || !IsAlive( player ) )
		{
			lastPlayer = null // respawn: start a new baseline
			continue
		}

		vector origin = player.GetOrigin()
		if ( player == lastPlayer )
		{
			vector delta = origin - lastOrigin
			if ( Length( delta ) > BH_TIMESHIFT_MIN_JUMP )
			{
				BH_Debug( "timeshift: timeline switch, offset x " + delta.x + " y " + delta.y + " z " + delta.z )
				BH_Play( "timeshift" )
			}
		}

		lastPlayer = player
		lastOrigin = origin
	}
}

// ===================================================================
//  VR mod interactions: drawing guns, grenades, reloading
//
//  CircuitLord's VR mod handles these gestures in its native code and talks
//  to its scripts through convars and client commands:
//   - Gun drawn from a holster: the server acks "tf2vr_draw" by setting
//     tf2vr_gun_ack to the request's ticket (> 0) and tf2vr_gun_slot to the
//     slot: 0 = primary (over the shoulder), 1 = secondary, 3 = arc tool (hip).
//     Picking up a gun class you already carry acks the same way.
//   - Grenade in hand: tf2vr_grenade_ack gets a new ticket (> 0), for chest
//     grabs and for grenades picked up from the world.
//   - Grenade put back on the chest: server only, see tf2vr_bh_server.nut.
//  These are read here on the client because a server script's callbacks
//  only exist if the level was started with this mod installed: continuing
//  from a save restores the server script state of that save.
//   - Grenade thrown: tf2vr_projectile_ticket changes.
//   - Reload: tf2vr_reload_magazine_out, tf2vr_reload_ejections and
//     tf2vr_reload_rounds count steps (magazine pulled out, ejected, rounds
//     thrown out by racking). Magazine in = clip goes up, see the recoil watcher.
//  Holstering a gun is not exposed by the VR mod (yet).
// ===================================================================

void function BH_InteractionWatchThread()
{
	file.gunAck             = GetConVarInt( "tf2vr_gun_ack" )
	file.grenadeAck         = GetConVarInt( "tf2vr_grenade_ack" )
	file.grenadeThrowTicket = GetConVarInt( "tf2vr_projectile_ticket" )
	file.reloadMagazineOut  = GetConVarInt( "tf2vr_reload_magazine_out" )
	file.reloadEjections    = GetConVarInt( "tf2vr_reload_ejections" )
	file.reloadRounds       = GetConVarInt( "tf2vr_reload_rounds" )

	while ( true )
	{
		WaitFrame()

		int gunAck = GetConVarInt( "tf2vr_gun_ack" )
		if ( gunAck != file.gunAck )
		{
			file.gunAck = gunAck
			if ( gunAck > 0 )
			{
				int slot = GetConVarInt( "tf2vr_gun_slot" )
				string side = TF2VR_WeaponHand() == 0 ? "l" : "r"
				BH_Debug( "draw: slot " + slot + " (" + GetConVarString( "tf2vr_gun_name" ) + ")" )
				BH_Play( slot == 0 ? "draw_primary_" + side : "draw_secondary_" + side )
			}
		}

		int grenadeAck = GetConVarInt( "tf2vr_grenade_ack" )
		if ( grenadeAck != file.grenadeAck )
		{
			file.grenadeAck = grenadeAck
			if ( grenadeAck > 0 )
			{
				BH_Debug( "grenade: in hand (ticket " + grenadeAck + ")" )
				BH_Play( "grenade_take" )
			}
		}

		int ticket = GetConVarInt( "tf2vr_projectile_ticket" )
		if ( ticket != file.grenadeThrowTicket )
		{
			file.grenadeThrowTicket = ticket
			if ( ticket != 0 )
			{
				BH_Debug( "grenade: thrown (ticket " + ticket + ")" )
				BH_Play( "grenade_throw" )
			}
		}

		int magazineOut = GetConVarInt( "tf2vr_reload_magazine_out" )
		if ( magazineOut > file.reloadMagazineOut )
		{
			BH_Debug( "reload: magazine out (" + magazineOut + ")" )
			BH_Play( "reload_magazine_out" )
		}
		file.reloadMagazineOut = magazineOut

		// Meaning not fully known yet: logged only, to see when it counts up.
		int ejections = GetConVarInt( "tf2vr_reload_ejections" )
		if ( ejections > file.reloadEjections )
			BH_Debug( "reload: ejection (" + ejections + ")" )
		file.reloadEjections = ejections

		int rounds = GetConVarInt( "tf2vr_reload_rounds" )
		if ( rounds > file.reloadRounds )
		{
			BH_Debug( "reload: rack, rounds thrown out (" + rounds + ")" )
			BH_Play( "reload_rack" )
		}
		file.reloadRounds = rounds
	}
}

// ===================================================================
//  Titan abilities: VTOL hover, Flight Core, core charge
//
//  - VTOL hover (mp_titanability_hover, Northstar loadout): about 3 s of
//    hovering with gravity off (server function FlyerHovers). Detected when
//    the ability is used (its ammo drops or its cooldown starts), like the
//    shoulder rockets. Plays "titan_hover" once; the pattern can be ~3 s long.
//  - Flight Core: while it runs, the Titan's active weapon is
//    mp_titanweapon_flightcore_rockets. "titan_flight" repeats meanwhile.
//  - Core: "titan_core_ready" when the core becomes available,
//    "titan_core_charge" repeatedly during the activation wind-up (the core
//    weapon is a charge weapon; intensity follows the charge), and
//    "titan_core_active" when the core starts.
//  - Shield: "titan_shield_start" when a defensive shield is used: vortex
//    (incl. Ion's), Scorch's heat shield, Tone's particle wall or Legion's gun
//    shield. Detected like the hover: the ability becomes the active weapon,
//    its ammo drops or its cooldown starts (max. once per second).
// ===================================================================

void function BH_TitanAbilityWatchThread()
{
	entity hoverWeapon      = null
	int    hoverClip        = -1
	float  hoverCooldown    = 0.0
	bool   flying           = false
	float  nextFlight       = 0.0
	bool   coreReady        = true  // no "ready" right after embarking with a full core
	bool   coreActive       = false
	float  nextCoreCharge   = 0.0
	bool   wasTitan         = false
	bool   shieldUp         = false
	entity shieldWeapon     = null
	int    shieldClip       = -1
	float  shieldCooldown   = 0.0
	float  lastShield       = -999.0

	while ( true )
	{
		WaitFrame()

		entity player = GetLocalClientPlayer()
		if ( !IsValid( player ) || !IsAlive( player ) || !player.IsTitan() )
		{
			hoverWeapon = null
			flying      = false
			wasTitan    = false
			shieldUp     = false
			shieldWeapon = null
			continue
		}

		float now = Time()
		entity soul = player.GetTitanSoul()

		// ---- Core: ready / wind-up / active ----
		if ( IsValid( soul ) )
		{
			bool ready = IsCoreChargeAvailable( player, soul )
			if ( ready && !coreReady && wasTitan )
			{
				BH_Debug( "titan: core ready" )
				BH_Play( "titan_core_ready" )
			}
			coreReady = ready

			bool coreNowActive = SoulTitanCore_GetExpireTime( soul ) > now
			if ( coreNowActive && !coreActive )
			{
				BH_Debug( "titan: core active" )
				BH_Play( "titan_core_active" )
			}
			coreActive = coreNowActive
		}

		entity coreWeapon = player.GetOffhandWeapon( OFFHAND_EQUIPMENT )
		if ( IsValid( coreWeapon ) )
		{
			float charge = coreWeapon.GetWeaponChargeFraction()
			if ( charge > 0.0 && charge < 1.0 && now >= nextCoreCharge )
			{
				BH_PlayParam( "titan_core_charge", 0.3 + 0.7 * charge, 1.0, 0.0, 0.0 )
				nextCoreCharge = now + BH_CORE_CHARGE_INTERVAL
			}
		}

		// ---- Flight Core ----
		entity activeWeapon = player.GetActiveWeapon()
		bool flightCore = IsValid( activeWeapon ) && activeWeapon.GetWeaponClassName() == "mp_titanweapon_flightcore_rockets"
		if ( flightCore && ( !flying || now >= nextFlight ) )
		{
			if ( !flying )
				BH_Debug( "titan: Flight Core flying" )
			BH_Play( "titan_flight" )
			nextFlight = now + BH_FLIGHT_INTERVAL
		}
		flying = flightCore

		// ---- Shields: vortex, heat shield, particle wall, gun shield ----
		entity shieldOffhand = null
		foreach ( entity offhand in player.GetOffhandWeapons() )
		{
			if ( IsValid( offhand ) && BH_IsTitanShieldWeapon( offhand.GetWeaponClassName() ) )
				shieldOffhand = offhand
		}

		bool shieldUsed = false
		bool shieldActive = IsValid( activeWeapon ) && BH_IsTitanShieldWeapon( activeWeapon.GetWeaponClassName() )
		if ( shieldActive && !shieldUp )
			shieldUsed = true
		shieldUp = shieldActive

		if ( shieldOffhand == null )
		{
			shieldWeapon = null
		}
		else
		{
			int   clip     = shieldOffhand.GetWeaponPrimaryClipCount()
			float cooldown = shieldOffhand.GetNextAttackAllowedTime() - now
			if ( cooldown < 0.0 )
				cooldown = 0.0

			if ( shieldOffhand == shieldWeapon )
			{
				if ( shieldClip > 0 && clip >= 0 && clip < shieldClip )
					shieldUsed = true
				if ( clip == shieldClip && shieldCooldown < 0.05 && cooldown > 0.2 )
					shieldUsed = true
			}

			shieldWeapon   = shieldOffhand
			shieldClip     = clip
			shieldCooldown = cooldown
		}

		if ( shieldUsed && now - lastShield > BH_SHIELD_DEDUPE )
		{
			BH_Debug( "titan: shield (" + ( IsValid( shieldOffhand ) ? shieldOffhand.GetWeaponClassName() : "?" ) + ")" )
			BH_Play( "titan_shield_start" )
			lastShield = now
		}

		// ---- VTOL hover ----
		entity hover = null
		foreach ( entity offhand in player.GetOffhandWeapons() )
		{
			if ( IsValid( offhand ) && offhand.GetWeaponClassName() == "mp_titanability_hover" )
				hover = offhand
		}

		if ( hover == null )
		{
			hoverWeapon = null
		}
		else
		{
			int   clip     = hover.GetWeaponPrimaryClipCount()
			float cooldown = hover.GetNextAttackAllowedTime() - now
			if ( cooldown < 0.0 )
				cooldown = 0.0

			if ( hover == hoverWeapon )
			{
				bool spent   = hoverClip > 0 && clip >= 0 && clip < hoverClip
				bool started = clip == hoverClip && hoverCooldown < 0.05 && cooldown > 0.2
				if ( spent || started )
				{
					BH_Debug( "titan: VTOL hover (clip " + hoverClip + " -> " + clip + ", cooldown " + cooldown + ")" )
					BH_Play( "titan_hover" )
				}
			}

			hoverWeapon   = hover
			hoverClip     = clip
			hoverCooldown = cooldown
		}

		wasTitan = true
	}
}

bool function BH_IsTitanShieldWeapon( string weaponClass )
{
	switch ( weaponClass )
	{
		case "mp_titanweapon_vortex_shield":
		case "mp_titanweapon_vortex_shield_ion":
		case "mp_titanweapon_heat_shield":
		case "mp_titanability_particle_wall":
		case "mp_titanability_gun_shield":
			return true
	}
	return false
}

// Titan abilities (sonar pulse, particle wall, hover, ...) are not guns.
bool function BH_IsTitanAbility( string weaponClass )
{
	return weaponClass.len() > 16 && weaponClass.slice( 0, 16 ) == "mp_titanability_"
}

#endif
