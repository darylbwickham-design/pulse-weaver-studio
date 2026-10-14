# Updater checks

Build the CMake targets with the bundled Qt Core, Network and Widgets libraries. `PulseUpdateTests` checks version ordering, channel opt-ins, downgrade prevention, draft rejection, asset identity/digests and HTTPS download policy. On Windows, `PulseUpdateHandoffTests` exercises the production clean-process handoff in disposable child processes.

`PulseUpdateTests.exe --release-catalog <github-releases.json> v1.14.4-alpha.5` checks the production selector against saved GitHub release metadata. An existing alpha and a stable installation with alpha enabled must find the exact installer with its uploaded digest. Stable without opt-in and an already-current alpha must receive no update. This reads metadata only and does not download or run an installer.
