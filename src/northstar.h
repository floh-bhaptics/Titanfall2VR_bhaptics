#pragma once
//
// northstar.h - minimal mirror of the Northstar plugin ABI ("plugins v4").
//
// Source of truth: R2Northstar/NorthstarLauncher, primedev/plugins/interfaces/
// Verified identical in launcher v1.31.10 and v1.31.14-rc2, i.e. covers the
// Northstar v1.31.13 that the TF2VR installer pins.
//
// These are pure interfaces: only the ORDER and SIGNATURES of the virtual
// functions matter, because Northstar calls them through the vtable.
// Never add, remove or reorder methods, and never add a virtual destructor.
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>

namespace ns
{
    // ---- Interface names ----
    constexpr const char* PLUGIN_ID_VERSION        = "PluginId001";        // we provide
    constexpr const char* PLUGIN_CALLBACKS_VERSION = "PluginCallbacks001"; // we provide
    constexpr const char* SYS_VERSION              = "NSSys001";           // Northstar provides

    // Signature of the CreateInterface export (both ours and Northstar.dll's).
    // status: 0 = OK, 1 = failed. May be null.
    using CreateInterfaceFn = void* (*)(const char* name, int* status);

    // ---- PluginId001 ----
    enum class PluginString : int
    {
        NAME            = 0, // display name
        LOG_NAME        = 1, // prefix in the console / log
        DEPENDENCY_NAME = 2, // Squirrel constant for "PluginDependencies" in mod.json
    };

    enum class PluginField : int
    {
        CONTEXT = 0, // bit flags, see PluginContext
        COLOR   = 1, // log color, 0x00BBGGRR (0 = default)
    };

    namespace PluginContext
    {
        enum : int64_t
        {
            DEDICATED = 0x1,
            CLIENT    = 0x2,
        };
    }

    class IPluginId
    {
    public:
        virtual const char* GetString(PluginString prop) = 0;
        virtual int64_t GetField(PluginField prop) = 0;
    };

    // ---- PluginCallbacks001 ----
    struct PluginNorthstarData
    {
        HMODULE  pluginHandle; // our own module handle
        uint64_t size;         // sizeof(PluginNorthstarData) as Northstar knows it
    };

    struct CSquirrelVM; // opaque for now; needed later to register BH_* natives

    class IPluginCallbacks
    {
    public:
        virtual void Init(HMODULE northstarModule, const PluginNorthstarData* initData, bool reloaded) = 0;
        virtual void Finalize() = 0;      // after all plugins are loaded
        virtual bool Unload() = 0;        // return false to refuse unloading
        virtual void OnSqvmCreated(CSquirrelVM* sqvm) = 0;
        virtual void OnSqvmDestroying(CSquirrelVM* sqvm) = 0;
        virtual void OnLibraryLoaded(HMODULE module, const char* name) = 0;
        virtual void RunFrame() = 0;      // every host frame, on the game thread
    };

    // ---- NSSys001 (provided by Northstar.dll) ----
    enum class LogLevel : int
    {
        INFO = 0,
        WARN = 1,
        ERR  = 2,
    };

    class ISys
    {
    public:
        // Northstar identifies the calling plugin from the return address, so
        // Log must be called from code inside this DLL (it is, via log.cpp).
        virtual void Log(int64_t pluginHandle, LogLevel level, char* msg) = 0;
        virtual void Unload(int64_t pluginHandle) = 0;
        virtual void Reload(int64_t pluginHandle) = 0;
    };
}
