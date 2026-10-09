# Titanfall2VR_bhaptics: build notes

For players: see `README.md` (installation only). This file is for development.

bHaptics support for CircuitLord's Titanfall 2 VR mod, built as a Northstar
plugin (native DLL) plus a small Northstar mod (Squirrel scripts).

Current state (v0.4.1): the plugin connects to the bHaptics Player, plays a
heartbeat on startup, and gives the game scripts `BH_*` functions. The mod's
scripts use them for damage, health, death, movement, Titan events, recoil,
explosions and Titan melee.

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
   │  └─ lib\                          dependency folder, NOT loaded as plugins
   │     └─ bhaptics_library.dll       ← bHaptics SDK
   ├─ mods\                            Northstar mods (scripts, assets)
   │  ├─ Titanfall2VR.Cockpit\         (installer)
   │  └─ Titanfall2VR_bhaptics\        ← this project's mod
   │     ├─ mod.json
   │     └─ mod\scripts\vscripts\
   │        ├─ tf2vr_bh_client.nut     client hooks (damage, health, spawn, embark)
   │        └─ tf2vr_bh_server.nut     server hooks (death, shield, movement, Titan)
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
- Every build also creates the release package
  `bin\x64\<Config>\Titanfall2VR_bhaptics.zip`, containing `plugins\` and
  `mods\` exactly as they go into the `TF2VR` folder. Upload it to the GitHub
  release under that file name, so the download link in `README.md` keeps working.
  Packaging uses `tar`, which ships with Windows 10 (1803+) and 11.
- The game must be closed when building, otherwise the DLL is locked and the
  post-build step fails with "Could not copy the plugin".

## Testing

1. Start the bHaptics Player and connect the devices.
2. Build (Debug or Release).
3. Start the game through CircuitLord's installer ("Launch in VR").
4. Expected: one heartbeat shortly after the game starts (in the menu at the latest).
5. Check `TF2VR\logs\nslog<newest>.txt` for lines like:

```
[BHAPTICS] Titanfall2VR_bhaptics v0.3.0 loaded
[BHAPTICS] [DEBUG] Loading bHaptics library from G:\...\TF2VR\plugins\lib\bhaptics_library.dll
[BHAPTICS] [DEBUG] bHaptics library loaded, waiting for bHaptics Player...
[BHAPTICS] Connected to bHaptics Player after 312 ms
[BHAPTICS] [DEBUG] Devices: Vest=1 ArmL=0 ArmR=0 Head=0 HandL=0 HandR=0
[BHAPTICS] [DEBUG] Play 'heartbeat' (intensity 1.00, duration 1.00, angle 0, offsetY 0.00) -> request 1
...
[Titanfall2VR_bhaptics] client script loaded, native plugin present
```

### Log level

Set in `src/log.h`:

```cpp
constexpr Level kMinLevel = Level::Debug;   // development
constexpr Level kMinLevel = Level::Info;    // public release
```

`Debug` logs every played event with its parameters, so a misspelled event
name in the bHaptics portal shows up as a `Play '...'` line without a felt
effect. `Info` keeps only "loaded" and "connected", plus warnings and errors.

All playback goes through `haptics::PlaybackHaptics()`, which lower-cases the
event name before sending it to the SDK (bHaptics event names are all lower
case). So code can use readable CamelCase like `HeartBeat` or `RecoilVest_R`.

## bHaptics events

All names are lower case, as they must be in the bHaptics portal.

| Event | Trigger | Script side |
|---|---|---|
| `heartbeat` | Plugin startup; loop every 1 s while pilot health < 25 % | plugin / client |
| `impact` | Taking damage, rotated towards the damage source (`angleX`) | client |
| `healing` | Start of a health regeneration phase (max. once per second) | client |
| `shield_damage` | Shield damage on the player (Titan shield) | server |
| `player_killed` | Player died (also stops heartbeat and zipline loops) | server |
| `player_spawned` | Player spawned (level load, checkpoint, restart) | client |
| `player_jump` | Jump and double jump | server |
| `player_dodge` | Dodge (also Titan dash) | server |
| `player_land` | Touching the ground | server |
| `player_mantle` | Mantling | server |
| `begin_wallrun` / `end_wallrun` | Wallrun start / end | server |
| `zipline` | Loop every 200 ms while on a zipline | server |
| `player_enter_titan` | Titan cockpit created (embark) | client |
| `player_exit_titan` | Leaving the Titan | server |
| `titan_hit` | Embarked Titan loses a health segment | server |
| `titan_destroyed` | Our Titan (embarked or BT as auto-titan) is destroyed | server |
| `recoil_pistol_r` / `_l` | Shot with a pistol or SMG, right / left hand | client |
| `recoil_rifle_r` / `_l` | Shot with a rifle or LMG | client |
| `recoil_shotgun_r` / `_l` | Shot with a shotgun, sniper or launcher | client |
| `recoil_titan` | Shot with a Titan weapon that has a magazine (incl. Burst Core) | client |
| `explosion` | Grenade/rocket detonating within ~38 m (intensity by distance, directional), or explosive damage on the player | client |
| `titan_melee` | Titan punch or sword swing | client |

Recoil: shots are detected when the active weapon's magazine count drops
(no game hook involved). The hand comes from the VR mod's
`TF2VR_WeaponHand()` (0 = left, 1 = right; the main hand when held with
both hands). The weapon groups are in `BH_RecoilGroup()` in the client
script; unknown weapons fall back to `rifle` and are logged. Weapons
without a magazine (charge weapons) play no recoil.

Tuning constants (loop intervals, low-health threshold) are at the top of the
two `.nut` files.

Note: the game's Squirrel compiler refuses to pass typed callbacks through
`var` parameters, so hooks can't be looked up by name at runtime. They are
called directly.

## Script API (natives from the plugin)

Available in CLIENT and SERVER scripts when the plugin is loaded
(wrap usage in `#if TF2VR_BHAPTICS`):

```squirrel
void BH_Play( string eventName )
void BH_PlayParam( string eventName, float intensity, float duration, float angleX, float offsetY )
bool BH_IsConnected()
void BH_Debug( string message )   // plugin log, only with Level::Debug
void BH_Warn( string message )
```

The natives are registered through the game's own Squirrel registration
function (offsets from NorthstarLauncher, see `src/squirrel.cpp`).

## First test checklist

With `Level::Debug`, the log should show:

1. `Registered 5 BH_* natives in SERVER VM` and `... in CLIENT VM` on level load.
2. `[SERVER script] server script init` and `[CLIENT script] client script init`.
3. No `COMPILE ERROR` lines. The hooks come from Northstar's multiplayer
   scripts; if one doesn't exist in the campaign, the game names it in a
   SERVER or CLIENT script compile error and returns to the main menu.
   That hook then has to be removed or replaced.
4. `Health ... -> ...` lines while taking damage and regenerating, and a
   `Play '...'` line for each event.

## Troubleshooting

| Log line / symptom | Meaning |
|---|---|
| No `[BHAPTICS]` lines at all | Plugin not loaded: check `TF2VR\plugins\`, and that the game was started via the TF2VR launcher, not plain Steam |
| `bHaptics disabled: could not load bhaptics_library.dll (Win32 error 126)` | DLL missing in `plugins\lib\`, or the Visual C++ 2015-2022 x64 runtime is not installed |
| `bHaptics Player still not reachable` | Player not running; the plugin keeps trying and connects when it starts |
| `Play '...'` logged but nothing felt | Event name missing or misspelled in the workspace, or the device isn't connected (see the `Devices:` line) |
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
  squirrel.cpp   BH_* natives, registered in the CLIENT and SERVER script VMs
  log.cpp        logging into the Northstar console/log
lib\
  BHapticsWrapper.lib, bhaptics_wrapper.h
  bhaptics\bhaptics_library.dll, library.h
mod\Titanfall2VR_bhaptics\   Northstar mod, copied to TF2VR\mods\
```

## Next steps

- Recoil for weapons without a magazine.
- Titan footsteps: the engine plays the native `titan_cockpit_footstep`
  rumble, no script hook exists. Needs a native rumble hook (CircuitLord).
