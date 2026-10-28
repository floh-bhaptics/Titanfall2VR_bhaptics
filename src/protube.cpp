#include "protube.h"
#include "log.h"

#include <objbase.h> // CoTaskMemFree

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
#include <vector>

#pragma comment(lib, "ole32.lib")

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
    // Channel management (optional, only needed for dual wielding).
    // ListChannels returns a CoTaskMemAlloc'd JSON string (what C# interop frees).
    using ListChannelsFn     = char* (*)();
    using ClearChannelFn     = void (*)(int channel);
    using AddToChannelFn     = void (*)(int channel, const char* name); // returns a bool we don't need

    struct Sdk
    {
        InitPistolFn       initPistol       = nullptr;
        SetActiveFn        setActive        = nullptr;
        KickChannelFn      kickChannel      = nullptr;
        RumbleChannelFn    rumbleChannel    = nullptr;
        TempoToKickPowerFn tempoToKickPower = nullptr;
        ListChannelsFn     listChannels     = nullptr;
        ClearChannelFn     clearChannel     = nullptr;
        AddToChannelFn     addToChannel     = nullptr;
    } g_sdk;

    // ---- Channels and dual wielding ----
    // InitPistol() puts the connected devices into pistol1, pistol2, rifleButt,
    // ... in the order Windows finds them. Same scheme as Astienth's and floh's
    // ProTube mods (files in <TF2VR>\UserData\):
    //   - one device:   everything goes to channel 0 (all devices)
    //   - two devices:  right hand = pistol1, left hand = pistol2, saved as
    //                   right.pro / left.pro (device name, e.g. "ForceTubeVR 616241100")
    //                   and restored on later starts; swap by swapping the file names
    //   - sameHandRight.txt / sameHandLeft.txt: both devices act as that hand
    constexpr int kChannelAll     = 0;
    constexpr int kChannelPistol1 = 4; // right hand
    constexpr int kChannelPistol2 = 5; // left hand
    constexpr int kChannelNone    = -1;

    enum class Hand { Any, Left, Right };
    enum class Layout { All, Dual, SameRight, SameLeft };

    // Device scan: every second for the first 20 s, then every 5 s up to 2 min
    // (devices switched on a bit later still get sorted).
    constexpr double kScanFastPeriod = 1.0;
    constexpr double kScanFastUntil  = 20.0;
    constexpr double kScanSlowPeriod = 5.0;
    constexpr double kScanUntil      = 120.0;

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
        Hand              hand     = Hand::Any;
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
    std::wstring            g_userDir;                // "<TF2VR>\UserData\"

    // Worker thread only:
    Layout                  g_layout = Layout::All;

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

    Hand ParseHand(const char* hand)
    {
        if (!hand)
            return Hand::Any;
        if (_stricmp(hand, "left") == 0)
            return Hand::Left;
        if (_stricmp(hand, "right") == 0)
            return Hand::Right;
        return Hand::Any;
    }

    const char* HandName(Hand hand)
    {
        switch (hand)
        {
            case Hand::Left:  return "left";
            case Hand::Right: return "right";
            default:          return "any";
        }
    }

    // Channel for an effect from this hand, or kChannelNone if no device
    // belongs to that hand (same-hand layout, other hand).
    int ChannelFor(Hand hand)
    {
        switch (g_layout)
        {
            case Layout::All:
                return kChannelAll;
            case Layout::Dual:
                if (hand == Hand::Right) return kChannelPistol1;
                if (hand == Hand::Left)  return kChannelPistol2;
                return kChannelAll;
            case Layout::SameRight:
                return hand == Hand::Left ? kChannelNone : kChannelAll;
            case Layout::SameLeft:
                return hand == Hand::Right ? kChannelNone : kChannelAll;
        }
        return kChannelAll;
    }

    // ---- UserData files (UTF-8 text, BOM and surrounding whitespace ignored) ----

    std::string ReadTextFile(const std::wstring& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return {};
        char buffer[512] = {};
        DWORD read = 0;
        ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
        CloseHandle(file);

        std::string text(buffer, read);
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
            text.erase(0, 3);
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    bool WriteTextFile(const std::wstring& path, const std::string& text)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(file);
        return ok && written == text.size();
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

    // ---- ListChannels() JSON: {"channels":{"pistol1":[{"name":"...","batteryLevel":0}], ...}} ----
    // Tiny purpose-built reader: device names in one channel's array.
    std::vector<std::string> ChannelDevices(const std::string& json, const char* channel)
    {
        std::vector<std::string> names;
        const std::string key = std::string("\"") + channel + "\"";
        auto pos = json.find(key);
        if (pos == std::string::npos)
            return names;
        const auto open = json.find('[', pos + key.size());
        if (open == std::string::npos)
            return names;
        const auto close = json.find(']', open);
        if (close == std::string::npos)
            return names;

        pos = open;
        for (;;)
        {
            pos = json.find("\"name\"", pos);
            if (pos == std::string::npos || pos > close)
                break;
            const auto colon = json.find(':', pos + 6);
            const auto start = colon == std::string::npos ? std::string::npos : json.find('"', colon);
            if (start == std::string::npos || start > close)
                break;
            std::string name;
            size_t i = start + 1;
            for (; i < close && json[i] != '"'; ++i)
            {
                if (json[i] == '\\' && i + 1 < close)
                    ++i;
                name += json[i];
            }
            if (!name.empty())
                names.push_back(name);
            pos = i + 1;
        }
        return names;
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

        // Optional: without these, all effects simply go to all devices.
        g_sdk.listChannels = reinterpret_cast<ListChannelsFn>(GetProcAddress(module, "ListChannels"));
        g_sdk.clearChannel = reinterpret_cast<ClearChannelFn>(GetProcAddress(module, "ClearChannel"));
        g_sdk.addToChannel = reinterpret_cast<AddToChannelFn>(GetProcAddress(module, "AddToChannel"));
        if (!g_sdk.listChannels || !g_sdk.clearChannel || !g_sdk.addToChannel)
        {
            logging::Warn("ProTube: channel functions missing, dual wielding disabled");
            g_sdk.listChannels = nullptr;
        }

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
        int               channel = kChannelAll;
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
            const int channel = ChannelFor(cmd.hand);
            if (channel == kChannelNone)
            {
                logging::Debug("ProTube kick %s skipped: no device for the %s hand", cmd.weapon.c_str(), HandName(cmd.hand));
                return;
            }

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
            pending.channel   = channel;
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
                g_sdk.kickChannel(power, pending.channel);
            logging::Debug("ProTube kick %s power %u recovery %.0f ms channel %d",
                           pending.weapon.c_str(), power, pending.recovery * 1000.0, pending.channel);

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

    // Short buzz so players know their devices are connected and which is which:
    // right hand once, left hand twice (all devices once if not dual wielding).
    void IdentifyBuzz()
    {
        constexpr uint8_t power = 200;
        constexpr float   time  = 0.15f;
        if (g_layout != Layout::Dual)
        {
            g_sdk.rumbleChannel(power, time, kChannelAll);
            return;
        }
        g_sdk.rumbleChannel(power, time, kChannelPistol1);
        Sleep(600);
        g_sdk.rumbleChannel(power, time, kChannelPistol2);
        Sleep(300);
        g_sdk.rumbleChannel(power, time, kChannelPistol2);
    }

    void SetupTwoDevices(const std::string& pistol1, const std::string& pistol2)
    {
        CreateDirectoryW(g_userDir.c_str(), nullptr); // fine if it exists

        const bool sameRight = FileExists(g_userDir + L"sameHandRight.txt");
        const bool sameLeft  = FileExists(g_userDir + L"sameHandLeft.txt");
        if (sameRight || sameLeft)
        {
            if (sameRight && sameLeft)
                logging::Warn("ProTube: both sameHandRight.txt and sameHandLeft.txt exist, using right");
            g_layout = sameRight ? Layout::SameRight : Layout::SameLeft;
            logging::Info("ProTube: two devices, both used for the %s hand (%s, %s)",
                          sameRight ? "right" : "left", pistol1.c_str(), pistol2.c_str());
            return;
        }

        const std::wstring rightFile = g_userDir + L"right.pro";
        const std::wstring leftFile  = g_userDir + L"left.pro";
        const std::string  right     = ReadTextFile(rightFile);
        const std::string  left      = ReadTextFile(leftFile);

        auto connected = [&](const std::string& name) { return name == pistol1 || name == pistol2; };

        if (!right.empty() && !left.empty() && right != left && connected(right) && connected(left))
        {
            g_sdk.clearChannel(kChannelPistol1);
            g_sdk.clearChannel(kChannelPistol2);
            g_sdk.addToChannel(kChannelPistol1, right.c_str());
            g_sdk.addToChannel(kChannelPistol2, left.c_str());
            logging::Info("ProTube: dual wielding from right.pro / left.pro. Right hand: %s, left hand: %s", right.c_str(), left.c_str());
        }
        else
        {
            if (!right.empty() || !left.empty())
                logging::Info("ProTube: right.pro / left.pro don't match the connected devices, saving new ones");
            const bool saved = WriteTextFile(rightFile, pistol1) && WriteTextFile(leftFile, pistol2);
            logging::Info("ProTube: dual wielding. Right hand: %s, left hand: %s (%s %s)",
                          pistol1.c_str(), pistol2.c_str(),
                          saved ? "saved to" : "could NOT save to", ToUtf8(g_userDir).c_str());
        }
        g_layout = Layout::Dual;
    }

    // Returns true when the device layout is final (two devices sorted).
    bool ScanDevices(size_t& seenDevices, std::string& lastJson)
    {
        if (!g_sdk.listChannels)
            return true;

        char* raw = g_sdk.listChannels();
        const std::string json = raw ? raw : "";
        if (raw)
            CoTaskMemFree(raw);

        if (json != lastJson)
        {
            logging::Debug("ProTube channels: %s", json.c_str());
            lastJson = json;
        }

        const auto pistol1 = ChannelDevices(json, "pistol1");
        const auto pistol2 = ChannelDevices(json, "pistol2");

        if (!pistol1.empty() && !pistol2.empty())
        {
            SetupTwoDevices(pistol1.front(), pistol2.front());
            IdentifyBuzz();
            return true;
        }

        const size_t count = pistol1.size() + pistol2.size();
        if (count > seenDevices)
        {
            // First device: everything goes to all channels, a second one may follow.
            logging::Info("ProTube: device connected (%s), all effects go to it",
                          (pistol1.empty() ? pistol2 : pistol1).front().c_str());
            IdentifyBuzz();
        }
        seenDevices = count;
        return false;
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
        g_layout = Layout::All;
        const auto  started     = Clock::now();
        auto        nextScan    = started + std::chrono::seconds(1);
        bool        scanning    = true;
        size_t      seenDevices = 0;
        std::string lastJson;
        bool      rumbling = false;
        bool      waiting  = false;
        Clock::time_point wakeAt{};

        auto stopRumble = [&] {
            if (rumbling)
                g_sdk.rumbleChannel(0, kRumbleStop, kChannelAll);
            rumbling = false;
        };

        for (;;)
        {
            std::deque<Command> batch;
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                auto pred = [] { return g_stopRequested || !g_queue.empty(); };
                auto until = Clock::now() + kIdlePoll;
                if (waiting && wakeAt < until)
                    until = wakeAt;
                if (scanning && nextScan < until)
                    until = nextScan;
                g_wake.wait_until(lock, until, pred);
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
                            const int channel = ChannelFor(cmd.hand);
                            if (channel != kChannelNone)
                            {
                                g_sdk.rumbleChannel(ChargePower(cmd.fraction), kRumblePulse, channel);
                                rumbling = true;
                            }
                        }
                        break;
                    case CmdType::Rumble:
                    {
                        const int power = static_cast<int>(cmd.fraction * 255.0f + 0.5f);
                        if (power <= 0)
                            break;
                        const uint8_t p = static_cast<uint8_t>(std::min(power, 255));
                        const float   t = std::clamp(cmd.seconds, kRumbleMin, kRumbleMax);
                        g_sdk.rumbleChannel(p, t, kChannelAll); // Titan: both hands
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

            if (scanning && Clock::now() >= nextScan)
            {
                const double elapsed = Seconds(Clock::now() - started);
                if (ScanDevices(seenDevices, lastJson))
                    scanning = false;
                else if (elapsed >= kScanUntil)
                {
                    scanning = false;
                    logging::Info(seenDevices == 0 ? "ProTube: no device found (switch it on before starting the game)"
                                                   : "ProTube: one device, all effects go to it");
                }
                else
                    nextScan = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(elapsed < kScanFastUntil ? kScanFastPeriod : kScanSlowPeriod));
            }
        }

        // A pulse that isn't refreshed ends by itself, but stop explicitly.
        g_sdk.rumbleChannel(0, kRumbleStop, kChannelAll);
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

        const std::wstring pluginDir = ModuleDirectory(self);   // "<TF2VR>\plugins\"
        g_libPath = pluginDir + L"lib\\ForceTubeVR_API_x64.dll";
        {
            std::wstring profile = pluginDir;
            if (!profile.empty())
                profile.pop_back();                                // strip trailing '\'
            const auto slash = profile.find_last_of(L"\\/");
            g_userDir = (slash == std::wstring::npos ? pluginDir : profile.substr(0, slash + 1)) + L"UserData\\";
        }
        // Opt-in: the ProTube DLL starts its own Bluetooth search thread as soon
        // as it's initialized, and that thread crashed some players' games on
        // startup (heap corruption inside the DLL, even without a ProTube
        // device). So the DLL is only loaded if the player asks for it.
        if (!FileExists(g_userDir + L"enable_protube.txt"))
        {
            logging::Info("ProTube support off (create TF2VR\\UserData\\enable_protube.txt to enable it)");
            return;
        }
        if (!FileExists(g_libPath))
        {
            logging::Info("ProTube support off (plugins\\lib\\ForceTubeVR_API_x64.dll not found)");
            return;
        }
        logging::Info("ProTube support enabled (UserData\\enable_protube.txt found)");

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

    void Kick(const char* weapon, const char* group, const char* hand)
    {
        if (!g_running.load())
            return;
        Command cmd;
        cmd.type   = CmdType::Kick;
        cmd.weapon = weapon ? weapon : "";
        cmd.group  = group ? group : "";
        cmd.hand   = ParseHand(hand);
        cmd.time   = Clock::now();
        PushCommand(std::move(cmd));
    }

    void Charge(float fraction, const char* hand)
    {
        if (!g_running.load())
            return;
        Command cmd;
        cmd.type     = CmdType::Charge;
        cmd.fraction = fraction;
        cmd.hand     = ParseHand(hand);
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
