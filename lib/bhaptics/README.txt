# lib/bhaptics/

bHaptics SDK2 files, kept here so the source tree is self-contained and you
always know which version you built against.

  bhaptics_library.dll   <- bHaptics runtime DLL (x64), required at runtime
  library.h              <- original SDK header, for reference only
                            (not used by the build: the wrapper uses GetProcAddress)

At runtime the DLL lives in the Northstar plugin dependency folder:

  <Titanfall2>\TF2VR\plugins\lib\bhaptics_library.dll

The post-build step copies it there. Northstar does not try to load DLLs in
plugins\lib\ as plugins, so it causes no "invalid plugin" errors.

The DLL needs the Visual C++ 2015-2022 x64 runtime (MSVCP140.dll,
VCRUNTIME140.dll, VCRUNTIME140_1.dll).

If an SDK update ever breaks loading ("export not found ..." in the log),
check the export names in a VS Developer Command Prompt:

  dumpbin /EXPORTS bhaptics_library.dll
