#include "log.h"
#include "northstar.h"

#include <cstdarg>
#include <cstdio>

namespace
{
    ns::ISys* g_sys  = nullptr;
    HMODULE   g_self = nullptr;

    void Write(ns::LogLevel level, const char* fmt, va_list args)
    {
        char buf[2048];
        std::vsnprintf(buf, sizeof(buf), fmt, args);

        if (g_sys)
        {
            g_sys->Log(reinterpret_cast<int64_t>(g_self), level, buf);
            return;
        }

        // Fallback before Northstar's logger is available.
        static const char* const prefix[] = { "[Titanfall2VR_bhaptics] ", "[Titanfall2VR_bhaptics] WARN: ", "[Titanfall2VR_bhaptics] ERROR: " };
        OutputDebugStringA(prefix[static_cast<int>(level)]);
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
}

namespace logging
{
    void Init(HMODULE northstarModule, HMODULE self)
    {
        g_self = self;
        if (!northstarModule)
            return;

        // Northstar.dll exports the same CreateInterface signature we do.
        auto createInterface = reinterpret_cast<ns::CreateInterfaceFn>(
            reinterpret_cast<void*>(GetProcAddress(northstarModule, "CreateInterface")));
        if (!createInterface)
            return;

        int status = 1;
        auto sys = static_cast<ns::ISys*>(createInterface(ns::SYS_VERSION, &status));
        if (sys && status == 0)
            g_sys = sys;
    }

    void Info(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Write(ns::LogLevel::INFO, fmt, args);
        va_end(args);
    }

    void Warn(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Write(ns::LogLevel::WARN, fmt, args);
        va_end(args);
    }

    void Error(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Write(ns::LogLevel::ERR, fmt, args);
        va_end(args);
    }
}
