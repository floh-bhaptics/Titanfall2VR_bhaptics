#pragma once
//
// log.h - printf-style logging into the Northstar console and log file
// (<Titanfall2>\TF2VR\logs\nslog*.txt), prefixed with the plugin's log name.
//
// Before logging::Init() succeeds, messages go to OutputDebugString instead
// (visible in Visual Studio's Output window or Sysinternals DebugView).
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace logging
{
    enum class Level : int
    {
        Debug = 0, // development details: every haptic event played, device states, ...
        Info  = 1, // minimal: plugin loaded, Player connected
        Warn  = 2,
        Error = 3,
    };

    // =====================================================================
    //  LOG LEVEL SETTING - messages below this level are dropped.
    //  Development: Level::Debug    Public release: Level::Info
    // =====================================================================
    constexpr Level kMinLevel = Level::Debug;

    // northstarModule: the HMODULE Northstar passes to IPluginCallbacks::Init.
    // self:            this plugin's own HMODULE.
    void Init(HMODULE northstarModule, HMODULE self);

    constexpr bool Enabled(Level level) { return level >= kMinLevel; }

    void Debug(_Printf_format_string_ const char* fmt, ...);
    void Info(_Printf_format_string_ const char* fmt, ...);
    void Warn(_Printf_format_string_ const char* fmt, ...);
    void Error(_Printf_format_string_ const char* fmt, ...);
}
