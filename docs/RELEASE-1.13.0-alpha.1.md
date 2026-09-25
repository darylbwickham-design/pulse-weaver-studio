# Pulse Weaver 1.13.0 alpha 1

An opt-in experimental update with the visual Motion editor in Show Control, saved stage looks, source animation, and Lumia controls. Across stages, the destination look is prepared before the configured stinger reveals it. Looks within a stage animate directly.

This installer upgrades the regular **Pulse Weaver** installation in place. It creates and verifies a full recovery backup first, preserving the existing profile, credentials, tokens and ports. It contains no personal scenes or test profile. Release remains the default for fresh installations; alpha requires opting in or explicitly running this alpha installer.

To return, stop all outputs and open **Studio → Updates → Restore a backup / leave alpha**, or run the release installer. Select the backup made before installing alpha. Setup restores the previous version and settings together and backs up the current alpha state first. You can turn off alpha offers after restoring if the saved release preferences had them enabled.

Use **Pulse Weaver Lumia 1.4.0** with the existing `pulseweavercontrol` plugin ID and connection settings. The regular default port remains 18755. The separate Motion Preview plugin and port 18765 are for the isolated test installation only.

Downloads: `PulseWeaver-Setup-1.13.0-alpha.1.exe`, `PulseWeaver-Lumia-1.4.0.lumiaplugin`, and `SHA256SUMS.txt`.
