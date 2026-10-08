#include "log.h"
#include "northstar.h"

#include <cstdarg>
#include <cstdio>

namespace
{
    ns::ISys* g_sys  = nullptr;
    HMODULE   g_self = nullptr;

    void Write(logging::Level level, const char* fmt, va_list args)
    {
        if (!logging::Enabled(level))
            return;

        // Northstar has no debug level, so debug lines go out as INFO with a tag.
        char buf[2048];
        int offset = 0;
        if (level == logging::Level::Debug)
            offset = std::snprintf(buf, sizeof(buf), "[DEBUG] ");
        std::vsnprintf(buf + offset, sizeof(buf) - offset, fmt, args);

        if (g_sys)
        {
            ns::LogLevel nsLevel = ns::LogLevel::INFO;
            if (level == logging::Level::Warn)
                nsLevel = ns::LogLevel::WARN;
            else if (level == logging::Level::Error)
                nsLevel = ns::LogLevel::ERR;

            g_sys->Log(reinterpret_cast<int64_t>(g_self), nsLevel, buf);
            return;
        }

        // Fallback before Northstar's logger is available.
        static const char* const prefix[] = { "[Titanfall2VR_bhaptics] ", "[Titanfall2VR_bhaptics] ", "[Titanfall2VR_bhaptics] WARN: ", "[Titanfall2VR_bhaptics] ERROR: " };
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

#define TF2VR_BH_LOG_FN(Name, Lvl)            \
    void Name(const char* fmt, ...)           \
    {                                         \
        if (!Enabled(Lvl))                    \
            return;                           \
        va_list args;                         \
        va_start(args, fmt);                  \
        Write(Lvl, fmt, args);                \
        va_end(args);                         \
    }

    TF2VR_BH_LOG_FN(Debug, Level::Debug)
    TF2VR_BH_LOG_FN(Info,  Level::Info)
    TF2VR_BH_LOG_FN(Warn,  Level::Warn)
    TF2VR_BH_LOG_FN(Error, Level::Error)

#undef TF2VR_BH_LOG_FN
}
