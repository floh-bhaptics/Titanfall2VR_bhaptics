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
    // northstarModule: the HMODULE Northstar passes to IPluginCallbacks::Init.
    // self:            this plugin's own HMODULE.
    void Init(HMODULE northstarModule, HMODULE self);

    void Info(_Printf_format_string_ const char* fmt, ...);
    void Warn(_Printf_format_string_ const char* fmt, ...);
    void Error(_Printf_format_string_ const char* fmt, ...);
}
