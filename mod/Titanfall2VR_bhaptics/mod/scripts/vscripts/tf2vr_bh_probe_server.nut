// RECOIL PROBE (server) - temporary test script.
//
// Tests whether AddCallback_OnWeaponAttack fires in the campaign. If this
// hook doesn't exist there, the game reports a SERVER script COMPILE ERROR
// in this file: remove its entry from mod.json and the rest keeps working.
//
// All log lines start with [RECOIL-PROBE] for easy searching.

global function TF2VR_BH_ProbeServerInit

void function TF2VR_BH_ProbeServerInit()
{
#if TF2VR_BHAPTICS
	AddCallback_OnWeaponAttack( BH_Probe_OnWeaponAttack )
	BH_Debug( "[RECOIL-PROBE] SERVER hook AddCallback_OnWeaponAttack registered" )
#endif
}

#if TF2VR_BHAPTICS

void function BH_Probe_OnWeaponAttack( entity player, entity weapon, string weaponName, int attackIndex )
{
	if ( !IsValid( player ) || !player.IsPlayer() )
		return

	BH_Debug( "[RECOIL-PROBE] SERVER OnWeaponAttack weapon=" + weaponName + " attackIndex=" + attackIndex + " titan=" + player.IsTitan() + " time=" + Time() )

	// Hand the event to the client, where the VR mod's functions live.
	ServerToClientStringCommand( player, "BH_ProbeWeaponFired " + weaponName )
}

#endif
