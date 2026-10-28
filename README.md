# Pulse Weaver Studio — 2.0.0-unstable.9

A Windows streaming studio built on OBS Studio, with show control, landscape and portrait outputs, platform chat, an audio mixer and three visual themes.

**2.0.0-unstable.9** adds an **All chats (no Discord)** read filter. Receive-only Discord channels, introduced in unstable 8, appear in the main chat panel through a secure Pulse connection. Choose channels, including authorised private channels, from the Chat options menu. See [the release notes](docs/RELEASE-2.0.0-unstable.9.md) and [Discord setup](docs/DISCORD-RECEIVE-ONLY.md). The latest regular Windows release remains 1.14.3.

Incoming YouTube chat automatically uses the web scanner in the combined chat box. Sending and moderation use the official API. No reader dropdown or API-receive fallback is exposed, and old reader preferences cannot re-enable API reception.

## Connecting your platforms

Packaged builds include the platform application registrations configured for that package, never a signed-in user's account or personal configuration. Open **Action → Connections** (or **Connections** on Show) to connect accounts; advanced custom registrations remain available:

- **Twitch:** uses Pulse Weaver's registered public desktop application. Connect uses device authorization; no Twitch client secret or developer registration is required.
- **Kick:** packaged builds support Pulse Weaver's registered application and OAuth relay. A custom registration uses `http://localhost:18757/auth/callback` and its own Client ID and secret.
- **YouTube:** channel packaging supplies an approved **Desktop app** OAuth registration. Source builders can supply their own Google Desktop app registration through the packaging script. Testing restrictions and the registration's project quota still apply.

Pulse Weaver asks you to accept its current privacy notice and terms before YouTube access begins, requests the narrower `youtube.force-ssl` scope, protects saved OAuth credentials with Windows DPAPI, and provides an in-app disconnect action that revokes Google access and deletes the live local credentials.

App details save locally when you leave a field. Disconnect before changing a registration, then reconnect. Secrets are protected with Windows DPAPI for the current Windows account. Do not share the installed `config` folder: it contains account settings and tokens.

You can explore the studio without connecting a platform. For an independent test profile, use a separate portable copy with its own configuration folder.

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

Open **Settings → Start here → Build my show**, or **Show Control → Control → Build my show…**. The four-page wizard guides you from stage groups and functional Looks to source assignments and review. A Stage holds related Looks; a Look is one presentation you choose while running your show.

Each stage group has its own source assignments. Reuse existing inputs or create cameras, captures, media files, images and browser sources in the wizard. New inputs can be reused by other groups, and each Look has separate landscape and portrait artwork. Three shared alert layers per format can be assigned to the selected Looks. Add or reuse stingers and select the transition used when entering each Stage.

**Lumia Stream is required for the Lumia overlay templates for Pulse Weaver, including the countdown and widgets**, and its [companion plugin](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v1.14.4-alpha.1/PulseWeaver-Lumia-1.4.4.lumiaplugin) is the recommended way to control animations and show actions. Install Lumia, import and connect the plugin, then import the templates. On page 4, reuse the corresponding Lumia browser sources or add their browser links. The Lumia bulb on Show Control explains the countdown start payload and the command to run when the timer reaches zero.

In **Control**, design and rehearse Looks, then save them. **Show** contains the live Stage, chat, audio and output controls. The Lumia bulb opens that service's overlay and command instructions. See [the current setup guide](docs/SETUP-WIZARD-GUIDE.md) for the full flow and [the alpha release notes](docs/RELEASE-v1.14.4-alpha.1.md) for this update. The older theme-builder guide is historical.
