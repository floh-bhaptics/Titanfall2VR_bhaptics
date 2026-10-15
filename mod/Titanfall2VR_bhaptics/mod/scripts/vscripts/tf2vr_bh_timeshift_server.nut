untyped

// tf2vr_bh_timeshift_server.nut - timeshift device in "Effect and Cause" (server, campaign).
//
// The timeshift device is the ability weapon mp_ability_timeshift. Its script
// (weapons/mp_ability_timeshift.nut, compiled in all VMs) calls every callback
// registered with AddCallback_OnTimeShiftAbilityUsed( callbackFunc ) when the
// player uses it, i.e. at the START of the ~0.5 s wind-up before the jump.
//
// That function is untyped and stores the callback in an untyped array, so a
// typed function can't be passed to it (the compiler rejects typed functionrefs
// for untyped parameters). Hence this separate "untyped" file with an untyped
// callback. The callback must have exactly one parameter named "player"
// (the game checks this with AssertParameters).
//
// The jump itself (manual and scripted ones) is detected on the client from the
// player's position, see BH_TimeshiftWatchThread in tf2vr_bh_client.nut.

global function TF2VR_BH_TimeshiftServerInit

void function TF2VR_BH_TimeshiftServerInit()
{
#if TF2VR_BHAPTICS
	string map = GetMapName()
	if ( map != "sp_hub_timeshift" && map != "sp_timeshift_spoke02" )
		return

	AddCallback_OnTimeShiftAbilityUsed( BH_OnTimeShiftAbilityUsed )
	BH_Debug( "timeshift: device callback registered on " + map )
#endif
}

#if TF2VR_BHAPTICS
function BH_OnTimeShiftAbilityUsed( player )
{
	BH_Debug( "timeshift: device used" )
	BH_Play( "timeshift_charge" )
}
#endif
