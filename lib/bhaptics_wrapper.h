#pragma once
//
// bhaptics_wrapper.h - thin, dependency-free wrapper around bhaptics_library.dll (SDK2)
//
// The bHaptics DLL is loaded at runtime with LoadLibrary/GetProcAddress, so no import
// library or SDK header is needed to build anything that uses this wrapper.
//
// Usage:
//   bh::Initialize("<workspace id>", "<sdk api key>");
//   bh::Play("RecoilVest_R");
//   bh::PlayParam("Impact", 1.0f, 1.0f, 90.0f, 0.2f);
//   bh::Shutdown();
//
// Every function is safe to call when the wrapper is not initialized or the DLL is
// missing: it simply does nothing and returns a neutral value (0 / false).
//

#include <cstdint>

namespace bh
{
    // Result of Initialize(). Only DllNotFound and ExportMissing are hard failures;
    // NotConnected means the DLL is loaded and the app was registered, but the
    // bHaptics Player is not (yet) reachable. Play calls are then silently dropped
    // by the bHaptics library until the Player connects.
    enum class InitStatus
    {
        Ok,
        AlreadyInitialized,
        NotConnected,
        DllNotFound,
        ExportMissing,
    };

    // Device positions for IsDeviceConnected().
    // Values follow the bHaptics SDK2 PositionType enum. Verify against the SDK if
    // a device reports wrongly.
    enum class Position : int
    {
        Vest = 0,
        ForearmL = 1,
        ForearmR = 2,
        Head = 3,
        HandL = 4,
        HandR = 5,
        FootL = 6,
        FootR = 7,
        GloveL = 8,
        GloveR = 9,
    };

    // Loads bhaptics_library.dll and registers the app with the bHaptics Player.
    //
    // workspaceId / sdkApiKey: from the bHaptics developer portal.
    // defaultConfigJson:       optional fallback haptic config (may be empty).
    // dllPath:                 full path to bhaptics_library.dll. If nullptr, the DLL is
    //                          expected next to the module (DLL/EXE) this wrapper is
    //                          linked into.
    InitStatus Initialize(const char* workspaceId,
                          const char* sdkApiKey,
                          const char* defaultConfigJson = "",
                          const wchar_t* dllPath = nullptr);

    // Stops all haptics and closes the connection to the Player.
    // The DLL itself stays loaded on purpose (see .cpp for the reason).
    void Shutdown();

    // True after a successful load of the DLL (independent of Player connection).
    bool IsLoaded();

    // True if the websocket to the bHaptics Player is open.
    bool IsConnected();

    // True if the given device is connected to the Player.
    bool IsDeviceConnected(Position position);

    // Plays an event defined in the workspace. Returns the request id (0 if not loaded).
    int Play(const char* eventName);

    // Plays an event with modifiers.
    //   intensity: multiplier, 1.0 = as designed
    //   duration:  multiplier, 1.0 = as designed
    //   angleX:    rotation around the body in degrees (0-360), for vest events
    //   offsetY:   vertical shift (-0.5 .. 0.5), for vest events
    // Returns the request id (0 if not loaded).
    int PlayParam(const char* eventName,
                  float intensity = 1.0f,
                  float duration = 1.0f,
                  float angleX = 0.0f,
                  float offsetY = 0.0f);

    // Fire-and-forget variant of PlayParam (no request id round trip).
    void PlayParamNoResult(const char* eventName,
                           float intensity = 1.0f,
                           float duration = 1.0f,
                           float angleX = 0.0f,
                           float offsetY = 0.0f);

    // Plays an event repeatedly.
    //   intervalMs: pause between repetitions
    //   maxCount:   number of repetitions, 0 = until stopped
    // Returns the request id (0 if not loaded).
    int PlayLoop(const char* eventName,
                 float intensity = 1.0f,
                 float duration = 1.0f,
                 float angleX = 0.0f,
                 float offsetY = 0.0f,
                 int intervalMs = 0,
                 int maxCount = 0);

    bool Stop(int requestId);
    bool StopEvent(const char* eventName);
    bool StopAll();

    bool IsPlaying();
    bool IsPlayingEvent(const char* eventName);
    bool IsPlayingRequest(int requestId);

    // Human-readable description of the last load/init problem ("" if none).
    const char* LastError();
}
