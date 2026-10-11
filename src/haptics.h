#pragma once
//
// haptics.h - game-side bHaptics glue: connection lifecycle, startup
// heartbeat and event playback.
//
// All bHaptics SDK calls run on a worker thread; every function here returns
// immediately, so a slow or hanging bHaptics Player can't freeze the game.
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace haptics
{
    // Starts the worker thread, which loads bhaptics_library.dll (from
    // <plugin dir>\lib\) and registers the app with the bHaptics Player.
    void Startup(HMODULE self);

    // Call once per host frame (game thread). Only a watchdog that logs if
    // the worker hangs in an SDK call.
    void Frame();

    // Stops all haptics, closes the Player connection and ends the worker.
    // Returns false if the worker didn't stop in time; the plugin must then
    // stay loaded (the thread still runs code from this DLL).
    bool Shutdown();

    // True if the bHaptics Player is connected (cached, no SDK call).
    bool IsConnected();

    // Drops queued events and stops everything playing (e.g. on level unload).
    void StopAll();

    // Queues a workspace event for playback. The name is lower-cased before it
    // goes to the SDK (bHaptics event names are all lower case), so CamelCase
    // like "RecoilVest_R" can be used in code. Returns false if not queued.
    //   intensity / duration: multipliers, 1.0 = as designed
    //   angleX:  rotation around the body in degrees (0-360), vest events
    //   offsetY: vertical shift (-0.5 .. 0.5), vest events
    bool PlaybackHaptics(const char* eventName,
                         float intensity = 1.0f,
                         float duration = 1.0f,
                         float angleX = 0.0f,
                         float offsetY = 0.0f);
}
