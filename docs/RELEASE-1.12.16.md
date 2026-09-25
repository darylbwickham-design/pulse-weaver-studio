# Pulse Weaver 1.12.16

This release adds an opt-in alpha update channel and a direct route to Setup & Recovery. Fresh installations stay on the release channel. Existing users keep their scenes, credentials, API tokens and configured ports.

In **Studio → Updates**, enable **Include experimental alpha builds** to receive alpha offers. Setup verifies a complete backup before updating the existing installation. Use **Restore a backup / leave alpha**, or the downloaded release installer, to restore the previous version and settings together. The current state is backed up before a restore.

The included **Pulse Weaver Lumia 1.4.0** updates the existing `pulseweavercontrol` plugin. Saved actions and connection settings remain compatible; it keeps reconnecting while an update or rollback is in progress. Install it over the regular Lumia plugin, keeping your port and token settings.

The separate Motion Preview installation is not an upgrade target. External media files stay where they are and are not included in installation backups. Full recovery backups contain private account data and should remain on the same Windows account.

Downloads: `PulseWeaver-Setup-1.12.16-BETA.exe`, `PulseWeaver-Lumia-1.4.0.lumiaplugin`, and `SHA256SUMS.txt`.
