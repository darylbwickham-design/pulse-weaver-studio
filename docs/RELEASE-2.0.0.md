# Pulse Weaver 2.0

Pulse Weaver 2.0.0 is the regular Windows x64 release, bringing the 2.0 workspace and show-building flow into the main update channel.

## Downloads

- [Windows x64 installer](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v2.0.0/PulseWeaver-Setup-2.0.0.exe)
- [PulseWeaver Stream Studio Lumia plugin 1.5.0](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v2.0.0/PulseWeaver-Lumia-1.5.0.lumiaplugin)
- [Release assets, source and SHA-256 checksums](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/tag/v2.0.0)

## What changed

- Refined Show/Control workspace with consistent, animated platform-colour format selectors. Preview and chat keep their established working space.
- Click-through show setup based on the stages and layouts a user needs, with reusable inputs, separate landscape/portrait framing, stingers and up to three shared alert layers per format.
- Smoother camera Look movement and corrected portrait scene ownership/editing.
- One transparent, click-through HUD, independent from stages and Looks. Choose a display to set the canvas size and toggle it from Show Control.
- Discord chat through **Pulse, the Discord bot by PulseWeaver**: read up to five selected channels from one server alongside streaming chats, including All chats (no Discord). Both you and Pulse must have access to each selected channel.
- A configurable 15-button Lumia keypad in the chat panel. Give buttons names, choose an included icon or supply an image. The plugin exposes Keypad 01–15, all defaulting to Off.
- The renamed PulseWeaver Stream Studio plugin retains the existing Motion Preview ID and alert/action bindings. Its connection help is concise; installation instructions remain in the external README.
- Optional YouTube Desktop app client ID and secret, a shared daily quota notice and a six-step setup guide. Blank fields use the bundled registration, whose values are never filled into the editor. Custom pairs are protected by Windows and saved together; changing apps requires disconnecting first.
- YouTube OAuth objects retain their owning application registration, preventing refreshes from mixing different app credentials. Incoming chat keeps the existing web reader; sending, moderation and broadcast management use the authenticated API.
- Lights remains visible with its normal empty panels marked Coming soon. Unfinished Alerts and Overlay designer entry points remain hidden.

## Discord chat with Pulse

Have Pulse added to your Discord server, then open the chat panel's options menu → **Discord channels…** in Pulse Weaver. Approve the connection in your browser and select the server and channels. Public and private moderator channels are available where both your account and Pulse have permission to read them.

Discord is receive-only; messages typed in Pulse Weaver continue to go to Twitch, YouTube and Kick. [Open the Pulse bot portal](https://pulse-community.darylbwickham.workers.dev/) or follow the [Discord chat setup guide](DISCORD-RECEIVE-ONLY.md).

## Updating

Close Pulse Weaver, then run the installer. Keep the existing installation rather than uninstalling first. Setup creates a verified application-and-settings backup and preserves the show, sources, accounts and configured local API port. Keep recovery backups private because they contain settings and account data.

Regular, alpha and regular unstable installations can detect 2.0.0 through Studio → Updates. A compatibility download is provided for the historical public installer channel. The separately installed **Pulse Weaver 2 Preview** remains an isolated product; its profile is not silently migrated into the regular installation. Lite and the Apple Silicon preview remain separate editions.

## Limits and verification

Your own Google Cloud project still has daily quota and OAuth restrictions. An External app in Testing generally requires reconnection after seven days. Exhausting control-request quota does not automatically terminate an existing broadcast. See the in-app guide and [implementation/source references](YOUTUBE-APPLICATION-SETUP.md).

Some advanced recording controls remain labelled experimental. Browser/media sources and multiple encoders can require substantial resources; this release does not promise a fixed RAM footprint or eliminate game-dependent contention.

The release uses the YouTube-enabled native frontend and core. Credential-storage and UI tests passed, all six guide pages were inspected, and the expanded card was checked in a shorter window. The Lumia suite, installer diagnostics and production update selector are checked during packaging. No real Google login, chat send, moderation action or end-to-end live broadcast is claimed by the automated credential tests.
