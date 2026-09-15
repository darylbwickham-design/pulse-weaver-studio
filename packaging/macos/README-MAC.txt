Pulse Weaver Mac Preview 0.1.0 alpha 1 — Apple Silicon

Requires an Apple Silicon Mac (M1/M2/M3/M4 or newer), macOS 13 or later.
This is an experimental Mac branch based on Windows 1.12.1. It has its own
mac-v release tags and is not an upgrade to the Windows release line.

Open the DMG and drag Pulse Weaver Mac Preview.app into Applications.
The app has its own name and bundle ID, studio.pulseweaver.macpreview.
Settings, scenes, profiles and app credential references live under:
~/Library/Application Support/Pulse Weaver Mac Preview/
App client secrets are stored in your login Keychain. The package contains
no developer registrations or accounts. Configure your own registrations
in Action > Connections or use a custom RTMP destination for a basic test.

This alpha is ad hoc signed and is not Apple notarized. If macOS blocks it,
use System Settings > Privacy & Security > Open Anyway after trying to open
it. Do not disable Gatekeeper globally. Allow camera, microphone and screen
recording permissions when requested; these are separate from existing OBS.

First test: create a fresh scene, add your camera and microphone, make a short
local recording, then check playback. Try horizontal and portrait Stage
routes before a private/unlisted broadcast. Report your macOS version,
Mac model and any failing feature. Windows capture sources do not exist on
macOS; recreate them with Mac camera/screen/audio sources. Existing Windows
scene collections may have unavailable capture sources and file paths.

The existing OBS/MELD installation is not replaced. OBS settings are not
imported automatically; importing a collection is a separate explicit action.
Virtual camera/system extensions and OBS automatic updates are disabled.
Native Twitch EventSub currently remains Windows-only in this alpha.
Chat, encoder compatibility and real device capture need your hardware test.
Build verification is not a guarantee of a successful live platform stream.

To remove the app, move only Pulse Weaver Mac Preview.app to Trash.
Keep its settings folder if you want to reinstall. Existing OBS data is separate.
