# Pulse Weaver Studio — 2.0.3

A Windows streaming studio built on OBS Studio, with show control, landscape and portrait outputs, platform chat, an audio mixer and three visual themes.

**2.0.3 is the current regular Windows release.** A new Show Control button beside HUD and Lumia refreshes browser sources across every canvas, including inactive sources, while bypassing the browser cache. The printable Nerds' Introduction manual remains available in Studio Help and About. See [the patch notes](docs/RELEASE-2.0.3.md).

The 2.0 workspace includes animated format selectors, selection-first show setup, smoother camera Looks, an independent transparent HUD, receive-only Discord chat and a configurable 15-button Lumia keypad. YouTube connections include optional app credentials, a shared-quota notice and a click-through setup guide. See [the 2.0 feature notes](docs/RELEASE-2.0.0.md).

[Download 2.0.3 for Windows x64](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v2.0.3/PulseWeaver-Setup-2.0.3.exe) · [Lumia plugin 1.5.2](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v2.0.3/PulseWeaver-Lumia-1.5.2.lumiaplugin) · [Release assets and checksums](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/tag/v2.0.3)

Incoming YouTube chat automatically uses the web scanner in the combined chat box. Sending and moderation use the official API. No reader dropdown or API-receive fallback is exposed, and old reader preferences cannot re-enable API reception.

## Connecting your platforms

Packaged builds include the platform application registrations configured for that package, never a signed-in user's account or personal configuration. Open **Action → Connections** (or **Connections** on Show) to connect accounts; advanced custom registrations remain available:

- **Twitch:** uses Pulse Weaver's registered public desktop application. Connect uses device authorization; no Twitch client secret or developer registration is required.
- **Kick:** packaged builds support Pulse Weaver's registered application and OAuth relay. A custom registration uses `http://localhost:18757/auth/callback` and its own Client ID and secret.
- **YouTube:** packaged builds supply a **Desktop app** OAuth registration. In **Action → Connections → YouTube**, leave the optional client ID and secret empty to use it, or enter your own Desktop app credentials. The card explains shared daily quota and provides a step-by-step guide. Each project still has limits, and Google's account and Testing restrictions apply.

Pulse Weaver asks you to accept its current privacy notice and terms before YouTube access begins, requests the narrower `youtube.force-ssl` scope, protects saved OAuth credentials with Windows DPAPI, and provides an in-app disconnect action that revokes Google access and deletes the live local credentials.

For YouTube, select **Save app details** to save both fields together. Disconnect before changing a registration, then reconnect. Bundled YouTube credentials are never filled into the editor. Secrets are protected with Windows DPAPI for the current Windows account. Do not share the installed `config` folder: it contains account settings and tokens.

You can explore the studio without connecting a platform. For an independent test profile, use a separate portable copy with its own configuration folder.

## Discord chat with Pulse

**Pulse is the Discord bot behind Pulse Weaver's Discord chat feature.** Read up to five selected channels from one server alongside Twitch, YouTube and Kick in the main chat panel. Both your Discord account and Pulse must have access to each selected channel, including private moderator channels.

Have Pulse added to your server, then open the chat panel's options menu → **Discord channels…**. Approve the connection in your browser and choose the server and channels. Discord is receive-only; the composer continues to send to the streaming platforms. Use **Discord**, individual-channel filters or **All chats (no Discord)** to choose what you see.

[Open the Pulse bot portal](https://pulse-community.darylbwickham.workers.dev/) · [Discord chat setup guide](docs/DISCORD-RECEIVE-ONLY.md)

## Published minimum system

The current conservative minimum for a multi-output 1080p60 show is **Windows 10/11 64-bit, Intel Core i5-12400F, NVIDIA GeForce RTX 3060, 32 GB RAM, and 2 GB free storage**. This is the developer's tested machine baseline. Simpler single-output shows may run on less capable hardware, but are not part of the published support floor yet.

## Custom shows with Codex

Give your own Codex [this custom-show guide](docs/CODEX-CUSTOM-SHOWS.md) to create
Stages and animated Looks from your description, screenshots and existing
sources. It includes a copy-and-paste prompt, detailed composition example,
configuration/identity notes and a backup/rehearsal workflow. For focused code
changes and adding output destinations, see
[local modifications and custom outputs](docs/CUSTOM-OUTPUTS-AND-LOCAL-MODS.md).

## Source and builds

See [BUILD.md](BUILD.md). Native application source is in `engine/obs-studio`; Lumia companion plugin source is in `integrations/lumia-pulseweaver`. The P logo and native theme assets are included.

The 1.14 branch includes named looks, camera close-ups and multi-source layouts, Stage-aware execution, conservative Move/Lumia import, and a shared controller surface for Lumia, LumiCon and Stream Deck. The earlier `codex/native-motion-engine` branch remains the isolated Motion Preview lineage. See [the product and implementation overview](docs/design/native-motion-overview.md).

This repository starts with a clean source snapshot. Build outputs, personal configuration, credentials, logs and previous Git history are excluded.

## Licensing

Pulse Weaver is derived from OBS Studio and distributed under GPL version 2 or later; see [LICENSE](LICENSE), [UPSTREAM.md](UPSTREAM.md), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). See the [Privacy Notice](PRIVACY.md) and [Terms of Service](TERMS.md) for the app's data practices and service terms. This is an independent project and is not an official OBS Studio release.

## Build your show

Open **Settings → Start here → Build my show**, or **Show Control → Control → Build my show…**. The click-through wizard guides you from stage groups and functional Looks to source assignments and review. A Stage holds related Looks; a Look is one presentation you choose while running your show.

Each stage group has its own source assignments. Reuse existing inputs or create cameras, captures, media files, images and browser sources in the wizard. New inputs can be reused by other groups, and each Look has separate landscape and portrait artwork. Three shared alert layers per format can be assigned to the selected Looks. Add or reuse stingers and select the transition used when entering each Stage.

**Lumia Stream is required for the Lumia overlay templates for Pulse Weaver, including the countdown and widgets**, and its [companion plugin 1.5.2](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v2.0.3/PulseWeaver-Lumia-1.5.2.lumiaplugin) is the recommended way to control animations and show actions. Install Lumia, import and connect the plugin, then import the templates. In the setup wizard's alert editor, reuse the corresponding Lumia browser sources or add their browser links. The Lumia bulb on Show Control explains the countdown start payload and the command to run when the timer reaches zero.

In **Control**, design and rehearse Looks, then save them. **Show** contains the live Stage, chat, audio and output controls. The Lumia bulb opens that service's overlay and command instructions. See [the current setup guide](docs/SETUP-WIZARD-GUIDE.md) for the full flow and [the 2.0 release notes](docs/RELEASE-2.0.0.md) for this update. The older theme-builder guide is historical.
