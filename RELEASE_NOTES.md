# Pulse Weaver 1.12.4 — Update installer survives shutdown

Fixes accepting an update closing both Pulse Weaver and the installer. The verified
Windows installer now explicitly leaves the studio's process-cleanup job before
the studio exits. Ordinary browser and capture helpers still close with the studio.
Includes the live 16:9 source-transform fix from 1.12.3.

**Updating from 1.12.2 or 1.12.3:** those versions still contain the old handoff code.
Close Pulse Weaver and run the downloaded installer manually once. Subsequent
updates launched from 1.12.4 use the corrected handoff.

- Existing private beta: PulseWeaver-Setup-1.12.4-BETA.exe.
- Public Preview: PulseWeaver-Public-Dist-1.12.4-Setup.exe.

Both installers preserve configuration and contain no personal credentials.
Corresponding source archives, Lumia 1.1.3 and SHA-256 checksums are included.
Mac remains on its separate preview release line.

Validation: native Windows build; actual process-exit regression confirming the
installer survives while ordinary helpers terminate; updater channel tests;
installer payload/configuration checks; credential and archive-parity audit.
