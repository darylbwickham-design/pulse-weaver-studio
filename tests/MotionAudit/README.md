# Offline motion preservation regression

This suite compiles the production `pulse-motion-engine.cpp` and uses private synthetic scenes, inert video inputs and a temporary motion JSON directory. It does not open installed settings, capture a camera, contact a platform or start a real broadcast.

Coverage includes unsupported/malformed storage, unknown JSON retention, failed import/delete rollback, grouped source UUID lookup after rename, source resizing and flipped/cropped transforms, editor/live transform consistency, and collection-change timer/reference cleanup.

Wizard coverage checks independent native canvas ownership for generated camera wrappers and recursive saved-layout units, local coordinate sizes, nested crops and frames, native save/reload, and stable source/item identities across Looks. Browser creation is tested with distinct landscape and portrait dimensions and shared-source caching within each format. Existing artwork must match its assigned format; square and unknown-size sources retain compatible fallback.

The real flexible wizard widgets are exercised without showing a window: separate artwork for each Look and canvas, filtered source choices, required-answer validation, independent browser definitions, answer retention after navigation, and a built-in countdown duration shared between both formats. Regression checks cover single-Look overlay targeting, deselected targets, rejected Lumia import URLs, and native BRB/Ending artwork visibility in both directions. Legacy shared artwork remains compatible.

Configure with CMake and the bundled Qt path, then build `Release`. `OBS_BUILD` is a configurable path to an existing compatible OBS build (default `engine/obs-studio/build_unstable`). Set its `rundir/RelWithDebInfo/bin/64bit` on `PATH` and pass the `rundir/RelWithDebInfo` directory as the executable's only argument.

For a headless run use `QT_QPA_PLATFORM=minimal` and set `QT_QPA_PLATFORM_PLUGIN_PATH` to the bundled Qt `plugins/platforms` directory. This Qt bundle does not include the `offscreen` plugin. The test intentionally has no OBS frontend callback owner; expected null-window callback diagnostics are filtered, while all other OBS errors fail the suite.
Verified 12 October 2026: **1,736 checks passed**, including warm browser overlay lifecycle settings.
