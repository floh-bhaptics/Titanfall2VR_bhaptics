// Projectile creation hook for the bHaptics explosion tracking.
//
// ClientCodeCallback_OnMissileCreation is called by the engine for every
// projectile (rockets, grenades, ...). In the game's own scripts it is
// commented out, i.e. nobody defines it, so we can. If the campaign scripts
// DO define it, the game reports a CLIENT script COMPILE ERROR about a
// duplicate function in this file: remove this file's entry from mod.json
// and the rest of the mod keeps working (explosions then come only from
// explosive damage on the player).

#if TF2VR_BHAPTICS

global function ClientCodeCallback_OnMissileCreation

void function ClientCodeCallback_OnMissileCreation( entity missileEnt, string weaponName, bool firstTime )
{
	BH_TrackProjectile( missileEnt, weaponName )
}

#endif
