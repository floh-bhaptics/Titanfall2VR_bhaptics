//
// plugin.cpp - Northstar plugin entry point for Titanfall2VR_bhaptics.
//
// Northstar loads every *.dll in <profile>\plugins\, calls our exported
// CreateInterface() to get the two interfaces below, and then drives the
// plugin through the IPluginCallbacks methods.
//

#include "northstar.h"
#include "log.h"
#include "haptics.h"
#include "squirrel.h"

#include <cstring>

namespace
{
    constexpr const char* kPluginName     = "Titanfall2VR_bhaptics";
    constexpr const char* kPluginVersion  = "0.4.1";
    constexpr const char* kLogName        = "BHAPTICS";
    // Squirrel constant for mod.json "PluginDependencies". Must be a valid
    // Squirrel identifier, otherwise Northstar refuses to load the plugin.
    constexpr const char* kDependencyName = "TF2VR_BHAPTICS";
    // Log color 0x00BBGGRR: light blue.
    constexpr int64_t kLogColor = 0x40 | (0xC4 << 8) | (0xFF << 16);

    HMODULE g_self = nullptr;

    class PluginId final : public ns::IPluginId
    {
    public:
        const char* GetString(ns::PluginString prop) override
        {
            switch (prop)
            {
                case ns::PluginString::NAME:            return kPluginName;
                case ns::PluginString::LOG_NAME:        return kLogName;
                case ns::PluginString::DEPENDENCY_NAME: return kDependencyName;
            }
            return nullptr;
        }

        int64_t GetField(ns::PluginField prop) override
        {
            switch (prop)
            {
                case ns::PluginField::CONTEXT: return ns::PluginContext::CLIENT; // never on dedicated servers
                case ns::PluginField::COLOR:   return kLogColor;
            }
            return 0;
        }
    };

    class PluginCallbacks final : public ns::IPluginCallbacks
    {
    public:
        void Init(HMODULE northstarModule, const ns::PluginNorthstarData* initData, bool reloaded) override
        {
            if (initData && initData->pluginHandle)
                g_self = initData->pluginHandle;

            logging::Init(northstarModule, g_self);
            logging::Info("%s v%s loaded%s", kPluginName, kPluginVersion, reloaded ? " (reloaded)" : "");

            haptics::Startup(g_self);
        }

        void Finalize() override {}

        bool Unload() override
        {
            haptics::Shutdown();
            logging::Debug("%s unloading", kPluginName);
            return true;
        }

        void OnSqvmCreated(ns::CSquirrelVM* vm) override
        {
            squirrel::OnVmCreated(vm);
        }

        void OnSqvmDestroying(ns::CSquirrelVM* vm) override
        {
            squirrel::OnVmDestroying(vm);
        }

        void OnLibraryLoaded(HMODULE, const char*) override {}

        void RunFrame() override
        {
            haptics::Frame();
        }
    };

    PluginId        g_pluginId;
    PluginCallbacks g_callbacks;
}

extern "C" __declspec(dllexport) void* CreateInterface(const char* name, int* status)
{
    void* result = nullptr;

    if (name && std::strcmp(name, ns::PLUGIN_ID_VERSION) == 0)
        result = static_cast<ns::IPluginId*>(&g_pluginId);
    else if (name && std::strcmp(name, ns::PLUGIN_CALLBACKS_VERSION) == 0)
        result = static_cast<ns::IPluginCallbacks*>(&g_callbacks);

    if (status)
        *status = result ? 0 : 1;
    return result;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
