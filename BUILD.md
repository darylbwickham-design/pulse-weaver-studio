# Build Public Dist 0.01 on Windows

Install Visual Studio 2022 17.14 with C++ desktop development, MSVC 14.44, ATL and Windows SDK 10.0.22621.0; CMake 3.28 or later; Git; and .NET 8 SDK for the installer. Node.js is needed only to regenerate visual assets.

OBS dependencies are defined with download URLs and hashes in `engine/obs-studio/CMakePresets.json`. The native CMake configuration obtains the OBS dependencies. Browser, websocket and DirectShow submodule sources are included in this snapshot.

From the repository root, in a developer PowerShell:

```powershell
cmake -S engine/obs-studio -B engine/obs-studio/build_pw_vs1714_sdk22621 -G "Visual Studio 17 2022" -A "x64,version=10.0.22621.0" -DENABLE_BROWSER=ON -DENABLE_WEBSOCKET=ON -DENABLE_AJA=OFF -DENABLE_VLC=OFF -DYOUTUBE_CLIENTID= -DYOUTUBE_SECRET= -DYOUTUBE_CLIENTID_HASH=0 -DYOUTUBE_SECRET_HASH=0
cmake --build engine/obs-studio/build_pw_vs1714_sdk22621 --config RelWithDebInfo --target obs-studio pulse-weaver-core pulse-weaver-sample-clock --parallel 4
```

The executable is `engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo/bin/64bit/PulseWeaverCore.exe`. It uses an isolated portable configuration.

Do not set compile-time OAuth credentials for this public build. Platform registrations are supplied at runtime through Action → Connections.

To package the clean runtime and installer:

```powershell
./packaging/Build-NativePreview.ps1 -Version 0.01-public
dotnet publish packaging/PulseWeaver.Setup/PulseWeaver.Setup.csproj -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true /p:PublishTrimmed=false -o artifacts/setup-public-0.01
```

The installer supports `/test` to validate its embedded payload without installing or launching the studio. Keep installers on GitHub Releases; never commit the runtime, configuration or build caches to Git.
