# Shared Lumia guide UI checks

Builds the production `PulseInstructions.hpp` with Qt Widgets. The harness opens its actual modal helper under a Show Control-like widget and a wizard parent, checks identical contents, all five topics, scrolling, wrapping, the Close control and a real clicked external-link anchor. The link is intercepted; no browser/network page is opened. It captures all topics at 900×700, 760×560 and 640×480, including the end of the challenge migration note.

Configure with CMake and the bundled `Qt6_DIR`, then build `RelWithDebInfo`. Set the Qt `bin` on PATH and its `plugins` as `QT_PLUGIN_PATH`. Run `PulseLumiaGuideChecks.exe <screenshot-directory>` with the normal Windows platform for actual font rendering. Repeat with `QT_SCALE_FACTOR=1.5` to check high-DPI text. QtTest and the unavailable offscreen platform plugin are not required.

This uses isolated test windows. It does not open installed Pulse Weaver/Lumia settings, connect to either app, import examples or trigger show actions. Source call sites are additionally checked to confirm the real Show Control bulb and wizard instruction button both use this helper. The native `MotionAudit` suite compiles the changed production wizard source against a compatible existing OBS runtime and checks its source/layout preservation behavior separately.
