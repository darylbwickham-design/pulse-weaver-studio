# Pulse Weaver 1.12.15 Beta — Crash and stability fixes

This update fixes an Overlay Designer selection callback that could run while the window was being destroyed. The faulty callback sequence was reproduced in a regression test and the protected sequence passed 100 deletion cycles. It is consistent with the reported Qt selection crash; the friend's exact live-camera sequence has not been reproduced on their hardware.

Scene visibility, transform and automation operations now retain scene-item references safely. Browser subscriber cleanup handles disconnect callbacks without invalidating its iteration. The local control API waits for complete request bodies, rejects malformed JSON and ambiguous request framing, and limits stalled connections and receive buffers.

This build also includes portrait-stage stinger preloading and lifetime fixes, and passes the undecorated stage name to the Lumia integration.

Download **PulseWeaver-Setup-1.12.15-BETA.exe**. Close Pulse Weaver and previous setup windows, then run the installer. Do not uninstall first. Setup creates a verified app-and-settings backup before replacement and provides a restore option. Keep backups private because they can contain account information.

The installer includes the existing Google Desktop application registration for Connect YouTube; no personal account tokens or user configuration are included. Existing registrations and settings are preserved on upgrade.

The matching source ZIP and SHA-256 checksums accompany this release. This remains beta software.
