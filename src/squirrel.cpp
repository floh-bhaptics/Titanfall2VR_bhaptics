//
// squirrel.cpp - BH_* natives for the CLIENT and SERVER Squirrel VMs.
//
// Northstar's plugin API hands us the CSquirrelVM on creation but no API to
// register functions, so we call the game's own registration function
// directly, like Northstar itself does. All offsets below come from
// R2Northstar/NorthstarLauncher primedev/squirrel/squirrel.cpp and are
// identical in launcher v1.31.10 through v1.31.14 (they point into the
// game's client.dll / server.dll, which haven't changed in years).
//
// In the campaign, server and client run in the same process, so server
// scripts can call bHaptics directly through the same natives.
//

#include "squirrel.h"
#include "haptics.h"
#include "log.h"

#include <cstddef>
#include <cstdint>

namespace
{
    // ---- Minimal Squirrel ABI (primedev/vscript/languages/squirrel_re) ----
    struct SQVM;
    using HSQUIRRELVM = SQVM*;
    using SQInteger   = long;          // 32 bit on Windows, as in Respawn's build
    using SQFloat     = float;
    using SQBool      = unsigned long;
    using SQChar      = char;

    enum SQRESULT : SQInteger
    {
        SQRESULT_ERROR   = -1,
        SQRESULT_NULL    = 0, // function returns nothing
        SQRESULT_NOTNULL = 1, // function pushed a return value
    };

    using SQFunction = SQRESULT (*)(HSQUIRRELVM);

    enum class eSQReturnType : int
    {
        Float   = 0x1,
        Integer = 0x5,
        Boolean = 0x6,
        Default = 0x20, // void
        String  = 0x21,
    };

    struct SQFuncRegistration
    {
        const char*   squirrelFuncName;
        const char*   cppFuncName;
        const char*   helpText;
        const char*   returnTypeString;
        const char*   argTypes;
        uint32_t      unknown1;
        uint32_t      devLevel;
        const char*   shortNameMaybe;
        uint32_t      unknown2;
        eSQReturnType returnType;
        uint32_t*     externalBufferPointer;
        uint64_t      externalBufferSize;
        uint64_t      unknown3;
        uint64_t      unknown4;
        SQFunction    funcPtr;
    };
    static_assert(offsetof(SQFuncRegistration, returnType) == 0x3C, "SQFuncRegistration layout");
    static_assert(offsetof(SQFuncRegistration, funcPtr) == 0x60, "SQFuncRegistration layout");
    static_assert(sizeof(SQFuncRegistration) == 0x68, "SQFuncRegistration layout");

    // The one field we need from CSquirrelVM (744 bytes in total):
    // int32 vmContext at 0x3C (0 = SERVER, 1 = CLIENT, 2 = UI).
    constexpr size_t kCSquirrelVM_vmContext = 0x3C;

    enum Context : int
    {
        CTX_SERVER = 0,
        CTX_CLIENT = 1,
        CTX_UI     = 2,
        CTX_COUNT
    };

    const char* ContextName(int ctx)
    {
        switch (ctx)
        {
            case CTX_SERVER: return "SERVER";
            case CTX_CLIENT: return "CLIENT";
            case CTX_UI:     return "UI";
        }
        return "?";
    }

    // ---- Game function pointers, per DLL ----
    using RegisterSquirrelFuncFn = int64_t (*)(ns::CSquirrelVM* vm, SQFuncRegistration* reg, char unknown);
    using GetStringFn            = const SQChar* (*)(HSQUIRRELVM, SQInteger stackPos);
    using GetFloatFn             = SQFloat (*)(HSQUIRRELVM, SQInteger stackPos);
    using PushBoolFn             = void (*)(HSQUIRRELVM, SQBool value);

    struct Offsets
    {
        const wchar_t* module;
        uintptr_t registerFunc;
        uintptr_t getString;
        uintptr_t getFloat;
        uintptr_t pushBool;
    };

    constexpr Offsets kServerOffsets = { L"server.dll", 0x1DD10, 0x60A0, 0x60E0, 0x3710 };
    constexpr Offsets kClientOffsets = { L"client.dll", 0x108E0, 0x60C0, 0x6100, 0x3710 };

    struct Api
    {
        bool                   ready        = false;
        RegisterSquirrelFuncFn registerFunc = nullptr;
        GetStringFn            getString    = nullptr;
        GetFloatFn             getFloat     = nullptr;
        PushBoolFn             pushBool     = nullptr;
    };

    Api g_api[CTX_COUNT]; // indexed by Context; UI stays unused

    bool ResolveApi(int ctx)
    {
        Api& api = g_api[ctx];
        if (api.ready)
            return true;

        const Offsets& o = (ctx == CTX_SERVER) ? kServerOffsets : kClientOffsets;
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(o.module));
        if (!base)
        {
            logging::Error("Squirrel: %s not loaded, natives unavailable in %s VM", ctx == CTX_SERVER ? "server.dll" : "client.dll", ContextName(ctx));
            return false;
        }

        api.registerFunc = reinterpret_cast<RegisterSquirrelFuncFn>(base + o.registerFunc);
        api.getString    = reinterpret_cast<GetStringFn>(base + o.getString);
        api.getFloat     = reinterpret_cast<GetFloatFn>(base + o.getFloat);
        api.pushBool     = reinterpret_cast<PushBoolFn>(base + o.pushBool);
        api.ready        = true;
        return true;
    }

    // ---- Natives ----
    // Argument 1 is at stack position 1 (Northstar convention).

    template <int Ctx>
    SQRESULT Native_Play(HSQUIRRELVM vm)
    {
        const Api& api = g_api[Ctx];
        haptics::PlaybackHaptics(api.getString(vm, 1));
        return SQRESULT_NULL;
    }

    template <int Ctx>
    SQRESULT Native_PlayParam(HSQUIRRELVM vm)
    {
        const Api& api = g_api[Ctx];
        haptics::PlaybackHaptics(api.getString(vm, 1),
                                 api.getFloat(vm, 2),
                                 api.getFloat(vm, 3),
                                 api.getFloat(vm, 4),
                                 api.getFloat(vm, 5));
        return SQRESULT_NULL;
    }

    template <int Ctx>
    SQRESULT Native_IsConnected(HSQUIRRELVM vm)
    {
        g_api[Ctx].pushBool(vm, haptics::IsConnected() ? 1 : 0);
        return SQRESULT_NOTNULL;
    }

    template <int Ctx>
    SQRESULT Native_Debug(HSQUIRRELVM vm)
    {
        if (logging::Enabled(logging::Level::Debug))
            logging::Debug("[%s script] %s", ContextName(Ctx), g_api[Ctx].getString(vm, 1));
        return SQRESULT_NULL;
    }

    template <int Ctx>
    SQRESULT Native_Warn(HSQUIRRELVM vm)
    {
        logging::Warn("[%s script] %s", ContextName(Ctx), g_api[Ctx].getString(vm, 1));
        return SQRESULT_NULL;
    }

    // ---- Registration table ----

    struct NativeDef
    {
        const char* name;
        const char* returnType;
        eSQReturnType returnTypeId;
        const char* args;
        const char* help;
        SQFunction  server;
        SQFunction  client;
    };

#define TF2VR_BH_NATIVE(fn) &fn<CTX_SERVER>, &fn<CTX_CLIENT>

    const NativeDef kNatives[] = {
        { "BH_Play",        "void", eSQReturnType::Default, "string eventName",
          "Play a bHaptics event (name is lower-cased)", TF2VR_BH_NATIVE(Native_Play) },
        { "BH_PlayParam",   "void", eSQReturnType::Default, "string eventName, float intensity, float duration, float angleX, float offsetY",
          "Play a bHaptics event with intensity/duration multipliers and vest rotation/offset", TF2VR_BH_NATIVE(Native_PlayParam) },
        { "BH_IsConnected", "bool", eSQReturnType::Boolean, "",
          "True if the bHaptics Player is connected", TF2VR_BH_NATIVE(Native_IsConnected) },
        { "BH_Debug",       "void", eSQReturnType::Default, "string message",
          "Write to the bHaptics plugin log (debug level)", TF2VR_BH_NATIVE(Native_Debug) },
        { "BH_Warn",        "void", eSQReturnType::Default, "string message",
          "Write a warning to the bHaptics plugin log", TF2VR_BH_NATIVE(Native_Warn) },
    };

#undef TF2VR_BH_NATIVE

    constexpr size_t kNativeCount = sizeof(kNatives) / sizeof(kNatives[0]);

    // The game keeps pointers to these, so they need static storage.
    SQFuncRegistration g_regs[CTX_COUNT][kNativeCount] = {};
}

namespace squirrel
{
    void OnVmCreated(ns::CSquirrelVM* vm)
    {
        if (!vm)
            return;

        const int ctx = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(vm) + kCSquirrelVM_vmContext);
        if (ctx != CTX_SERVER && ctx != CTX_CLIENT)
            return; // UI VM: nothing to do

        if (!ResolveApi(ctx))
            return;

        for (size_t i = 0; i < kNativeCount; ++i)
        {
            const NativeDef& def = kNatives[i];
            SQFuncRegistration& reg = g_regs[ctx][i];

            reg = SQFuncRegistration{};
            reg.squirrelFuncName = def.name;
            reg.cppFuncName      = def.name;
            reg.helpText         = def.help;
            reg.returnTypeString = def.returnType;
            reg.argTypes         = def.args;
            reg.returnType       = def.returnTypeId;
            reg.funcPtr          = (ctx == CTX_SERVER) ? def.server : def.client;

            g_api[ctx].registerFunc(vm, &reg, 1);
        }

        logging::Debug("Registered %zu BH_* natives in %s VM", kNativeCount, ContextName(ctx));
    }

    void OnVmDestroying(ns::CSquirrelVM* vm)
    {
        if (!vm)
            return;

        const int ctx = *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(vm) + kCSquirrelVM_vmContext);
        if (ctx == CTX_CLIENT)
        {
            // Level unload / quit to menu: cut anything still playing.
            haptics::StopAll();
        }
    }
}
