# Updater checks

Build the CMake targets with the bundled Qt Core, Network and Widgets libraries. `PulseUpdateTests` checks version ordering, channel opt-ins, downgrade prevention, draft rejection, asset identity/digests and HTTPS download policy. On Windows, `PulseUpdateHandoffTests` exercises the production clean-process handoff in disposable child processes.

The isolated 2.0 installer writes the optional schema-1 identity field `product: "studio2-preview"` with channel `windows-unstable`. Its updater selects only `PulseWeaver-2-Preview-Setup-<tag without v>.exe` assets and its own `Uninstall Pulse Weaver 2 Preview.exe` recovery executable. Tests cover generic/newer stable/alpha rejection, malformed or absent product markers, exact asset URL/digest validation, paired asset families, and unchanged markerless regular selection. These are metadata-only tests; they do not download or install anything.

`PulseUpdateTests.exe --release-catalog <github-releases.json> v1.14.4-alpha.5` checks the production selector against saved GitHub release metadata. An existing alpha and a stable installation with alpha enabled must find the exact installer with its uploaded digest. Stable without opt-in and an already-current alpha must receive no update. This reads metadata only and does not download or run an installer.
