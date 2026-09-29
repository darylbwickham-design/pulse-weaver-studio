# Pulse Weaver 1.14.0 experimental alpha 4

This experimental alpha contains the same audited showcase code as **1.14.0 unstable 11**, with the alpha updater identity. It updates the regular Pulse Weaver installation with a verified backup and preserves existing profiles, scenes, account settings and Lumia connection settings.

## Changes

- YouTube chat keeps a persistent transport, resumes from saved cursors and follows active outputs. **Save quota** waits longer after empty responses; **Responsive** reconnects sooner. Save quota can add up to 30 seconds during quiet chat. Reusable ingest streams and creation-response chat IDs reduce setup requests. No YouTube follow/like/subscriber poller is added.
- Fixed overlapping Twitch/Kick requests, stale login and relay callbacks, and retired output callbacks changing a replacement output's status.
- Protected completed YouTube broadcasts/replays during later cleanup; unfamiliar lifecycle states are retained rather than deleted.
- Fixed source-resize differences between look editing and playback, grouped-input rename lookup, exclusion identity across renames/reloads, collection-change cleanup and removed sources remaining in destination copies.
- Fixed stage transition completion, rapid destination changes, inconsistent YouTube format selectors and settings guards missing independent outputs.
- Protected malformed motion/Stage storage, rolled back failed edits, reported save failures, fixed a Lights callback lifetime issue, and strengthened installer/archive/recovery validation.
- Includes **Lumia plugin 1.4.1**, preserving its existing identity and bindings while discarding queued work after unload or connection changes.

## Verification and limits

Both native targets built successfully. Offline suites cover chat lifecycles, platform callbacks, saved looks, stage storage, audio/source lifetime, transitions, overlays, rendering and installer recovery. The locally installed build passed startup and Show/Control/Settings smoke checks with profiles and scenes preserved.

Live quota savings still need measurement; server behavior and busy chat affect requests. Dual YouTube uses two independent broadcasts/chats. Some scene/group container rename references and adding sources to existing private output copies remain limitations. See the [full source audit](PULSE-WEAVER-SOURCE-AUDIT-2026-09-29.md).

Downloads: `PulseWeaver-Setup-1.14.0-alpha.4.exe`, `PulseWeaver-Lumia-1.4.1.lumiaplugin`, and `SHA256SUMS.txt`.

Choose the experimental alpha channel in **Studio → Updates**, or install the matching download. Stop all outputs before updating. To return to a previous version, use the installer's verified backup restore rather than installing older binaries over newer settings.
