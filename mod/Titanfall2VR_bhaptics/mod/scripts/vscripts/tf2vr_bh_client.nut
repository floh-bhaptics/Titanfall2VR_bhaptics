// Client-side bHaptics glue for Titanfall 2 VR.
//
// TF2VR_BHAPTICS is a compile-time constant that Northstar defines because
// mod.json lists it under "PluginDependencies": true if the native plugin
// Titanfall2VR_bhaptics.dll is loaded, false otherwise. Everything that calls
// into the plugin goes inside #if TF2VR_BHAPTICS, so the mod stays harmless
// without the plugin.

global function TF2VR_BH_ClientInit

void function TF2VR_BH_ClientInit()
{
#if TF2VR_BHAPTICS
	printt( "[Titanfall2VR_bhaptics] client script loaded, native plugin present" )
#else
	printt( "[Titanfall2VR_bhaptics] client script loaded, native plugin NOT loaded - haptics disabled" )
#endif
}
