# Native HUD checks

Build this target with the Windows SDK, Qt 6 Widgets and an existing compatible
OBS build supplied as `OBS_BUILD`. Run `PulseHudTests.exe <runtime-root>` with the
runtime's `bin/64bit` on PATH and the Windows Qt platform plugin available.

The fixture uses inert colour sources, a small desktop surface and independent
native landscape, portrait and HUD canvases. It does not access accounts,
cameras, browser pages, audio devices or the installed application's state.

Checks cover physical monitor mapping, editor ownership, the actual presented
GPU backbuffer's empty and partial alpha, premultiplied colour, native input
hit-testing, programme isolation, one presentation per video tick with multiple
mixes, off-state inactivity and rapid start/stop with queued frame notifications.
Backbuffer CPU readback and counters exist only in this test target; the normal
application build performs its complete HUD render and conversion on the GPU.

These checks do not constitute a live game compatibility or exclusive-fullscreen
test, nor do they replace a native Camera/HUD UI walkthrough.
