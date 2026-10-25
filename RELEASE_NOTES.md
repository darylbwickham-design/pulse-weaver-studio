# Pulse Weaver 2.0.0-unstable.4 — Click-through show setup

Build my show now follows Start, Sections, Looks, Sources and Review. Focused
Preview and Configure screens, separate landscape/portrait transition tabs,
deferred optional layers and direct Review edits keep the main task clear.
The wizard matches the main UI's raised controls and retains existing native
movement, stingers, multistream and Lumia/audio behavior.

Show output and broadcast controls sit above previews and audio. Native build,
2,143 preservation checks, three-size layout checks and an isolated Windows
creation walkthrough passed. Both ordinary unstable and isolated 2 Preview
installer families advance to revision 4. See `docs/RELEASE-2.0.0-unstable.4.md`.

---
# Pulse Weaver 1.14.4-alpha.2 — Installer update fix

This alpha fixes updates that stop with “Access to the path …\config is denied”.
When the staged settings are unchanged, setup leaves the existing config folder
in place instead of renaming it. Full verified backups and rollback remain active;
changed settings and full restores still use the normal protected transaction.

Setup now displays the full alpha revision. Readonly staged copies are cleaned
without changing the original settings, and access errors explain how to retry.
The broadcast runtime, Lumia 1.4.4 plugin and wizard assets are unchanged from alpha 1.

Validation: 104 recovery checks, a real Windows directory-lock reproduction,
all rollback boundaries, payload validation, and a private copied-profile
upgrade/restore with byte-for-byte configuration preservation. Updater checks
confirm alpha 1 discovers alpha 2 and regular users receive it only when opted in.

Download **PulseWeaver-Setup-1.14.4-alpha.2.exe**. Close Pulse Weaver and older setup
windows, then run it. The matching source archive and SHA-256 checksums are included.

---
# Pulse Weaver 1.12.15 Beta — Crash and stability fixes

This update fixes an Overlay Designer selection callback that could run while the window was being destroyed. The faulty callback sequence was reproduced in a regression test and the protected sequence passed 100 deletion cycles. It is consistent with the reported Qt selection crash; the friend's exact live-camera sequence has not been reproduced on their hardware.

Scene visibility, transform and automation operations now retain scene-item references safely. Browser subscriber cleanup handles disconnect callbacks without invalidating its iteration. The local control API waits for complete request bodies, rejects malformed JSON and ambiguous request framing, and limits stalled connections and receive buffers.

This build also includes portrait-stage stinger preloading and lifetime fixes, and passes the undecorated stage name to the Lumia integration.

Download **PulseWeaver-Setup-1.12.15-BETA.exe**. Close Pulse Weaver and previous setup windows, then run the installer. Do not uninstall first. Setup creates a verified app-and-settings backup before replacement and provides a restore option. Keep backups private because they can contain account information.

The installer includes the existing Google Desktop application registration for Connect YouTube; no personal account tokens or user configuration are included. Existing registrations and settings are preserved on upgrade.

The matching source ZIP and SHA-256 checksums accompany this release. This remains beta software.
