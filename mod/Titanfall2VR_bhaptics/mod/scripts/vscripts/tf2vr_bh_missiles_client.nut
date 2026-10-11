// DISABLED in v0.5.1 (not listed in mod.json, and the code below is
// commented out): suspected cause of a game freeze at the start of the Kane
// arena (right after Kane's "I'm coming down"), seen only with v0.5.0.
//
// Projectile creation hook for the bHaptics explosion tracking.
//
// ClientCodeCallback_OnMissileCreation is called by the engine for every
// projectile (rockets, grenades, ...). In the game's own scripts it is
// commented out, i.e. nobody defines it, so we can. To re-enable: uncomment
// the block below AND add this file back to mod.json:
//
//     {
//         "Path": "tf2vr_bh_missiles_client.nut",
//         "RunOn": "CLIENT && SP"
//     },

/*
#if TF2VR_BHAPTICS

global function ClientCodeCallback_OnMissileCreation

void function ClientCodeCallback_OnMissileCreation( entity missileEnt, string weaponName, bool firstTime )
{
	BH_TrackProjectile( missileEnt, weaponName )
}

#endif
*/
