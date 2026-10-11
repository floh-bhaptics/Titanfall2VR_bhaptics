#include "haptics.h"
#include "log.h"
#include "bhaptics_wrapper.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

//
// Threading model
// ---------------
// Every call into the bHaptics SDK happens on ONE worker thread. The game
// thread (RunFrame, Squirrel natives) only pushes commands into a small queue
// and returns immediately, so a slow or hanging bHaptics library/Player can
// never freeze the game. In the worst case haptics arrive late or are dropped.
//

namespace
{
    // ---- bHaptics workspace (developer portal) ----
    constexpr const char* kWorkspaceId = "6ac728501d88936357e2273e";
    constexpr const char* kApiKey      = "nD3lyROAtB93VkqOlShi";

    // ---- Tuning ----
    constexpr ULONGLONG kNotConnectedWarnAfter = 15000; // ms: warn once if the Player isn't reachable
    constexpr ULONGLONG kHeartbeatDelay        = 1500;  // ms: let the Player receive the workspace first
    constexpr ULONGLONG kSlowCallWarn          = 100;   // ms: log SDK calls slower than this
    constexpr ULONGLONG kStuckCallWarn         = 2000;  // ms: game thread warns if a call hangs this long
    constexpr ULONGLONG kShutdownWait          = 2000;  // ms: max wait for the worker on shutdown
    constexpr size_t    kMaxQueue              = 32;    // pending commands; oldest dropped when full
    constexpr auto      kIdlePoll              = std::chrono::milliseconds(250);

    // ---- Commands ----
    enum class CmdType { Play, StopAll };

    struct Command
    {
        CmdType     type = CmdType::Play;
        std::string key;
        float       intensity = 1.0f;
        float       duration  = 1.0f;
        float       angleX    = 0.0f;
        float       offsetY   = 0.0f;
    };

    // ---- Shared state (game thread <-> worker) ----
    std::mutex              g_mutex;
    std::condition_variable g_wake;
    std::deque<Command>     g_queue;
    bool                    g_stopRequested = false;   // guarded by g_mutex
    size_t                  g_dropped       = 0;       // guarded by g_mutex

    // Heap-allocated and never destroyed by a static destructor: a joinable
    // std::thread being destroyed at game exit would call std::terminate.
    std::thread*            g_worker = nullptr;
    std::atomic<bool>       g_running{ false };        // worker alive, accepting commands
    std::atomic<bool>       g_workerExited{ true };
    std::atomic<bool>       g_connected{ false };      // cached Player connection state
    std::atomic<ULONGLONG>  g_callStartTick{ 0 };      // != 0 while the worker is inside an SDK call
    std::atomic<bool>       g_stuckWarned{ false };

    std::wstring            g_libPath;                 // set before the worker starts

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

    std::string ToLower(const char* s)
    {
        std::string key(s);
        for (char& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return key;
    }

    // Times an SDK call made on the worker thread; slow calls are logged.
    template <typename Fn>
    void TimedCall(const char* what, Fn&& fn)
    {
        const ULONGLONG start = GetTickCount64();
        g_callStartTick.store(start);
        fn();
        g_callStartTick.store(0);

        const ULONGLONG took = GetTickCount64() - start;
        if (took >= kSlowCallWarn)
            logging::Warn("bHaptics call '%s' took %llu ms", what, took);
        if (g_stuckWarned.exchange(false))
            logging::Warn("bHaptics call '%s' finished after %llu ms, haptics resumed", what, took);
    }

    void PushCommand(Command&& cmd)
    {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_queue.size() >= kMaxQueue)
            {
                g_queue.pop_front(); // drop the oldest: fresh events matter more
                ++g_dropped;
            }
            g_queue.push_back(std::move(cmd));
        }
        g_wake.notify_one();
    }

    // ---- Worker thread ----

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

    bool InitializeSdk()
    {
        const bool inLibDir = FileExists(g_libPath);
        logging::Debug("Loading bHaptics library from %s", inLibDir ? ToUtf8(g_libPath).c_str() : "plugin folder (plugins\\lib\\ not found)");

        bh::InitStatus status = bh::InitStatus::DllNotFound;
        TimedCall("Initialize", [&] { status = bh::Initialize(kWorkspaceId, kApiKey, "", inLibDir ? g_libPath.c_str() : nullptr); });

        switch (status)
        {
            case bh::InitStatus::Ok:                 logging::Debug("Registered with bHaptics Player"); return true;
            case bh::InitStatus::AlreadyInitialized: logging::Debug("bHaptics already initialized (plugin reload)"); return true;
            case bh::InitStatus::NotConnected:       logging::Debug("bHaptics library loaded, waiting for bHaptics Player..."); return true;
            case bh::InitStatus::DllNotFound:
            case bh::InitStatus::ExportMissing:
                logging::Error("bHaptics disabled: %s", bh::LastError());
                return false;
        }
        return false;
    }

    void Execute(const Command& cmd)
    {
        switch (cmd.type)
        {
            case CmdType::Play:
                TimedCall(cmd.key.c_str(), [&] { bh::PlayParamNoResult(cmd.key.c_str(), cmd.intensity, cmd.duration, cmd.angleX, cmd.offsetY); });
                break;
            case CmdType::StopAll:
                TimedCall("StopAll", [] { bh::StopAll(); });
                logging::Debug("Stopped all haptics");
                break;
        }
    }

    void WorkerMain()
    {
        if (!InitializeSdk())
        {
            g_running.store(false);
            g_workerExited.store(true);
            return;
        }

        const ULONGLONG startTick     = GetTickCount64();
        ULONGLONG       heartbeatTick = 0;   // != 0: startup heartbeat pending
        bool            heartbeatDone = false;
        bool            warnedNoPlayer = false;

        ULONGLONG lastPoll = 0;

        for (;;)
        {
            std::deque<Command> batch;
            size_t dropped = 0;
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_wake.wait_for(lock, kIdlePoll, [] { return g_stopRequested || !g_queue.empty(); });
                if (g_stopRequested)
                    break;
                batch.swap(g_queue); // take everything queued so far
                dropped   = g_dropped;
                g_dropped = 0;
            }

            if (dropped > 0)
                logging::Debug("Dropped %zu queued haptic event(s): bHaptics was too slow", dropped);

            for (const Command& cmd : batch)
                Execute(cmd);

            // Connection state: polled here at most every kIdlePoll, read by
            // the game thread from g_connected.
            const ULONGLONG now = GetTickCount64();
            if (now - lastPoll < static_cast<ULONGLONG>(kIdlePoll.count()))
                continue;
            lastPoll = now;

            bool connected = false;
            TimedCall("IsConnected", [&] { connected = bh::IsConnected(); });

            if (connected && !g_connected.load())
            {
                logging::Info("Connected to bHaptics Player after %llu ms", now - startTick);
                LogDevices();
                if (!heartbeatDone)
                    heartbeatTick = now + kHeartbeatDelay;
            }
            else if (!connected && g_connected.load())
            {
                logging::Warn("Lost connection to bHaptics Player (will keep trying)");
            }
            else if (!connected && !warnedNoPlayer && now - startTick > kNotConnectedWarnAfter)
            {
                logging::Warn("bHaptics Player still not reachable. Is it running? (will keep trying)");
                warnedNoPlayer = true;
            }
            g_connected.store(connected);

            if (heartbeatTick != 0 && now >= heartbeatTick)
            {
                Command hb;
                hb.key = "heartbeat";
                logging::Debug("Play 'heartbeat' (startup)");
                Execute(hb);
                heartbeatTick = 0;
                heartbeatDone = true;
            }
        }

        // Shutdown requested: stop and disconnect while we still own the SDK.
        TimedCall("Shutdown", [] { bh::Shutdown(); });
        g_connected.store(false);
        logging::Debug("bHaptics connection closed");
        g_workerExited.store(true);
    }
}

namespace haptics
{
    bool PlaybackHaptics(const char* eventName, float intensity, float duration, float angleX, float offsetY)
    {
        if (!eventName || !*eventName || !g_running.load())
            return false;

        // bHaptics event names are all lower case; callers may use CamelCase.
        Command cmd;
        cmd.type      = CmdType::Play;
        cmd.key       = ToLower(eventName);
        cmd.intensity = intensity;
        cmd.duration  = duration;
        cmd.angleX    = angleX;
        cmd.offsetY   = offsetY;

        logging::Debug("Play '%s' (intensity %.2f, duration %.2f, angle %.0f, offsetY %.2f)",
                       cmd.key.c_str(), intensity, duration, angleX, offsetY);

        PushCommand(std::move(cmd));
        return true;
    }

    void Startup(HMODULE self)
    {
        if (g_running.load())
            return;

        // Preferred location: <TF2VR>\plugins\lib\bhaptics_library.dll
        // (Northstar does not try to load DLLs in plugins\lib as plugins).
        g_libPath = ModuleDirectory(self) + L"lib\\bhaptics_library.dll";

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_stopRequested = false;
            g_queue.clear();
            g_dropped = 0;
        }
        g_workerExited.store(false);
        g_running.store(true);
        g_worker = new std::thread(WorkerMain);
    }

    void Frame()
    {
        // Watchdog only: report (once per stall) if the worker hangs in an SDK
        // call. The game itself is unaffected.
        const ULONGLONG start = g_callStartTick.load();
        if (start != 0 && !g_stuckWarned.load() && GetTickCount64() - start > kStuckCallWarn)
        {
            g_stuckWarned.store(true);
            logging::Warn("bHaptics call is hanging for over %llu ms (game keeps running, haptics paused)", kStuckCallWarn);
        }
    }

    bool IsConnected()
    {
        return g_running.load() && g_connected.load();
    }

    void StopAll()
    {
        if (!g_running.load())
            return;

        {
            // Queued events are obsolete as well.
            std::lock_guard<std::mutex> lock(g_mutex);
            g_queue.clear();
        }
        Command cmd;
        cmd.type = CmdType::StopAll;
        PushCommand(std::move(cmd));
    }

    bool Shutdown()
    {
        if (!g_worker)
            return true;

        g_running.store(false);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_stopRequested = true;
            g_queue.clear();
        }
        g_wake.notify_one();

        // Don't block forever if the worker is stuck inside the SDK.
        const ULONGLONG deadline = GetTickCount64() + kShutdownWait;
        while (!g_workerExited.load() && GetTickCount64() < deadline)
            Sleep(10);

        if (!g_workerExited.load())
        {
            // The thread still runs code from this DLL, so the DLL must stay
            // loaded: detach and tell Northstar not to unload us.
            logging::Warn("bHaptics worker did not stop in time, keeping plugin loaded");
            g_worker->detach();
            g_worker = nullptr; // leaked on purpose, the thread still owns its stack
            return false;
        }

        g_worker->join();
        delete g_worker;
        g_worker = nullptr;
        return true;
    }
}
