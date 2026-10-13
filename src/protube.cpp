#include "protube.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

//
// Same threading model as haptics.cpp: the game thread only queues commands,
// one worker thread owns every call into ForceTubeVR_API_x64.dll.
//

namespace
{
    using Clock = std::chrono::steady_clock;

    // ---- SDK (plain C exports, x64 has a single calling convention) ----
    using InitPistolFn       = void (*)();
    using SetActiveFn        = void (*)(int active); // C# "bool": int works for 1- and 4-byte bools
    using KickChannelFn      = void (*)(uint8_t power, int channel);
    using RumbleChannelFn    = void (*)(uint8_t power, float seconds, int channel);
    using TempoToKickPowerFn = uint8_t (*)(float seconds);

    struct Sdk
    {
        InitPistolFn       initPistol       = nullptr;
        SetActiveFn        setActive        = nullptr;
        KickChannelFn      kickChannel      = nullptr;
        RumbleChannelFn    rumbleChannel    = nullptr;
        TempoToKickPowerFn tempoToKickPower = nullptr;
    } g_sdk;

    // InitPistol() spreads the connected devices over pistol1, pistol2, rifleButt,
    // ... in the order Windows finds them. Until left/right routing exists,
    // channel 0 ("all") sends every effect to every connected device.
    constexpr int kChannel = 0; // all

    // ---- Tuning (values from the ProVolver bridge 0.3.3) ----
    constexpr double kRecoveryFull     = 0.130; // s, "full" and initial "heavy" recovery
    constexpr double kRecoveryLight    = 0.060; // s, initial recovery for other groups
    constexpr double kRecoveryMin      = 0.050; // s
    constexpr double kRecoveryMax      = 0.130; // s
    constexpr double kRecoveryMargin   = 0.008; // s, subtracted from the fastest interval
    constexpr double kIntervalMin      = 0.025; // s, shorter gaps aren't used as samples
    constexpr double kIntervalMax      = 0.300; // s, longer gaps reset the adaptation
    constexpr size_t kIntervalSamples  = 6;
    constexpr double kKickMaxWait      = 0.040; // s, a waiting kick expires after this
    constexpr float  kRumblePulse      = 0.100f; // s per charge sample, never 0 (= endless)
    constexpr float  kRumbleStop       = 0.010f;
    constexpr float  kRumbleMin        = 0.030f; // s, shorter pulses aren't felt (SDK docs)
    constexpr float  kRumbleMax        = 2.000f; // s
    constexpr size_t kMaxQueue         = 32;
    constexpr ULONGLONG kShutdownWait  = 2000; // ms
    constexpr auto   kIdlePoll         = std::chrono::milliseconds(250);

    // ---- Commands ----
    enum class CmdType { Kick, Charge, Rumble, Stop };

    struct Command
    {
        CmdType           type = CmdType::Kick;
        std::string       weapon;
        std::string       group;
        float             fraction = 0.0f;
        float             seconds  = 0.0f;
        Clock::time_point time;
    };

    std::mutex              g_mutex;
    std::condition_variable g_wake;
    std::deque<Command>     g_queue;
    bool                    g_stopRequested = false; // guarded by g_mutex

    std::thread*            g_worker = nullptr;      // heap: see haptics.cpp
    std::atomic<bool>       g_running{ false };
    std::atomic<bool>       g_workerExited{ true };
    std::atomic<bool>       g_available{ false };    // DLL file present

    std::wstring            g_libPath;

    double Seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

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

    void PushCommand(Command&& cmd)
    {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_queue.size() >= kMaxQueue)
                g_queue.pop_front();
            g_queue.push_back(std::move(cmd));
        }
        g_wake.notify_one();
    }

    template <typename T>
    bool Bind(HMODULE module, const char* name, T& out)
    {
        out = reinterpret_cast<T>(GetProcAddress(module, name));
        if (!out)
            logging::Error("ProTube disabled: export '%s' missing in ForceTubeVR_API_x64.dll", name);
        return out != nullptr;
    }

    bool LoadSdk()
    {
        HMODULE module = LoadLibraryW(g_libPath.c_str());
        if (!module)
        {
            const DWORD err = GetLastError();
            logging::Error("ProTube disabled: could not load ForceTubeVR_API_x64.dll (Windows error %lu%s)",
                           err, err == ERROR_MOD_NOT_FOUND ? ", install the Visual C++ x64 runtime" : "");
            return false;
        }
        // Never FreeLibrary: the SDK runs its own threads until process exit.

        if (!Bind(module, "InitPistol", g_sdk.initPistol) ||
            !Bind(module, "SetActive", g_sdk.setActive) ||
            !Bind(module, "KickChannel", g_sdk.kickChannel) ||
            !Bind(module, "RumbleChannel", g_sdk.rumbleChannel) ||
            !Bind(module, "TempoToKickPower", g_sdk.tempoToKickPower))
            return false;

        const auto start = Clock::now();
        g_sdk.initPistol(); // starts the SDK's own device search/reconnect thread
        logging::Info("ProTube SDK loaded (init took %.0f ms)", Seconds(Clock::now() - start) * 1000.0);
        return true;
    }

    // ---- Recoil scheduler (port of the bridge's Scheduler + RecoilPower) ----
    struct PendingKick
    {
        bool              valid = false;
        std::string       weapon;
        Clock::time_point received;
        double            recovery = 0.0;
        bool              fullPower = false;
    };

    struct Scheduler
    {
        std::deque<double> intervals;
        Clock::time_point  lastArrival{};
        Clock::time_point  lastOutput{};
        bool               hasArrival = false;
        bool               hasOutput  = false;
        double             recovery   = 0.0; // of the previous kick
        std::string        weapon;
        PendingKick        pending;

        void Offer(const Command& cmd)
        {
            const double dt = hasArrival ? Seconds(cmd.time - lastArrival) : 1e9;
            if (cmd.weapon != weapon || dt > kIntervalMax)
                intervals.clear();
            else if (dt >= kIntervalMin)
            {
                intervals.push_back(dt);
                while (intervals.size() > kIntervalSamples)
                    intervals.pop_front();
            }
            weapon      = cmd.weapon;
            lastArrival = cmd.time;
            hasArrival  = true;

            const bool full = cmd.group == "full";
            double period = (full || cmd.group == "heavy") ? kRecoveryFull : kRecoveryLight;
            if (!full && intervals.size() >= 2)
            {
                const double fastest = *std::min_element(intervals.begin(), intervals.end());
                period = std::clamp(fastest - kRecoveryMargin, kRecoveryMin, kRecoveryMax);
            }

            pending.valid     = true;
            pending.weapon    = cmd.weapon;
            pending.received  = cmd.time;
            pending.recovery  = period;
            pending.fullPower = full;
        }

        // Fires the pending kick if allowed. Returns the time to wake up again
        // if a kick is still waiting for the previous one to recover.
        bool Take(Clock::time_point now, Clock::time_point& wakeAt)
        {
            if (!pending.valid)
                return false;

            if (Seconds(now - pending.received) > kKickMaxWait)
            {
                logging::Debug("ProTube kick dropped (device still recovering)");
                pending.valid = false;
                return false;
            }

            if (hasOutput)
            {
                const auto ready = lastOutput + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(recovery));
                if (now < ready)
                {
                    wakeAt = ready;
                    return true;
                }
            }

            const uint8_t power = pending.fullPower ? 255 : g_sdk.tempoToKickPower(static_cast<float>(pending.recovery));
            if (power > 0)
                g_sdk.kickChannel(power, kChannel);
            logging::Debug("ProTube kick %s power %u recovery %.0f ms", pending.weapon.c_str(), power, pending.recovery * 1000.0);

            lastOutput    = now;
            hasOutput     = true;
            recovery      = pending.recovery; // recovery belongs to THIS kick
            pending.valid = false;
            return false;
        }
    };

    uint8_t ChargePower(float fraction)
    {
        // Bridge envelope: 1..100 % -> 32..255
        int percent = static_cast<int>(fraction * 100.0f);
        percent = std::clamp(percent, 1, 100);
        return static_cast<uint8_t>(32 + (223 * percent) / 100);
    }

    void WorkerMain()
    {
        if (!LoadSdk())
        {
            g_available.store(false);
            g_running.store(false);
            g_workerExited.store(true);
            return;
        }

        Scheduler scheduler;
        bool      rumbling = false;
        bool      waiting  = false;
        Clock::time_point wakeAt{};

        auto stopRumble = [&] {
            if (rumbling)
                g_sdk.rumbleChannel(0, kRumbleStop, kChannel);
            rumbling = false;
        };

        for (;;)
        {
            std::deque<Command> batch;
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                auto pred = [] { return g_stopRequested || !g_queue.empty(); };
                if (waiting)
                    g_wake.wait_until(lock, wakeAt, pred);
                else
                    g_wake.wait_for(lock, kIdlePoll, pred);
                if (g_stopRequested)
                    break;
                batch.swap(g_queue);
            }

            for (const Command& cmd : batch)
            {
                switch (cmd.type)
                {
                    case CmdType::Kick:
                        scheduler.Offer(cmd);
                        break;
                    case CmdType::Charge:
                        if (cmd.fraction <= 0.0f)
                            stopRumble();
                        else
                        {
                            g_sdk.rumbleChannel(ChargePower(cmd.fraction), kRumblePulse, kChannel);
                            rumbling = true;
                        }
                        break;
                    case CmdType::Rumble:
                    {
                        const int power = static_cast<int>(cmd.fraction * 255.0f + 0.5f);
                        if (power <= 0)
                            break;
                        const uint8_t p = static_cast<uint8_t>(std::min(power, 255));
                        const float   t = std::clamp(cmd.seconds, kRumbleMin, kRumbleMax);
                        g_sdk.rumbleChannel(p, t, kChannel);
                        rumbling = true;
                        logging::Debug("ProTube rumble power %u for %.0f ms", p, t * 1000.0f);
                        break;
                    }
                    case CmdType::Stop:
                        scheduler.pending.valid = false;
                        stopRumble();
                        break;
                }
            }

            waiting = scheduler.Take(Clock::now(), wakeAt);
        }

        // A pulse that isn't refreshed ends by itself, but stop explicitly.
        g_sdk.rumbleChannel(0, kRumbleStop, kChannel);
        g_sdk.setActive(0);  // ends the SDK's reconnect thread
        logging::Debug("ProTube stopped");
        g_workerExited.store(true);
    }
}

namespace protube
{
    void Startup(HMODULE self)
    {
        if (g_running.load())
            return;

        g_libPath = ModuleDirectory(self) + L"lib\\ForceTubeVR_API_x64.dll";
        if (!FileExists(g_libPath))
        {
            logging::Info("ProTube support off (plugins\\lib\\ForceTubeVR_API_x64.dll not found)");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_stopRequested = false;
            g_queue.clear();
        }
        g_available.store(true);
        g_workerExited.store(false);
        g_running.store(true);
        g_worker = new std::thread(WorkerMain);
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

        const ULONGLONG deadline = GetTickCount64() + kShutdownWait;
        while (!g_workerExited.load() && GetTickCount64() < deadline)
            Sleep(10);

        if (!g_workerExited.load())
        {
            logging::Warn("ProTube worker did not stop in time, keeping plugin loaded");
            g_worker->detach();
            g_worker = nullptr;
            return false;
        }

        g_worker->join();
        delete g_worker;
        g_worker = nullptr;
        return true;
    }

    bool IsAvailable()
    {
        return g_available.load();
    }

    void Kick(const char* weapon, const char* group)
    {
        if (!g_running.load())
            return;
        Command cmd;
        cmd.type   = CmdType::Kick;
        cmd.weapon = weapon ? weapon : "";
        cmd.group  = group ? group : "";
        cmd.time   = Clock::now();
        PushCommand(std::move(cmd));
    }

    void Charge(float fraction)
    {
        if (!g_running.load())
            return;
        Command cmd;
        cmd.type     = CmdType::Charge;
        cmd.fraction = fraction;
        cmd.time     = Clock::now();
        PushCommand(std::move(cmd));
    }

    void Rumble(float power, float seconds)
    {
        if (!g_running.load())
            return;
        Command cmd;
        cmd.type     = CmdType::Rumble;
        cmd.fraction = power;
        cmd.seconds  = seconds;
        cmd.time     = Clock::now();
        PushCommand(std::move(cmd));
    }

    void Stop()
    {
        if (!g_running.load())
            return;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_queue.clear();
        }
        Command cmd;
        cmd.type = CmdType::Stop;
        cmd.time = Clock::now();
        PushCommand(std::move(cmd));
    }
}
