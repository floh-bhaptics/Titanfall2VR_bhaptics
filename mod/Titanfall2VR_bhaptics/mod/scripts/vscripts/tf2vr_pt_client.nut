// tf2vr_pt_client.nut - ProTube / ProVolver force feedback (client, campaign only).
//
// Shot and charge detection ported from PurplePeanut00's titanfall2-vr-provolver
// (pvbridge_client.nut 0.3.3, https://github.com/PurplePeanut00/titanfall2-vr-provolver):
//
//   MIT License, Copyright (c) 2026 Titanfall 2 VR ProVolver Bridge contributors
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// Changes for this mod: the log-file protocol ([PVBRIDGE/1] print lines read by
// an external bridge program) is replaced by direct calls into the plugin:
//   PT_Kick( weaponClass, group )  group "full" = 255 (shotguns), "heavy" = slow
//                                  start, others adaptive (see src/protube.h)
//   PT_Charge( fraction )          100 ms rumble per call, <= 0 stops
// The plugin owns the device and the kick-power scheduling.
//
// This file is independent of the bHaptics scripts: to remove ProTube support,
// delete it and its entry in mod.json.

global function TF2VR_PT_ClientInit

struct
{
	bool started = false
	bool chargeActive = false
	float nextChargeSample = 0.0
} file

void function TF2VR_PT_ClientInit()
{
	if ( file.started )
		return
	file.started = true

	// No ForceTubeVR_API_x64.dll in plugins\lib: nothing to do.
	if ( !PT_IsAvailable() )
		return

	BH_Debug( "ProTube: client script started" )
	thread PT_Watch()
}

// "full": actual shotguns, always maximum kick.
// "heavy": snipers, launchers, ... start with the long 130 ms recovery.
// Everything else starts at 60 ms; the plugin adapts to the observed fire rate.
string function PT_Group( string name )
{
	array<string> full = [ "mp_weapon_shotgun", "mp_weapon_mastiff",
		"mp_weapon_shotgun_pistol", "mp_weapon_shotgun_doublebarrel" ]
	foreach ( string item in full )
		if ( name == item )
			return "full"
	array<string> heavy = [ "mp_weapon_sniper", "mp_weapon_doubletake", "mp_weapon_dmr",
		"mp_weapon_epg", "mp_weapon_smr", "mp_weapon_softball",
		"mp_weapon_pulse_lmg", "mp_weapon_rocket_launcher", "mp_weapon_arc_launcher",
		"mp_weapon_mgl", "mp_weapon_defender" ]
	foreach ( string item in heavy )
		if ( name == item )
			return "heavy"
	array<string> light = [ "mp_weapon_semipistol", "mp_weapon_autopistol",
		"mp_weapon_wingman", "mp_weapon_wingman_n", "mp_weapon_smart_pistol",
		"mp_weapon_gibber_pistol", "mp_weapon_alternator_smg", "mp_weapon_car",
		"mp_weapon_r97", "mp_weapon_hemlok_smg", "sp_weapon_arc_tool" ]
	foreach ( string item in light )
		if ( name == item )
			return "pistol"
	return "rifle"
}

bool function PT_RocketOrdnance( string name )
{
	return name == "mp_titanweapon_shoulder_rockets"
		|| name == "mp_titanweapon_homing_rockets"
		|| name == "mp_titanweapon_salvo_rockets"
		|| name == "mp_titanweapon_dumbfire_rockets"
}

void function PT_Emit( string group, entity weapon )
{
	PT_Kick( weapon.GetWeaponClassName(), group )
}

// Only known primary charge weapons: shields/heat weapons use this API too,
// but must not produce a charging effect merely because they hold a resource.
void function PT_StopCharge()
{
	if ( file.chargeActive )
		PT_Charge( 0.0 )
	file.chargeActive = false
	file.nextChargeSample = 0.0
}

void function PT_UpdateCharge( entity player, entity weapon, bool fired )
{
	string name = weapon.GetWeaponClassName()
	bool held = false
	if ( name == "mp_titanweapon_sniper" )
		held = weapon.IsWeaponInAds()
	else if ( name == "mp_weapon_defender" )
		held = player.IsInputCommandHeld( IN_ATTACK )
	else
	{
		PT_StopCharge()
		return
	}
	float fraction = weapon.GetWeaponChargeFraction()
	if ( fired || !held || fraction <= 0.0 )
	{
		PT_StopCharge()
		return
	}
	if ( fraction > 1.0 )
		fraction = 1.0
	// Each PT_Charge rumbles 100 ms; refresh every 50 ms while charging.
	if ( !file.chargeActive || Time() >= file.nextChargeSample )
	{
		PT_Charge( fraction )
		file.chargeActive = true
		file.nextChargeSample = Time() + 0.05
	}
}

void function PT_Watch()
{
	entity owner = null
	entity primary = null
	entity secondary = null
	table<entity, int> clips = {}
	table<entity, float> attacks = {}
	table<entity, float> charges = {}
	int secondaryAmmo = -1
	float secondaryDelay = 0.0
	float lastRocket = -100.0
	float lastFrame = -1.0
	while ( true )
	{
		WaitFrame()
		float now = Time()
		entity currentOwner = GetLocalClientPlayer()
		// Rebaseline after respawn, player replacement or a long simulation gap.
		bool reset = currentOwner != owner || lastFrame < 0.0 || now < lastFrame || now - lastFrame > 0.25
		lastFrame = now
		owner = currentOwner
		if ( !IsValid( owner ) || !IsAlive( owner ) )
		{
			PT_StopCharge()
			primary = null
			clips.clear()
			attacks.clear()
			charges.clear()
			secondary = null
			continue
		}
		if ( reset )
		{
			PT_StopCharge()
			primary = null
			clips.clear()
			attacks.clear()
			charges.clear()
			secondary = null
		}
		entity active = owner.GetActiveWeapon()
		if ( IsValid( active ) )
		{
			if ( active != primary )
				PT_StopCharge()
			int ammo = active.GetWeaponPrimaryClipCount()
			// One observed discharge per frame; ammo consumption is not a shot count.
			bool fired = false
			if ( active in clips )
			{
				fired = clips[ active ] > 0 && ammo >= 0 && ammo < clips[ active ]
				// Charge discharge can use an energy/cooldown transition without a clip drop.
				// A charge disappearing alone is cancellation, never a shot.
				string name = active.GetWeaponClassName()
				bool chargeWeapon = name == "mp_weapon_defender" || name == "mp_titanweapon_sniper"
				if ( !fired && chargeWeapon && owner.IsInputCommandHeld( IN_ATTACK ) )
				{
					float attack = active.GetNextAttackAllowedTime()
					fired = charges[ active ] > 0.0 && active.GetWeaponChargeFraction() <= 0.0
						&& attack > attacks[ active ] + 0.001 && attack > now
				}
			}
			if ( fired )
				PT_Emit( owner.IsTitan() ? "titan" : PT_Group( active.GetWeaponClassName() ), active )
			PT_UpdateCharge( owner, active, fired )
			primary = active
		}
		else
		{
			PT_StopCharge()
			primary = null
		}

		// Snapshot the whole inventory, including holstered guns. A switch and first
		// discharge in the same frame still compares against that gun's prior clip.
		table<entity, int> nextClips = {}
		table<entity, float> nextAttacks = {}
		table<entity, float> nextCharges = {}
		array<entity> inventory = owner.GetMainWeapons()
		if ( IsValid( active ) && !inventory.contains( active ) )
			inventory.append( active )
		foreach ( entity weapon in inventory )
		{
			if ( !IsValid( weapon ) )
				continue
			nextClips[ weapon ] <- weapon.GetWeaponPrimaryClipCount()
			nextAttacks[ weapon ] <- weapon.GetNextAttackAllowedTime()
			nextCharges[ weapon ] <- weapon.GetWeaponChargeFraction()
		}
		clips = nextClips
		attacks = nextAttacks
		charges = nextCharges

		// Titan shoulder rockets: one kick at the start of each salvo.
		if ( !owner.IsTitan() )
		{
			secondary = null
			continue
		}
		entity ordnance = owner.GetOffhandWeapon( OFFHAND_ORDNANCE )
		if ( !IsValid( ordnance ) || ordnance == active || !PT_RocketOrdnance( ordnance.GetWeaponClassName() ) )
		{
			secondary = null
			continue
		}
		int ammo = ordnance.GetWeaponPrimaryClipCount()
		float delay = ordnance.GetNextAttackAllowedTime() - now
		if ( delay < 0.0 )
			delay = 0.0
		if ( ordnance != secondary )
			lastRocket = -100.0
		else
		{
			bool spent = secondaryAmmo > 0 && ammo >= 0 && ammo < secondaryAmmo
			bool cooldownBegan = ammo == secondaryAmmo && secondaryDelay < 0.05 && delay > 0.2
			if ( spent || cooldownBegan )
			{
				if ( now - lastRocket > 3.0 )
					PT_Emit( "titan_rockets", ordnance )
				lastRocket = now
			}
		}
		secondary = ordnance
		secondaryAmmo = ammo
		secondaryDelay = delay
	}
}
