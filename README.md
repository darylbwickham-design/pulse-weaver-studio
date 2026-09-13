# Pulse Weaver Studio — Public Dist 0.03

A Windows streaming studio built on OBS Studio, with show control, landscape and portrait outputs, platform chat, an audio mixer and three visual themes.

This is a **public testing preview**. Download the Windows installer from this repository's **Releases** page. The installer is attached to the release rather than stored in Git. It installs separately as **Pulse Weaver Public Preview**.

## Connecting your platforms

This build includes **no developer app credentials or signed-in accounts**. Testers supply their own app registrations in **Action → Connections**:

- **Twitch:** create a Public application in the Twitch developer console and paste its Client ID. Connect uses device authorization; no Twitch client secret is needed.
- **Kick:** create an application in Kick's developer settings. Register exactly `http://localhost:18757/auth/callback`. Enter its Client ID and Client secret, then connect.
- **YouTube:** enable YouTube Data API v3 in your Google Cloud project, configure the OAuth consent screen, and create a **Desktop app** OAuth client. Enter its Client ID and Client secret. If your consent screen is in Testing, add the Google account you will use as a test user. Testing restrictions and quotas still apply.

App details save locally when you leave a field. Disconnect before changing a registration, then reconnect. Secrets are protected with Windows DPAPI for the current Windows account. Do not share the installed `config` folder: it contains account settings and tokens.

You can explore the studio without connecting a platform. Your existing private Pulse Weaver installation is separate.

## Published minimum system

The current conservative minimum for a multi-output 1080p60 show is **Windows 10/11 64-bit, Intel Core i5-12400F, NVIDIA GeForce RTX 3060, 32 GB RAM, and 2 GB free storage**. This is the developer's tested machine baseline. Simpler single-output shows may run on less capable hardware, but are not part of the published support floor yet.

## Source and builds

See [BUILD.md](BUILD.md). Native application source is in `engine/obs-studio`; optional Lumia plugin source is in `integrations/lumia-pulseweaver`. The P logo and native theme assets are included.

This repository starts with a clean source snapshot. Build outputs, personal configuration, credentials, logs and previous Git history are excluded.

## Licensing

Pulse Weaver is derived from OBS Studio and distributed under GPL version 2 or later; see [LICENSE](LICENSE), [UPSTREAM.md](UPSTREAM.md), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This is an independent project and is not an official OBS Studio release.
