# Offline motion preservation regression

This suite compiles the production `pulse-motion-engine.cpp` and uses private synthetic scenes, inert video inputs and a temporary motion JSON directory. It does not open installed settings, capture a camera, contact a platform or start a real broadcast.

Coverage includes unsupported/malformed storage, unknown JSON retention, failed import/delete rollback, grouped source UUID lookup after rename, source resizing and flipped/cropped transforms, editor/live transform consistency, and collection-change timer/reference cleanup.

Default reflow coverage renders real D3D11 pixels across bounds modes, alignment,
cropping, flips and rotation. It checks mixed-mode continuity, exact native final
fields, varied entrances, stationary artwork, graphic midpoint changes, and
main-view coverage through full/inset, portrait-panel, inset/column and new-camera
handoffs in both directions. Pixel samples wait for a native video tick because
OBS caches cropped item textures between frames. Explicit legacy style, camera
close-ups, default timing and editor/runtime rehearsal consistency are retained.

Wizard coverage checks independent native canvas ownership for generated camera wrappers and recursive saved-layout units, local coordinate sizes, nested crops and frames, native save/reload, and stable source/item identities across Looks. Browser creation is tested with distinct landscape and portrait dimensions and shared-source caching within each format. Existing artwork must match its assigned format; square and unknown-size sources retain compatible fallback.

The real flexible wizard widgets are exercised without showing a window: artwork starts unassigned for each Look and canvas, Lumia browser choices and creation are filtered, required answers are validated, and independent browser definitions survive navigation. Wizard requests reject native Pulse Weaver artwork, including hidden overlays nested inside saved layouts, before changing the show. Saved native artwork requires a Lumia replacement with matching orientation. Local Lumia Chat Physics links remain supported. Regression checks cover single-Look overlay targeting, deselected targets, rejected Lumia import URLs, and BRB/Ending artwork visibility in both directions. Unmarked legacy API requests retain native title/countdown compatibility.

Configure with CMake and the bundled Qt path, then build `Release`. `OBS_BUILD` is a configurable path to an existing compatible OBS build (default `engine/obs-studio/build_unstable`). Set its `rundir/RelWithDebInfo/bin/64bit` on `PATH` and pass the `rundir/RelWithDebInfo` directory as the executable's only argument.

For a headless run use `QT_QPA_PLATFORM=minimal` and set `QT_QPA_PLATFORM_PLUGIN_PATH` to the bundled Qt `plugins/platforms` directory. This Qt bundle does not include the `offscreen` plugin. The test intentionally has no OBS frontend callback owner; expected null-window callback diagnostics are filtered, while all other OBS errors fail the suite.
Verified 12 October 2026: **1,934 checks passed**, including staged source creation and reuse, shared alert layers on saved and new layouts, duplicate-layer rejection within saved references and across overlapping Looks, full-canvas artwork, wizard stinger drafts and failure rollback, Lumia browser overlay policy, and warm browser overlay lifecycle settings.
