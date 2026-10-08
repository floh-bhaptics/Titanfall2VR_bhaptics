// RECOIL PROBE (client) - temporary test script.
//
// Tests CircuitLord's TF2VR_WeaponHand() native. Its signature is a guess
// (no arguments). If the guess is wrong, the game reports a CLIENT script
// COMPILE ERROR in this file that names the expected parameters: send me
// that line, and remove this file's entry from mod.json to keep playing.
//
// Called only when the server reports a shot, never at load time, so a
// misbehaving native can't break level loading.
//
// All log lines start with [RECOIL-PROBE] for easy searching.

global function TF2VR_BH_ProbeClientInit

void function TF2VR_BH_ProbeClientInit()
{
#if TF2VR_BHAPTICS
	AddServerToClientStringCommandCallback( "BH_ProbeWeaponFired", BH_Probe_OnWeaponFired )
	BH_Debug( "[RECOIL-PROBE] CLIENT probe ready" )
#endif
}

#if TF2VR_BHAPTICS

void function BH_Probe_OnWeaponFired( array<string> args )
{
	string weaponName = args.len() > 0 ? args[ 0 ] : "?"

	// var: we don't know the return type yet, the log shows it.
	var hand = TF2VR_WeaponHand()

	BH_Debug( "[RECOIL-PROBE] CLIENT shot weapon=" + weaponName
		+ " TF2VR_WeaponHand()=" + hand + " (" + typeof( hand ) + ")"
		+ " tf2vr_gun_name=" + GetConVarString( "tf2vr_gun_name" )
		+ " tf2vr_gun_slot=" + GetConVarInt( "tf2vr_gun_slot" )
		+ " args=" + args.len() )
}

#endif
