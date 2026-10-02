# Shared stinger cache regression checks

Native checks compile the production media playback implementation and link
against the existing Windows OBS build and FFmpeg dependencies. They make no
network requests and do not modify the installed application or original clip.

From the repository root, using the Visual Studio CMake executable:

```powershell
cmake -S tests/SharedStingerCache -B tests/SharedStingerCache/build -A x64
cmake --build tests/SharedStingerCache/build --config RelWithDebInfo
$base = (Get-Location).Path
$env:PATH = (Join-Path $base 'engine/obs-studio/.deps/obs-deps-2026-07-15-x64/bin') + ';' +
    (Join-Path $base 'engine/obs-studio/build_unstable/libobs/RelWithDebInfo') + ';' +
    (Join-Path $base 'engine/obs-studio/build_unstable/deps/w32-pthreads/RelWithDebInfo') + ';' + $env:PATH
& ./tests/SharedStingerCache/build/RelWithDebInfo/PulseSharedStingerCacheTests.exe '<local video file>'
```

Use a short transparent WebM with audio to cover both video and audio sharing.
The test creates and removes a temporary copy for file replacement checks.
`OBS_BUILD` and `OBS_DEPS` CMake cache variables can override the defaults.
The OBS build must already provide obs.lib, w32-pthreads.lib and config headers.
