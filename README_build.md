# Titanfall2VR_bhaptics: build notes

For players: see `README.md` (installation only). This file is for development.

bHaptics and ProTube (ProVolver) support for CircuitLord's Titanfall 2 VR mod,
built as a Northstar plugin (native DLL) plus a small Northstar mod (Squirrel scripts).

Current state (v1.5.0): the plugin connects to the bHaptics Player, plays a
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
   │     ├─ bhaptics_library.dll       ← bHaptics SDK
   │     └─ ForceTubeVR_API_x64.dll    ← ProTube SDK (optional)
   ├─ mods\                            Northstar mods (scripts, assets)
   │  ├─ Titanfall2VR.Cockpit\         (installer)
   │  └─ Titanfall2VR_bhaptics\        ← this project's mod
   │     ├─ mod.json
   │     └─ mod\scripts\vscripts\
   │        ├─ tf2vr_bh_client.nut     client hooks (damage, health, spawn, embark)
   │        ├─ tf2vr_bh_server.nut     server hooks (death, shield, movement, Titan)
   │        ├─ tf2vr_bh_timeshift_server.nut  timeshift device (untyped, see file)
   │        └─ tf2vr_pt_client.nut     ProTube recoil and charge rumble
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
[BHAPTICS] Titanfall2VR_bhaptics v1.5.0 loaded
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
| `explosion` | Explosive damage on the player; max. one per 0.3 s. (Projectile tracking via `tf2vr_bh_missiles_client.nut` is disabled since v0.5.1, see the file.) | client |
| `titan_melee` | Titan punch or sword swing | client |
| `titan_footstep` | Own Titan's footfall: walk animation cycle crosses 0.0 / 0.5 while moving on the ground | client |
| `titanfall` | Any Titan (incl. our own) landing after a fast fall, within ~100 m; intensity by distance, directional | client |
| `timeshift_charge` | "Effect and Cause": the player uses the timeshift device, start of the ~0.5 s wind-up before the jump (`AddCallback_OnTimeShiftAbilityUsed`, in the untyped `tf2vr_bh_timeshift_server.nut`) | server |
| `timeshift` | "Effect and Cause": the player switches timelines, manual or scripted (position jumps > 1500 units within one frame; past and present are stacked copies of the level). The debug log shows the offset, its z sign tells past from present | client |
| `draw_primary_l` / `_r` | Primary gun drawn from the shoulder holster, left / right hand (VR mod `tf2vr_draw` command, acked via `tf2vr_gun_ack`) | server + client |
| `draw_secondary_l` / `_r` | Secondary gun or arc tool drawn from the hip holster | server + client |
| `grenade_take` | Grenade taken from the chest (`tf2vr_grenade` command, acked via `tf2vr_grenade_ack`) | server |
| `grenade_store` | Held grenade put back on the chest (`tf2vr_store_grenade` command) | server |
| `grenade_throw` | Grenade thrown (`tf2vr_projectile_ticket` changes) | client |
| `reload_magazine_out` | Magazine pulled out (`tf2vr_reload_magazine_out` counts up) | client |
| `reload_magazine_in` | Clip count of the active pilot weapon goes up: magazine inserted (one event per shell for shell-by-shell reloads) | client |
| `reload_rack` | Slide/bolt racked with a round in it (`tf2vr_reload_rounds` counts up). `tf2vr_reload_ejections` is only logged for now | client |
| `titan_hover` | VTOL hover used (Northstar loadout, ~3 s of hovering): ability ammo drops or cooldown starts | client |
| `titan_flight` | Repeats every 0.5 s while Flight Core is active (active weapon `mp_titanweapon_flightcore_rockets`) | client |
| `titan_core_ready` | Titan core becomes available (`IsCoreChargeAvailable`) | client |
| `titan_core_charge` | Repeats every 0.2 s during the core's activation wind-up; intensity 0.3-1.0 follows the core weapon's charge fraction | client |
| `titan_shield_start` | Vortex shield (incl. Ion's) or Scorch's heat shield goes up: it becomes the Titan's active weapon | client |
| `titan_core_active` | Core starts (`SoulTitanCore_GetExpireTime( soul ) > Time()`) | client |
| `recoil_titan_rockets` | Own Titan fires shoulder rockets: once at the start of each salvo (ordnance slot: clip drop or cooldown start, then 3 s quiet period). Ordnance fired as the active weapon (Tone's tracker rockets) plays `recoil_titan` instead | client |

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

## ProTube (ProVolver)

Recoil kicks and charge-up rumble for ProTube devices (ProVolver, ForceTube),
independent of bHaptics: both can be used at the same time, or either alone.

- `src/protube.cpp` loads `plugins\lib\ForceTubeVR_API_x64.dll` at runtime
  (`LoadLibrary`/`GetProcAddress`, no wrapper or import lib). If the DLL is
  missing, ProTube support is off and `PT_IsAvailable()` returns false; the
  script then doesn't start. The DLL is never unloaded, the SDK keeps its own
  threads until the game exits. Source, hash and signatures: `lib\protube\README.txt`.
- Same worker-thread model as bHaptics, but a separate worker, so a stuck
  Bluetooth call can't delay the vest and vice versa.
- `InitPistol()` puts the connected devices into the channels `pistol1`,
  `pistol2`, `rifleButt`, `rifleBolt`, `other`, `vest`, in the order Windows
  finds them. The worker then reads `ListChannels()` (every second for 20 s,
  then every 5 s up to 2 min) until `pistol1` and `pistol2` are both taken:
  - **One device:** everything goes to channel 0 (`all`). Nothing is saved.
  - **Two devices:** right hand = channel 4 (`pistol1`), left hand = channel 5
    (`pistol2`). Saved as `<TF2VR>\UserData\right.pro` / `left.pro`
    (plain UTF-8 text with the device name, e.g. `ForceTubeVR 616241100`),
    the same format as Astienth's and floh's earlier ProTube mods. On later
    starts the saved devices are put back into channels 4/5 with
    `ClearChannel`/`AddToChannel`. If the saved names don't match the
    connected devices, the files are rewritten.
  - **`sameHandRight.txt` / `sameHandLeft.txt`** in `UserData`: both devices
    act as that hand (channel 0); kicks from the other hand are skipped.
  - Kicks and charge rumble follow `TF2VR_WeaponHand()`; Titan rumble always
    goes to channel 0. The devices buzz once they are sorted (right once,
    left twice), so players can check the assignment.
  - `ListChannels()` returns a `CoTaskMemAlloc`'d string (the DLL imports only
    that from ole32, and C# interop frees it the same way), so we
    `CoTaskMemFree` it.
- Kick power (ported from the ProVolver bridge's scheduler): shotguns
  (`group "full"`) always 255. Other weapons use the SDK's `TempoToKickPower()`
  with a recovery time adapted to the fire rate: start 130 ms (`"heavy"`) or
  60 ms, after two shots the fastest of the last six intervals minus 8 ms,
  clamped to 50-130 ms. A kick that arrives while the device is still
  recovering waits up to 40 ms, then it's dropped.
- Charge rumble: Charge Rifle (`mp_weapon_defender`, attack held) and Plasma
  Railgun (`mp_titanweapon_sniper`, aiming). Power 32-255 by charge. Every
  `PT_Charge()` call is a 100 ms pulse; the script refreshes it every 50 ms,
  so it stops by itself if the script stops (pause, load). Never pass 0 s to
  `RumbleChannel`: that means endless rumble.
- Titan weapons rumble instead of kicking (`PT_Rumble`): a short pulse per
  shot, a longer one at the start of a shoulder rocket salvo. Power and length
  are constants at the top of `tf2vr_pt_client.nut`.
- Detection in `tf2vr_pt_client.nut` is based on PurplePeanut00's script, with
  the log-file output replaced by `PT_*` calls. It tracks the magazines of all
  main weapons, including holstered ones, so the first shot right after a
  weapon switch is caught. To drop ProTube support, delete that file and its
  `mod.json` entry (and the DLL).

ProTube log lines (all `[BHAPTICS]`): `ProTube SDK loaded`, or
`ProTube support off (...not found)`; with Debug level also
`ProTube kick <weapon> power <n> recovery <ms> channel <c>` per kick,
`ProTube channels: {...}` whenever the SDK's channel list changes, and
`ProTube rumble power <n> for <ms> ms` per Titan shot.

## Threading

All bHaptics SDK calls run on one worker thread (`src/haptics.cpp`). The game
thread only queues events (max. 32 pending, oldest dropped) and reads a cached
connection flag, so a slow or hanging bHaptics library/Player can't freeze the
game. Slow SDK calls (>100 ms) and hangs (>2 s) are logged as warnings.

Titan footsteps and shoulder rocket detection use techniques from the
[Titanfall2-SimHub-Motion-Addon](https://github.com/nicotrial/Titanfall2-SimHub-Motion-Addon)
by nicotrial. ProTube shot and charge detection comes from
[titanfall2-vr-provolver](https://github.com/PurplePeanut00/titanfall2-vr-provolver)
by PurplePeanut00. The dual wield files follow the format of Astienth's
ProTube mods ([GunmanContracts_Provolver](https://github.com/Astienth/GunmanContracts_Provolver)).

## Script API (natives from the plugin)

Available in CLIENT and SERVER scripts when the plugin is loaded
(wrap usage in `#if TF2VR_BHAPTICS`):

```squirrel
void BH_Play( string eventName )
void BH_PlayParam( string eventName, float intensity, float duration, float angleX, float offsetY )
bool BH_IsConnected()
void BH_Debug( string message )   // plugin log, only with Level::Debug
void BH_Warn( string message )

void PT_Kick( string weaponClass, string group, string hand )  // group: "full", "heavy", or other
void PT_Charge( float fraction, string hand )      // 0..1, 100 ms pulse, <= 0 stops
void PT_Rumble( float power, float seconds )       // 0..1, 0.03..2 s (Titan weapons)
void PT_Stop()
bool PT_IsAvailable()                              // ProTube DLL found
```

The natives are registered through the game's own Squirrel registration
function (offsets from NorthstarLauncher, see `src/squirrel.cpp`).

## First test checklist

With `Level::Debug`, the log should show:

1. `Registered 10 BH_*/PT_* natives in SERVER VM` and `... in CLIENT VM` on level load.
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
| `ProTube disabled: could not load ForceTubeVR_API_x64.dll (Windows error 126)` | Visual C++ 2015-2022 x64 runtime missing |
| `ProTube kick ...` logged but nothing felt | ProVolver off, not paired in Windows Bluetooth, or ProTube Companion still running (close it) |
| SERVER script compile error about `AddClientCommandNotifyCallback` | `Northstar.CustomServers` is missing in `TF2VR\mods` (it provides that function); the draw and chest grenade effects need it |
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
  protube.cpp    ProTube SDK loading, kick scheduler, charge rumble
  squirrel.cpp   BH_* and PT_* natives, registered in the CLIENT and SERVER script VMs
  log.cpp        logging into the Northstar console/log
lib\
  BHapticsWrapper.lib, bhaptics_wrapper.h
  bhaptics\bhaptics_library.dll, library.h
  protube\ForceTubeVR_API_x64.dll, README.txt
mod\Titanfall2VR_bhaptics\   Northstar mod, copied to TF2VR\mods\
```

## Next steps

- Holstering a gun: the VR mod handles "release grip to holster" in its
  native code only (internal `gun_held` state). Needs a convar or command
  from CircuitLord, then `holster_primary` / `holster_secondary` are easy.

- Recoil for weapons without a magazine.
- Rocket/grenade detonations near the player (the client missile hook crashed
  the game on `crossbow_bolt` projectiles; a server-side approach may work).
