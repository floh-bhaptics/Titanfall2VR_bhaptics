# Titanfall2VR_bhaptics

bHaptics and ProTube (ProVolver) support for CircuitLord's [Titanfall 2 VR mod](https://github.com/CircuitLord/CircuitLordVRModInstaller).

## Requirements

- Titanfall 2 VR, installed with CircuitLord's installer. The installer
  creates a `TF2VR` folder inside your Titanfall 2 game folder.
- For bHaptics: [bHaptics Player](https://www.bhaptics.com/software/player/)
  installed, running, and your bHaptics devices connected.
- For a ProTube ProVolver: the ProVolver paired in Windows Bluetooth and
  switched on. Close ProTube Companion before playing.

Either one works alone, or both together.

## Installation

1. Download **[Titanfall2VR_bhaptics.zip](https://github.com/floh-bhaptics/Titanfall2VR_bhaptics/releases/latest/download/Titanfall2VR_bhaptics.zip)**
   from the latest release.
2. Open your `TF2VR` folder inside the Titanfall 2 game folder, for example
   `C:\Program Files (x86)\Steam\steamapps\common\Titanfall2\TF2VR`.
   (In Steam: right-click Titanfall 2 > Manage > Browse local files.)
3. Unpack the zip **into** the `TF2VR` folder. It already contains `plugins`
   and `mods` folders; the zip adds its files to them. Allow merging the
   folders if Windows asks.
4. Start the bHaptics Player, then start the game through CircuitLord's
   installer with **Launch in VR** as usual.

After unpacking, these files should exist:

```
TF2VR\
├─ plugins\
│  ├─ Titanfall2VR_bhaptics.dll
│  └─ lib\
│     ├─ bhaptics_library.dll
│     └─ ForceTubeVR_API_x64.dll
└─ mods\
   └─ Titanfall2VR_bhaptics\
```

With bHaptics, you should feel a heartbeat shortly after the game starts.
ProTube devices buzz briefly once they are connected (see below).

Updates of the VR mod through CircuitLord's installer don't touch these
files. To update this mod, unpack the new zip the same way and allow
overwriting.

## ProTube (ProVolver / ForceTube)

1. Pair your devices in Windows Bluetooth and switch them on **before**
   starting the game. Close ProTube Companion: while it runs, it takes the
   devices away from the game.
2. Start the game and wait a few seconds: your devices buzz once they are
   connected.

Featured effects:

- Recoil on every shot. Shotguns kick at full power, automatic weapons get
  lighter kicks so the device can keep up.
- Rumble instead of a kick when you fire from a Titan, and a longer rumble when
  the Titan's shoulder rockets fire.
- Rumble that builds up while you charge the Charge Rifle or the Titan's
  Plasma Railgun.

### One device

Every effect goes to it, no matter which hand holds the weapon. Nothing to set up.

### Dual wield (two devices)

1. If you use two devices, the mod sets the hands automatically from the
   device channels: channel pistol1 is the right hand, channel pistol2 the
   left hand. When both are connected, the right device buzzes once and the
   left device twice.
   The mod saves this in the folder `TF2VR\UserData`, in the files
   `right.pro` and `left.pro`, and uses them from then on.
   The kick goes to the device in the hand that holds the weapon (the main
   hand when you hold it with both hands). Titan effects go to both devices.
   If you want to change which device is used for which hand: swap the
   physical devices, OR rename `right.pro` to `left.pro` and `left.pro` to
   `right.pro`.
2. If you use one ProVolver and one ForceTube AND want both devices to be used
   for the same hand: create an empty text file in the folder
   `TF2VR\UserData`, named either `sameHandRight.txt` or `sameHandLeft.txt`.
   Shots from the other hand then don't trigger the devices.

The files have the same format as in other ProTube mods, so you can copy
`right.pro` and `left.pro` from another game's `UserData` folder.
If there are no files and you only have one device, nothing is created and
every effect goes to all connected devices.

## Uninstalling

Delete `TF2VR\plugins\Titanfall2VR_bhaptics.dll`,
`TF2VR\plugins\lib\bhaptics_library.dll`,
`TF2VR\plugins\lib\ForceTubeVR_API_x64.dll` and the folder
`TF2VR\mods\Titanfall2VR_bhaptics`. ProTube users can also delete
`right.pro`, `left.pro` and any `sameHand*.txt` in `TF2VR\UserData`.

## Troubleshooting

- **No heartbeat at startup:** make sure the bHaptics Player is running
  before you start the game, and that the game was started with
  CircuitLord's installer, not directly from Steam.
- **Still nothing:** check the newest log file in `TF2VR\logs`. Lines from
  this mod start with `[BHAPTICS]` and usually say what went wrong.
- **ProTube device doesn't kick:** switch it on before starting the game,
  check that it's paired in Windows Bluetooth, and close ProTube Companion.
  The log shows `ProTube: device connected` or `ProTube: dual wielding ...`
  once the mod sees your devices.
- **Wrong hand with two devices:** swap the file names `right.pro` and
  `left.pro` in `TF2VR\UserData` (see [Dual wield](#dual-wield-two-devices)).
- **"Could not load bhaptics_library.dll" or "ForceTubeVR_API_x64.dll":** install the
  [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## Credits

ProTube support is based on
[titanfall2-vr-provolver](https://github.com/PurplePeanut00/titanfall2-vr-provolver)
by PurplePeanut00. The dual wield setup follows the format used by Astienth's
ProTube mods, for example
[GunmanContracts_Provolver](https://github.com/Astienth/GunmanContracts_Provolver). Titan footstep and shoulder rocket detection use techniques
from nicotrial's
[Titanfall2-SimHub-Motion-Addon](https://github.com/nicotrial/Titanfall2-SimHub-Motion-Addon).
