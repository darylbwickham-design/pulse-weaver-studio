# Pulse Weaver 1.14.0 unstable 5

This preview continues the first-run show builder and visual Control editor in unstable 4. It focuses on a remaining everyday task: routing sound without opening the OBS advanced audio dialog.

## New in unstable 5

- Settings → Sound and recording now includes labels for all six audio tracks, a local monitoring device picker, and an audio source routing matrix. For each source, choose its track membership and Off, Monitor only or Monitor + output.
- The routing matrix uses source IDs internally, so changing a source name does not redirect a save to another source. It refreshes from the current scene collection and refuses changes while streaming or recording.
- The canvas-size controls can display existing profiles above 7680 pixels without silently clamping their values. Validation still requires the output size to fit the base canvas.
- The setup guide now explains the separation between Twitch VOD track membership and YouTube/Kick source exclusions.

## Retained behaviour and limits

The existing stage wizard, paired Control layouts, game window picker, Twitch/YouTube/Kick outputs, credentials, ports, saved looks and Lumia actions remain in place. This is an unstable preview, not a completed regular release. Advanced encoder tuning and custom FFmpeg recording remain in OBS advanced options. Provider exclusions still use Manage stages, and full live account and migrated-profile UI walkthroughs have not been performed.

## Verification

The native frontend and motion plugin compiled. Existing routing, updater and YouTube quota checks and installer self-checks are run during packaging. A packaged startup check uses an isolated portable profile; no installed Pulse Weaver instance or live broadcast is changed.
