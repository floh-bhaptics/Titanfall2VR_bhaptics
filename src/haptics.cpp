#include "haptics.h"
#include "log.h"
#include "bhaptics_wrapper.h"

#include <cctype>
#include <string>

namespace
{
    // ---- bHaptics workspace (developer portal) ----
    constexpr const char* kWorkspaceId = "6ac728501d88936357e2273e";
    constexpr const char* kApiKey      = "nD3lyROAtB93VkqOlShi";

    // ---- Timing (milliseconds) ----
    constexpr ULONGLONG kNotConnectedWarnAfter = 15000; // warn once if the Player isn't reachable
    constexpr ULONGLONG kHeartbeatDelay        = 1500;  // let the Player receive the workspace first

    enum class State
    {
        Off,              // not initialized or failed hard
        WaitingForPlayer, // registered, websocket not open yet
        Connected,        // connected, startup heartbeat pending
        Ready,            // startup heartbeat played
    };

    State     g_state          = State::Off;
    ULONGLONG g_startTick      = 0;
    ULONGLONG g_heartbeatTick  = 0;
    bool      g_warnedNoPlayer = false;

    // ---- Helpers ----

    std::wstring ModuleDirectory(HMODULE module)
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
        if (len == 0 || len == MAX_PATH)
            return L"";
        std::wstring dir(path, len);
        const auto slash = dir.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1);
    }

    bool FileExists(const std::wstring& path)
    {
        const DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty())
            return {};
        const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);
        return s;
    }

    void LogDevices()
    {
        logging::Debug("Devices: Vest=%d ArmL=%d ArmR=%d Head=%d HandL=%d HandR=%d",
                      bh::IsDeviceConnected(bh::Position::Vest),
                      bh::IsDeviceConnected(bh::Position::ForearmL),
                      bh::IsDeviceConnected(bh::Position::ForearmR),
                      bh::IsDeviceConnected(bh::Position::Head),
                      bh::IsDeviceConnected(bh::Position::HandL),
                      bh::IsDeviceConnected(bh::Position::HandR));
    }
}

namespace haptics
{
    int PlaybackHaptics(const char* eventName, float intensity, float duration, float angleX, float offsetY)
    {
        if (!eventName || !*eventName)
            return 0;

        // bHaptics event names are all lower case; callers may use CamelCase.
        std::string key(eventName);
        for (char& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        const int requestId = bh::PlayParam(key.c_str(), intensity, duration, angleX, offsetY);

        logging::Debug("Play '%s' (intensity %.2f, duration %.2f, angle %.0f, offsetY %.2f) -> request %d",
                       key.c_str(), intensity, duration, angleX, offsetY, requestId);
        return requestId;
    }

    void Startup(HMODULE self)
    {
        g_startTick      = GetTickCount64();
        g_warnedNoPlayer = false;

        // Preferred location: <TF2VR>\plugins\lib\bhaptics_library.dll
        // (Northstar does not try to load DLLs in plugins\lib as plugins).
        // Fallback: next to this plugin DLL.
        const std::wstring libPath = ModuleDirectory(self) + L"lib\\bhaptics_library.dll";
        const bool inLibDir = FileExists(libPath);
        logging::Debug("Loading bHaptics library from %s", inLibDir ? ToUtf8(libPath).c_str() : "plugin folder (plugins\\lib\\ not found)");

        const bh::InitStatus status = bh::Initialize(kWorkspaceId, kApiKey, "", inLibDir ? libPath.c_str() : nullptr);

        switch (status)
        {
            case bh::InitStatus::Ok:
                logging::Debug("Registered with bHaptics Player");
                g_state = State::WaitingForPlayer;
                break;
            case bh::InitStatus::AlreadyInitialized:
                logging::Debug("bHaptics already initialized (plugin reload)");
                g_state = State::WaitingForPlayer;
                break;
            case bh::InitStatus::NotConnected:
                logging::Debug("bHaptics library loaded, waiting for bHaptics Player...");
                g_state = State::WaitingForPlayer;
                break;
            case bh::InitStatus::DllNotFound:
            case bh::InitStatus::ExportMissing:
                logging::Error("bHaptics disabled: %s", bh::LastError());
                g_state = State::Off;
                break;
        }
    }

    void Frame()
    {
        if (g_state == State::Off || g_state == State::Ready)
            return;

        const ULONGLONG now = GetTickCount64();

        if (g_state == State::WaitingForPlayer)
        {
            if (bh::IsConnected())
            {
                logging::Info("Connected to bHaptics Player after %llu ms", now - g_startTick);
                LogDevices();
                g_state         = State::Connected;
                g_heartbeatTick = now + kHeartbeatDelay;
            }
            else if (!g_warnedNoPlayer && now - g_startTick > kNotConnectedWarnAfter)
            {
                logging::Warn("bHaptics Player still not reachable. Is it running? (will keep trying)");
                g_warnedNoPlayer = true;
            }
            return;
        }

        // State::Connected
        if (now < g_heartbeatTick)
            return;

        PlaybackHaptics("HeartBeat");
        g_state = State::Ready;
    }

    bool IsConnected()
    {
        return g_state != State::Off && bh::IsConnected();
    }

    void StopAll()
    {
        if (g_state == State::Off)
            return;
        bh::StopAll();
        logging::Debug("Stopped all haptics");
    }

    void Shutdown()
    {
        if (g_state == State::Off)
            return;
        bh::Shutdown();
        g_state = State::Off;
        logging::Debug("bHaptics connection closed");
    }
}
