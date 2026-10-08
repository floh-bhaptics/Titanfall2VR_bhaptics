# Titanfall2VR_bhaptics

bHaptics support for CircuitLord's Titanfall 2 VR mod, built as a Northstar
plugin (native DLL) plus a small Northstar mod (Squirrel scripts).

Current state (v0.1.0): the plugin loads with the game, connects to the
bHaptics Player and plays one heartbeat on startup.

## Where everything goes

CircuitLord's installer puts Northstar into its own profile folder `TF2VR`
inside the game directory and launches the game with `-profile=TF2VR`.
Northstar then loads plugins and mods only from that folder:

```
G:\SteamLibrary\steamapps\common\Titanfall2\
├─ Titanfall2VRLauncher.exe            (installer, CircuitLord)
└─ TF2VR\                              Northstar profile
   ├─ Northstar.dll                    (installer)
   ├─ plugins\                         every *.dll here is loaded as a plugin
   │  ├─ Titanfall2VR.dll              (installer, the VR mod itself)
   │  ├─ Titanfall2VR_bhaptics.dll     ← this project
   │  ├─ Titanfall2VR_bhaptics.pdb     ← symbols, for crash dumps
   │  └─ lib\                          dependency folder, NOT loaded as plugins
   │     └─ bhaptics_library.dll       ← bHaptics SDK
   ├─ mods\                            Northstar mods (scripts, assets)
   │  ├─ Titanfall2VR.Cockpit\         (installer)
   │  └─ Titanfall2VR_bhaptics\        ← this project's mod
   │     ├─ mod.json
   │     └─ mod\scripts\vscripts\tf2vr_bh_client.nut
   └─ logs\nslog<date>.txt             Northstar log, our lines start with [BHAPTICS]
```

The post-build step copies all of this automatically. CircuitLord's installer
only tracks and updates its own files, so ours survive mod updates.

## Building

- Visual Studio 2022 (v143), x64, C++17.
- `lib\BHapticsWrapper.lib` + `lib\bhaptics_wrapper.h` come from the
  BHapticsWrapper project. That `.lib` is a Release `/MT` build, so this
  project uses the static release CRT (`/MT`) in both Debug and Release.
- Deployment settings are at the top of `Titanfall2VR_bhaptics.vcxproj`:
  `Tf2GameDir`, `Tf2Profile`, `DeployToGame` (set to `false` to build without copying).
- The game must be closed when building, otherwise the DLL is locked and the
  post-build step fails with "Could not copy the plugin".

## Testing

1. Start the bHaptics Player and connect the devices.
2. Build (Debug or Release).
3. Start the game through CircuitLord's installer ("Launch in VR").
4. Expected: one heartbeat shortly after the game starts (in the menu at the latest).
5. Check `TF2VR\logs\nslog<newest>.txt` for lines like:

```
[BHAPTICS] Titanfall2VR_bhaptics v0.1.0 loaded
[BHAPTICS] Loading bHaptics library from G:\...\TF2VR\plugins\lib\bhaptics_library.dll
[BHAPTICS] bHaptics library loaded, waiting for bHaptics Player...
[BHAPTICS] Connected to bHaptics Player after 312 ms
[BHAPTICS] Devices: Vest=1 ArmL=0 ArmR=0 Head=0 HandL=0 HandR=0
[BHAPTICS] Startup heartbeat played (request 1)
...
[Titanfall2VR_bhaptics] client script loaded, native plugin present
```

All playback goes through `haptics::PlaybackHaptics()`, which lower-cases the
event name before sending it to the SDK (bHaptics event names are all lower
case). So code can use readable CamelCase like `HeartBeat` or `RecoilVest_R`.

## Troubleshooting

| Log line / symptom | Meaning |
|---|---|
| No `[BHAPTICS]` lines at all | Plugin not loaded: check `TF2VR\plugins\`, and that the game was started via the TF2VR launcher, not plain Steam |
| `bHaptics disabled: could not load bhaptics_library.dll (Win32 error 126)` | DLL missing in `plugins\lib\`, or the Visual C++ 2015-2022 x64 runtime is not installed |
| `bHaptics Player still not reachable` | Player not running; the plugin keeps trying and connects when it starts |
| Heartbeat logged with request id but nothing felt | Event `heartbeat` missing in the workspace, or the vest isn't connected (see the `Devices:` line) |
| `client script loaded, native plugin NOT loaded` | Mod loaded but plugin missing or failed: check the lines above it |

Do not use the `reload_plugins` console command: it reloads all plugins,
including CircuitLord's VR plugin. Restart the game instead.

## Layout

```
Titanfall2VR_bhaptics.sln / .vcxproj
src\
  plugin.cpp     Northstar entry point: CreateInterface, PluginId, callbacks
  northstar.h    minimal mirror of the Northstar plugin ABI
  haptics.cpp    bHaptics connection lifecycle, PlaybackHaptics(), startup heartbeat
  log.cpp        logging into the Northstar console/log
lib\
  BHapticsWrapper.lib, bhaptics_wrapper.h
  bhaptics\bhaptics_library.dll, library.h
mod\Titanfall2VR_bhaptics\   Northstar mod, copied to TF2VR\mods\
```

## Next steps

- Register `BH_*` Squirrel natives in `OnSqvmCreated` (client VM).
- Wire up CircuitLord's gun-fired callback and damage events in `tf2vr_bh_client.nut`.
