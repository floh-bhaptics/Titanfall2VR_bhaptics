# Titanfall2VR_bhaptics

bHaptics support for CircuitLord's [Titanfall 2 VR mod](https://github.com/CircuitLord/CircuitLordVRModInstaller).

## Requirements

- Titanfall 2 VR, installed with CircuitLord's installer. The installer
  creates a `TF2VR` folder inside your Titanfall 2 game folder.
- [bHaptics Player](https://www.bhaptics.com/software/player/) installed,
  running, and your bHaptics devices connected.

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
│     └─ bhaptics_library.dll
└─ mods\
   └─ Titanfall2VR_bhaptics\
```

You should feel a heartbeat shortly after the game starts.

Updates of the VR mod through CircuitLord's installer don't touch these
files. To update this mod, unpack the new zip the same way and allow
overwriting.

## Uninstalling

Delete `TF2VR\plugins\Titanfall2VR_bhaptics.dll`,
`TF2VR\plugins\lib\bhaptics_library.dll` and the folder
`TF2VR\mods\Titanfall2VR_bhaptics`.

## Troubleshooting

- **No heartbeat at startup:** make sure the bHaptics Player is running
  before you start the game, and that the game was started with
  CircuitLord's installer, not directly from Steam.
- **Still nothing:** check the newest log file in `TF2VR\logs`. Lines from
  this mod start with `[BHAPTICS]` and usually say what went wrong.
- **"Could not load bhaptics_library.dll":** install the
  [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).
