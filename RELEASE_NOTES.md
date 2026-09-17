# Pulse Weaver 1.12.5 — Destination bitrate controls

Settings → Output now has a dedicated destination bandwidth panel:

- Separate landscape and portrait video bitrates for Kick and YouTube, saved per profile.
- Twitch Enhanced Broadcasting total bandwidth cap and Automatic control. Individual Twitch tracks remain negotiated by Twitch.
- Sustained-upload input, all-output planning estimate and low-headroom warning. Automatic Twitch usage is explicitly excluded when unknown.
- Existing defaults are preserved; bitrate changes apply the next time an output starts.

Includes the updater handoff fix from 1.12.4 and live 16:9 transform fix from 1.12.3.
This build adds bitrate controls; the other performance-review findings are not yet fixed.

## Updating

From 1.12.4, use Studio → Updates → Check for updates to test the updater.
From 1.12.2 or 1.12.3, close Pulse Weaver and run the matching installer manually once because those versions contain the old updater handoff.

- Existing private beta: **PulseWeaver-Setup-1.12.5-BETA.exe**.
- Public Preview: **PulseWeaver-Public-Dist-1.12.5-Setup.exe**.

Both installers preserve configuration and contain no personal credentials. Corresponding source archives, Lumia 1.1.3 and SHA-256 checksums are included. Mac remains on its separate preview release line.

Validation: native Windows build; rendered Settings layout; independent bitrate changes; Apply/reopen and Cancel checks; updater channel/handoff regression; installer payload and configuration-preservation checks; credential and archive-parity audit. No live broadcast was started during verification; live performance still needs testing.
