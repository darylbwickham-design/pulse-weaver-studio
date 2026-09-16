# Build Public Dist 1.12.4 on Windows

Install Visual Studio 2022 17.14 with C++ desktop development, MSVC 14.44, ATL and Windows SDK 10.0.22621.0; CMake 3.28 or later; Git; Node.js; and the .NET 8 SDK for the installer.

OBS dependencies are defined with download URLs and hashes in `engine/obs-studio/CMakePresets.json`. Browser, websocket and DirectShow submodule source is vendored in this source snapshot.

From the repository root, in a developer PowerShell:

```powershell
cmake -S engine/obs-studio -B engine/obs-studio/build_pw_vs1714_sdk22621 -G "Visual Studio 17 2022" -A "x64,version=10.0.22621.0" -DENABLE_BROWSER=ON -DENABLE_WEBSOCKET=ON -DENABLE_AJA=OFF -DENABLE_VLC=OFF -DYOUTUBE_CLIENTID= -DYOUTUBE_SECRET= -DYOUTUBE_CLIENTID_HASH=0 -DYOUTUBE_SECRET_HASH=0
cmake --build engine/obs-studio/build_pw_vs1714_sdk22621 --config RelWithDebInfo --target obs-studio pulse-weaver-core pulse-weaver-sample-clock --parallel 4
node --test tests/LumiaPlugin.test.cjs
```

The executable is `engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo/bin/64bit/PulseWeaverCore.exe`. It uses an isolated portable configuration. The StageExclusions regression requires Qt6 Core and libobs from this build; run its executable with the runtime directory as its sole argument and the runtime bin/64bit directory on PATH.

Do not set compile-time OAuth credentials for a public build. Platform registrations are supplied at runtime through Action > Connections.

The release maintainer packages the clean runtime, Lumia companion and source snapshot from the private release workspace with `artifacts/prepare-public-1.12.4.mjs`, then runs `artifacts/audit-public-1.12.4.mjs`. The public repository intentionally excludes release binaries, build caches, configuration, logs and debug symbols. The installer belongs on GitHub Releases because it exceeds Git's 25 MB project limit.
