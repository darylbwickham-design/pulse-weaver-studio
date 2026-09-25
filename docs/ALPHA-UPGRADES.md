# Release and alpha updates

The regular release remains on the release channel by default. Open **Studio → Updates → Include experimental alpha builds** to opt in, then check for updates. Downloading an alpha does not install it automatically.

The alpha installer upgrades the regular **Pulse Weaver** installation for the current Windows account. It creates and verifies a full backup before replacement. Scenes, profiles, account credentials, browser state, API tokens and Lumia configuration remain in the same location. Alpha packages contain no personal scenes or settings. The isolated **Motion Preview** installation and its plugin remain separate.

The regular API defaults to **18755** on release and alpha. A saved `api/port` value is respected. The isolated preview defaults to **18765**. An in-place alpha upgrade does not move users to the preview port.

## Return to your previous version

Stop your outputs and choose **Studio → Updates → Restore a backup / leave alpha**. Alternatively, run either the release or alpha installer and choose **Restore backup**. Backups sit beside the install folder in **Pulse Weaver Backups**; their filenames include the saved version and date.

Select the backup from before the alpha upgrade. Setup verifies it, backs up the current state, then restores the previous app files and settings together. Later alpha-only files are removed as part of restoration. Changes made after the selected backup remain in the newly created recovery archive and are no longer active. Installing older binaries over newer settings is blocked.

Backups include private account data. Keep them on the same Windows account: encrypted credentials may not be usable on a different account or computer. External media files referenced by scenes are not copied by the installer; files stored inside the installation are included. Upgrading does not alter external source files.

## Lumia Stream

Install **Pulse Weaver 1.4.0** into Lumia as an update to the existing regular plugin. Its manifest ID is still `pulseweavercontrol`; existing action IDs, field keys, variables and port settings remain compatible. It adds the Motion controls and reconnects while Pulse Weaver is being upgraded or restored. Keep your current port and token settings. The separate `pulseweavermotionpreview` plugin is for the isolated preview only.

## Build

Use `packaging/Build-ChannelRelease.ps1` with `-Channel release -Version 1.12.16` or `-Channel alpha -Version 1.13.0 -AlphaRevision 1`, a complete `-RuntimeRoot`, and `-YouTubeDesktopClientJson`. Configure distributable builds with `PULSEWEAVER_MOTION_PREVIEW=OFF`. Release is built from the release maintenance branch; alpha includes the visual motion editor. Both installers use the same verified recovery engine.
