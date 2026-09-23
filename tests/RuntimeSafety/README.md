# Runtime safety regression

This uses the production safety helpers with Qt 6.11.1 and libobs. The fixtures
do not open cameras, connect streaming accounts, or load user settings.

Coverage:

- Reproduces a selection callback after the old editor teardown cleared its
  scene pointer. The fixture records the invalid access instead of dereferencing
  null. Verifies 100 protected direct/deferred deletions and live selection.
- Splits a JSON request at every byte boundary and rejects oversized requests,
  duplicate/negative/overflowing lengths, transfer encoding and pipelining.
- Exercises 100 grouped source lookups and removals, including ID lookup,
  ownership after scene deletion, and final source destruction.

Configure with the Visual Studio compiler/SDK described in `BUILD.md`:

```powershell
cmake -S tests/RuntimeSafety -B tests/RuntimeSafety/build -G "Visual Studio 17 2022" -A "x64,version=10.0.22621.0" -DCMAKE_PREFIX_PATH="engine/obs-studio/.deps/obs-deps-qt6-2026-07-15-x64"
cmake --build tests/RuntimeSafety/build --config Release
$testRuntime = (Resolve-Path 'engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo').Path
$env:PATH = "$testRuntime/bin/64bit;" + $env:PATH
$env:QT_QPA_PLATFORM = 'minimal'
$env:QT_PLUGIN_PATH = (Resolve-Path 'engine/obs-studio/.deps/obs-deps-qt6-2026-07-15-x64/plugins').Path
& tests/RuntimeSafety/build/Release/PulseRuntimeSafetyTests.exe $testRuntime
```

The test uses D3D11 for libobs resource creation and cleanup. It also runs with
CTest when the same runtime PATH and Qt environment are set.
