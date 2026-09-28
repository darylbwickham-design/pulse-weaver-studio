# Pulse Weaver 1.14.0 unstable 3

Fixes the early startup crash introduced by the unstable settings workspace. The recording folder was read before the active profile existed. Profile-backed recording and bandwidth fields now populate after profile loading and refresh when Settings is opened. Save actions guard against an unavailable profile and resolve the current recording mode at save time.

Includes all unstable 2 features and YouTube quota/streaming-chat fixes. Regular 1.13.3 is unaffected by this unstable-only startup defect.

If the application cannot open, download this installer and run it over the existing installation. Do not uninstall or remove your configuration. The existing installer backup and profile-preservation flow is retained, including credentials and Lumia ports.

Verification: crash stack resolved with matching debug symbols to InitPulseWeaverShell and the unguarded recording-folder read; native application rebuilt. Installed user configuration was not modified. Live streaming and the user's actual profile startup were not exercised.
