#pragma once
//
// squirrel.h - registers the BH_* and PT_* native functions in the game's Squirrel VMs.
//
// Natives available to scripts (CLIENT and SERVER VM):
//
//   void BH_Play( string eventName )
//   void BH_PlayParam( string eventName, float intensity, float duration, float angleX, float offsetY )
//   bool BH_IsConnected()
//   void BH_Debug( string message )    // plugin log, Debug level
//   void BH_Warn( string message )     // plugin log, Warn level
//
//   void PT_Kick( string weaponClass, string group, string hand )  // ProTube recoil, see protube.h
//   void PT_Charge( float fraction, string hand )   // ProTube charge rumble, 100 ms per call
//   void PT_Rumble( float power, float seconds )      // ProTube rumble pulse (Titan weapons)
//   void PT_Stop()
//   bool PT_IsAvailable()                             // ProTube DLL present
//
// Event names are lower-cased by haptics::PlaybackHaptics, so scripts can use CamelCase.
//

#include "northstar.h"

namespace squirrel
{
    // Call from IPluginCallbacks::OnSqvmCreated. Registers the natives in CLIENT
    // and SERVER VMs; the UI VM is left alone.
    void OnVmCreated(ns::CSquirrelVM* vm);

    // Call from IPluginCallbacks::OnSqvmDestroying.
    void OnVmDestroying(ns::CSquirrelVM* vm);
}
