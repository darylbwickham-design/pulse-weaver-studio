# Basic command and Streamer.bot integration checks

The Qt test compiles the production command helper and Commands page with inert controllers. It checks validation, native vetoes, repeated recording-stop safety, observed feedback, masked tokens, category navigation and all four category cards plus the guide fitting a 1148×500 content area. It starts no broadcasts and writes no recording or replay files.

From the repository root, with CMake and a compatible Qt 6 installation:

```powershell
cmake -S tests/StreamerBotIntegration -B build/basic-controls -DCMAKE_PREFIX_PATH="<Qt root>"
cmake --build build/basic-controls --config Release --parallel 2 -- /nr:false
$env:PATH = "<Qt root>/bin;" + $env:PATH
$env:QT_PLUGIN_PATH = "<Qt root>/plugins"
ctest --test-dir build/basic-controls -C Release --output-on-failure
```

CTest uses Qt's minimal platform. For a native-font screenshot, run `PulseBasicControlsTests.exe <output.png>` with `QT_QPA_PLATFORM=windows`; the test window stays hidden. The actual application layout also requires native fixture QA with its theme and surrounding panels.

The default check includes the inherited theme's ordinary-button height cap and checks every card against its ancestor bounds. To reproduce the complete native theme, set `PULSEWEAVER_CONTROL_THEME_QSS` to the native audit's `active-theme.qss` before running the test; it prints the actual card and navigator rectangles and checks full containment with that stylesheet.

The adapter check compiles the unchanged C# example with a small Streamer.bot API stand-in and an inert loopback HTTP server:

```powershell
dotnet run --project tests/StreamerBotIntegration/adapter/AdapterTests.csproj --configuration Release
```

It targets .NET 8 and references the Newtonsoft.Json assembly bundled with the installed .NET SDK, requiring no downloaded NuGet package. It verifies bearer/client headers, request correlation, response feedback, refused redirects, invalid inputs and safe logging. It never contacts Pulse Weaver or a streaming platform. This verifies the example's C# compilation and HTTP behaviour; the actual Streamer.bot application's import/compiler UI remains untested.

Native HTTP validation used a separate portable fixture with fresh credentials, all destinations Off, no capture devices and an empty-file media source. It checked missing/invalid client authentication, capabilities, invalid commands, real Stage select/next/previous, source visibility, mute/unmute and volume feedback, repeated recording-stop and inactive replay rejection. No streaming, recording or replay output was started. That smoke check is deliberately separate from CTest and must not target an installed show.
