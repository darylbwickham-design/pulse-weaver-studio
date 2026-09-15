# Mac Preview 0.1.0 alpha 1

Initial Apple Silicon port branch based on repaired Windows 1.12.1. Separate
app name, bundle ID, settings directory and release tags; app client secrets
stored in macOS Keychain. OBS updater and virtual camera extensions disabled.

Packages publish only after successful native build, isolated path and Keychain
regressions, architecture and bundle checks, ad hoc code-signature verification
and executable version smoke test. Real camera/audio capture and live broadcasts
are not verified on the tester's hardware. Native Twitch EventSub remains Windows-only.
