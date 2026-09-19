# Native Camera editor regression

The portrait tab now reuses the landscape OBS editor. The C++ test exercises the same `PulseEditor::Selection` and canvas-dimension helpers used by the application, with a real D3D11 libobs runtime.

It checks separate canvas dimensions, programme-channel isolation, independent scene-item placement for a shared source, selection retention, concurrent renderer references, same-name scene UUID lookup, canvas-preserving duplication/recreation, and transform undo snapshots.

Upgrade retention is a release requirement. A synthetic pre-native-editor scene-item fixture is loaded and saved/reloaded on both canvases in absolute and relative coordinate modes. Assertions cover scene UUID, canvas, source/item identity, position, scale/flip, rotation, alignment, bounds, crop, visibility, lock, scale filter and private metadata. This is format-level coverage, not a substitute for testing a copy of a real prior-version collection before release. The editor must not recreate scenes, auto-fit sources, reset transforms or change coordinate mode merely because the UI changed.

The private installer's `/test` additionally verifies byte-for-byte preservation of existing configuration and scene JSON through extraction and stale-manifest cleanup, plus a readable pre-upgrade configuration ZIP. Upgrades create unique local backups under `config-backups` before modifying the installation, reject payloads containing configuration, and never delete configuration through the installed-file manifest. Backups can contain account settings and should remain private. Upgrade in place; uninstalling explicitly removes the portable application's local settings.

Build with the Windows compiler and SDK described in `BUILD.md`:

```powershell
cmake -S tests/CameraEditor -B tests/CameraEditor/build -G "Visual Studio 17 2022" -A "x64,version=10.0.22621.0" -DCMAKE_PREFIX_PATH="engine/obs-studio/.deps/obs-deps-qt6-2026-07-15-x64"
cmake --build tests/CameraEditor/build --config Release
$runtime = (Resolve-Path 'engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo').Path
$env:PATH = "$runtime/bin/64bit;$env:PATH"
& tests/CameraEditor/build/Release/PulseCameraEditorTests.exe $runtime
```

An optional second argument writes a small scene-collection JSON for an isolated GUI test profile. Never target an existing user collection. The fixture contains landscape and portrait scenes both called `Same name`, a separate blank portrait programme, and a shared blue colour source.

GUI checks: switch between both tabs; select a portrait scene; inspect the native source context menu; fit to canvas; undo/redo; drag a source; inspect Properties and Filters; create/duplicate/rename a scene; switch back to Landscape; inspect Show Control to verify programmes stay unchanged; close and reopen to check saved scene identity.

Portrait intentionally disables the native **Resize output (source size)** command, which changes the main broadcast resolution. Portrait retains its dedicated 1080 × 1920 canvas. Transition presets and the audio mixer remain shared; Stage assignments govern programme changes.

## Verification: 19 September 2026

- Native RelWithDebInfo application build: passed.
- Standalone D3D11 regression: passed, including transform snapshot identity and restore.
- Isolated portable GUI profile: matching docks in both tabs, native source context menu, fit/undo staying in Portrait, source drag, portrait scene creation and unchanged Show Control programmes all passed.
- Normal test-application shutdown completed cleanly. Testing exposed selection being cleared before the final collection save; shutdown now only stops its showing reference, retaining the scene until `ClearSceneData()` after saving. The final rebuild passed and the preview started successfully, but repeat GUI reopen verification was blocked by the desktop-control error `foreground window did not report a process id`.
- No stream or recording was started; no installed application or existing user scene collection was replaced.

Repair validation adds the production managed-source migration code. It checks an empty pre-load scan, two separately loaded collections, failed journal writes and retry, idempotence, and exact source/scene JSON comparisons allowing only URL and webpage-control changes. Fixture cleanup removes sources before releasing references, as frontend collection teardown does. Any libobs error now fails the test, including the previously ignored double-destroy shutdown error.
