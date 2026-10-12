# Pulse Weaver Studio — 1.14.2

A Windows streaming studio built on OBS Studio, with show control, landscape and portrait outputs, platform chat, an audio mixer and three visual themes.

**1.14.2** brings the four-page layout wizard, separate landscape/portrait artwork for each Look, shared Lumia templates and named web chat panels to the regular release. Chat panels retain their state when switching views, and setup guidance explains overlay imports, countdown actions and optional stingers. It also includes the stinger memory and preview improvements from the 1.14.2 previews. Download the Windows installer from this repository's **Releases** page. Regular, alpha and unstable installations can update to 1.14.2 with backup and recovery support; the older **Pulse Weaver Public Preview** is a separate legacy installation. See [UPDATES.md](UPDATES.md) for channel behavior and [the release notes](docs/RELEASE-v1.14.2.md).

Incoming YouTube chat automatically uses the web scanner in the combined chat box. Sending and moderation use the official API. No reader dropdown or API-receive fallback is exposed, and old reader preferences cannot re-enable API reception.

## Connecting your platforms

Packaged builds include the platform application registrations configured for that package, never a signed-in user's account or personal configuration. Start in **Connections** to connect accounts; advanced custom registrations remain available:

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

See [BUILD.md](BUILD.md). Native application source is in `engine/obs-studio`; optional Lumia plugin source is in `integrations/lumia-pulseweaver`. The P logo and native theme assets are included.

The 1.14 branch includes named looks, camera close-ups and multi-source layouts, Stage-aware execution, conservative Move/Lumia import, and a shared controller surface for Lumia, LumiCon and Stream Deck. The earlier `codex/native-motion-engine` branch remains the isolated Motion Preview lineage. See [the product and implementation overview](docs/design/native-motion-overview.md).

This repository starts with a clean source snapshot. Build outputs, personal configuration, credentials, logs and previous Git history are excluded.

## Licensing

Pulse Weaver is derived from OBS Studio and distributed under GPL version 2 or later; see [LICENSE](LICENSE), [UPSTREAM.md](UPSTREAM.md), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). See the [Privacy Notice](PRIVACY.md) and [Terms of Service](TERMS.md) for the app's data practices and service terms. This is an independent project and is not an official OBS Studio release.

## Wizard implementation requirements

The setup wizard uses one four-page flow with broader layout options available by default. Each Look has its own landscape/portrait artwork, and extra overlays can target one Look or several. See [the setup wizard guide](docs/SETUP-WIZARD-GUIDE.md) and [1.14.2 release notes](docs/RELEASE-v1.14.2.md). The older theme builder guide is historical.
