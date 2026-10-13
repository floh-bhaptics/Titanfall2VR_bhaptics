ForceTubeVR_API_x64.dll - ProTubeVR native SDK (ProVolver / ForceTube devices)

Source:  https://github.com/ProTubeVR/ForceTubeVR-Unity-Package
Commit:  2873daf436296d9ea84013fef41c7ff8a4b379e5
Path:    new/Assets/Plugins/Windows/ForceTubeVR_API_x64.dll
SHA-256: 1f5acd7e815ad4f6289f254f8452c86732d46baefc5e975b2202d569007c5b75
Size:    164,352 bytes

Redistributed with this mod with ProTubeVR's permission.
Same binary as tested with PurplePeanut00's titanfall2-vr-provolver bridge.

Loaded at runtime by src/protube.cpp (LoadLibrary/GetProcAddress, no import
lib needed). Requires the Microsoft Visual C++ 2015-2022 x64 runtime.

Function signatures used (x64, plain C exports, see ForceTubeVRInterface.cs):
  void    InitPistol();
  void    SetActive(bool active);      // called with an int 0/1 (bool size unknown)
  void    KickChannel(uint8_t power, int channel);
  void    RumbleChannel(uint8_t power, float seconds, int channel);  // 0.0 s = endless!
  uint8_t TempoToKickPower(float seconds);
Channels: 0 all, 1 rifle, 2 rifleButt, 3 rifleBolt, 4 pistol1, 5 pistol2, 6 other, 7 vest
The plugin sends everything to channel 0 (all connected devices).
