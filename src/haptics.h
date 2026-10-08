#pragma once
//
// haptics.h - game-side bHaptics glue: connection lifecycle and the startup
// heartbeat. Later this is where the BH_* Squirrel natives will land.
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace haptics
{
    // Loads bhaptics_library.dll (from <plugin dir>\lib\) and registers the app
    // with the bHaptics Player. Non-blocking: the connection completes in the
    // background and is picked up by Frame().
    void Startup(HMODULE self);

    // Call once per host frame (game thread). Cheap once startup is done.
    void Frame();

    // Stops all haptics and closes the Player connection.
    void Shutdown();

    // True if the bHaptics Player is connected.
    bool IsConnected();

    // Stops everything currently playing (e.g. on level unload).
    void StopAll();

    // Plays a workspace event. The name is lower-cased before it goes to the
    // SDK (bHaptics event names are all lower case), so CamelCase like
    // "RecoilVest_R" can be used in code. Returns the request id (0 = not played).
    //   intensity / duration: multipliers, 1.0 = as designed
    //   angleX:  rotation around the body in degrees (0-360), vest events
    //   offsetY: vertical shift (-0.5 .. 0.5), vest events
    int PlaybackHaptics(const char* eventName,
                        float intensity = 1.0f,
                        float duration = 1.0f,
                        float angleX = 0.0f,
                        float offsetY = 0.0f);
}
