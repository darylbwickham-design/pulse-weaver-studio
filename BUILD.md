# Build Pulse Weaver 1.12.15 on Windows

This source snapshot corresponds to the Windows private/beta 1.12.15 release. It contains no personal configuration, account tokens or bundled Google registration file. Use `packaging/PulseWeaver.PrivateSetup` for this release.

After the native build below, supply an authorised Google Desktop app registration JSON and create the clean runtime ZIP with `packaging/Build-PrivateV126.ps1 -Version 1.12.15 -YouTubeDesktopClientJson <path>`, then publish the self-contained installer:

```powershell
dotnet publish packaging/PulseWeaver.PrivateSetup/PulseWeaver.PrivateSetup.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:PublishTrimmed=false -p:ReleaseVersion=1.12.15 -o artifacts/setup-1.12.15
dotnet run --project tests/InstallerRecovery/InstallerRecovery.csproj -c Release
```

The revised 1.12.7 recovery installer is built from the same maintenance source using `-p:ReleaseVersion=1.12.7 -p:InstallerSuffix=RECOVERY` and the archived original 1.12.7 runtime ZIP. Do not rebuild the old payload from new native sources or overwrite the original installer. See `RELEASE_NOTES.md` for update/rollback behavior. Publish only clean payloads and source archives, never configuration or recovery backups.

Install Visual Studio 2022 17.14 with C++ desktop development, MSVC 14.44, ATL and Windows SDK 10.0.22621.0; CMake 3.28 or later; Git; Node.js; and the .NET 8 SDK for the installer.

OBS dependencies are defined with download URLs and hashes in `engine/obs-studio/CMakePresets.json`. Browser, websocket and DirectShow submodule source is vendored in this source snapshot.

From the repository root, in a developer PowerShell:

```powershell
cmake -S engine/obs-studio -B engine/obs-studio/build_pw_vs1714_sdk22621 -G "Visual Studio 17 2022" -A "x64,version=10.0.22621.0" -DENABLE_BROWSER=ON -DENABLE_WEBSOCKET=ON -DENABLE_AJA=OFF -DENABLE_VLC=OFF -DYOUTUBE_CLIENTID= -DYOUTUBE_SECRET= -DYOUTUBE_CLIENTID_HASH=0 -DYOUTUBE_SECRET_HASH=0
cmake --build engine/obs-studio/build_pw_vs1714_sdk22621 --config RelWithDebInfo --target obs-studio pulse-weaver-core pulse-weaver-sample-clock --parallel 4
node --test tests/LumiaPlugin.test.cjs
```

The executable is `engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo/bin/64bit/PulseWeaverCore.exe`. It uses an isolated portable configuration. The StageExclusions regression requires Qt6 Core and libobs from this build; run its executable with the runtime directory as its sole argument and the runtime bin/64bit directory on PATH.

Do not set compile-time confidential OAuth credentials for a public build. Pulse Weaver includes the public Twitch and Kick application IDs; Kick's client secret remains only in the hosted relay. YouTube registration is supplied at runtime through Action > Connections.

The public repository intentionally excludes release binaries, build caches, configuration, logs, debug symbols and the packaged Google registration file. Run the native and installer regression suites and scan every release asset before publishing. The installer belongs on GitHub Releases because it exceeds Git's project file limit.
