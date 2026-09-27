# Pulse Weaver 1.13.1 alpha 6

This experimental update repairs the new show builder and prepares the updater for a separately opt-in unstable showcase. The installer targets the existing Pulse Weaver installation, makes a verified full backup, and retains its profile, ports, credentials and Lumia settings. It does not install itself when downloaded.

## Fixed

- A Hangout with only one screen or camera now fills the canvas instead of treating that one source as a corner inset.
- New portrait chat browser sources start at portrait dimensions.
- Swap focus excludes generated show graphics and never converts unbounded sources into zero-sized bounds. Undo/Redo treats a two-canvas swap as one operation.
- Saved motions identify sources by UUID and scene-item ID, so source renames do not break execution. Newly saved looks also record source dimensions and adapt crop/scale when a source changes resolution.
- The builder omits redundant Gameplay looks, unused title sources for disabled stages, and retains wizard choices after a creation error.
- A Game Capture chosen in the builder becomes Show's selected Game Capture source.
- Kick's settings no longer show a portrait bitrate field. Kick output remains landscape only in Show.
- The updater recognises the plain installer filename of the promoted 1.13.0 release and supports a separate opt-in unstable channel.

The existing Lumia plugin ID and action schema are unchanged. The bundled plugin is included for convenient installation, but an existing 1.4.0 plugin does not need replacing for this update.

## Update and recovery

In Pulse Weaver choose **Studio → Updates → Include experimental alpha builds**, then **Check for updates**. Setup verifies a full backup before replacing the app. To roll back, use **Studio → Updates → Restore a backup / leave preview channel**. Do not uninstall first.

The installer does not include anyone's scenes or credentials. Existing saved looks with bounded layouts continue to follow their saved canvas slots after a source resolution change. Dimension adaptation metadata is added when a look is newly saved or resaved in this build.
