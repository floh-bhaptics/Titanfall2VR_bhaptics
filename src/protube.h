#pragma once
//
// protube.h - ProTube / ProVolver force feedback (ForceTubeVR_API_x64.dll).
//
// Optional: if <plugins>\lib\ForceTubeVR_API_x64.dll is missing, everything
// here is a no-op. Like haptics.h, all SDK calls run on a worker thread, so
// Bluetooth trouble can never freeze the game.
//
// Recoil power values follow PurplePeanut00's titanfall2-vr-provolver bridge:
//   - "full" kicks (actual shotguns) always use 255.
//   - Other kicks use the SDK's TempoToKickPower() with a recovery time
//     adapted to the observed fire rate (50-130 ms), so fast automatic fire
//     gets weaker kicks the device can keep up with.
//   - A kick arriving before the previous one recovered waits up to 40 ms,
//     otherwise it is dropped. Newer kicks replace waiting ones.
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace protube
{
    // Loads the DLL (if present) on a worker thread and connects the device.
    void Startup(HMODULE self);

    // Stops rumble and ends the worker. Returns false if the worker didn't stop
    // in time; the plugin must then stay loaded. The ProTube DLL itself is never
    // unloaded (its own threads run until the game exits).
    bool Shutdown();

    // True if ForceTubeVR_API_x64.dll was found (device may still be off).
    bool IsAvailable();

    // Hands: "right", "left", or anything else for "no specific hand".
    // With two devices, right = pistol1 / right.pro, left = pistol2 / left.pro
    // (see protube.cpp). With one device, every effect goes to it.

    // One recoil kick.
    //   weapon: weapon class name, resets the fire-rate adaptation on change
    //   group:  "full"  - always 255 (shotguns)
    //           "heavy" - adaptive, starts with 130 ms recovery
    //           other   - adaptive, starts with 60 ms recovery
    //   hand:   the hand holding the weapon
    void Kick(const char* weapon, const char* group, const char* hand);

    // Charge-up rumble, fraction 0..1. Each call rumbles for 100 ms, so the
    // script must keep calling (it does, every 50 ms). fraction <= 0 stops.
    void Charge(float fraction, const char* hand);

    // One rumble pulse on all devices (Titan weapons instead of a kick).
    //   power:   0..1 (below 0.5 only the small motor runs)
    //   seconds: clamped to 0.03 .. 2.0 (0 would mean endless in the SDK)
    void Rumble(float power, float seconds);

    // Stops rumble and drops waiting kicks (death, level unload).
    void Stop();
}
