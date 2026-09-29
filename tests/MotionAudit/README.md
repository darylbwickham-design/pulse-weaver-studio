# Offline motion preservation regression

This suite compiles the production `pulse-motion-engine.cpp` and uses private synthetic scenes, inert video inputs and a temporary motion JSON directory. It does not open installed settings, capture a camera, contact a platform or start a real broadcast.

Coverage includes unsupported/malformed storage, unknown JSON retention, failed import/delete rollback, grouped source UUID lookup after rename, source resizing and flipped/cropped transforms, editor/live transform consistency, and collection-change timer/reference cleanup.

Configure with CMake and the bundled Qt path, then build `Release`. `OBS_BUILD` is a configurable path to an existing compatible OBS build (default `engine/obs-studio/build_unstable`). Set its `rundir/RelWithDebInfo/bin/64bit` on `PATH` and pass the `rundir/RelWithDebInfo` directory as the executable's only argument.

For a headless run use `QT_QPA_PLATFORM=minimal` and set `QT_QPA_PLATFORM_PLUGIN_PATH` to the bundled Qt `plugins/platforms` directory. This Qt bundle does not include the `offscreen` plugin. The test intentionally has no OBS frontend callback owner; expected null-window callback diagnostics are filtered, while all other OBS errors fail the suite.

Verified 29 September 2026: **96 checks passed**.
