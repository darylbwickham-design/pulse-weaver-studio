# Pulse Weaver 1.14.0 unstable 2

The 1.14 interface showcase now includes all five fixes added to the regular/alpha source since unstable 1, through regular 1.13.3.

## Included fixes

- Twitch Off / 16:9 / Dual routing and asynchronous startup status, including cancellation and rejected preparation cleanup.
- YouTube preparation follows the enabled destinations. Removed placeholder broadcast schedules and unwanted YouTube setup during Twitch/Kick-only startup.
- Persistent YouTube daily-quota protection, rate-limit pauses, full server polling intervals, and bounded retry delays.
- Resumable streaming YouTube chat using the official gRPC endpoint, one reader per unique chat, separate subscriber checks, cancellation, and temporary polling fallback when streaming is unavailable.
- YouTube desktop registration packaging now matches the already-public regular installer. Only the installed-app registration is bundled; user access tokens, refresh tokens, stream keys, and profiles are excluded. Existing registrations remain preserved by updates.

The 1.14 studio setup workspace, visual placement controls, source assignment improvements, music exclusions, and motion/source identity behavior remain in this build.

## Update

Enable **Studio → Updates → Include unstable showcase builds**, then check for updates. This is a prerelease on the separate unstable channel; regular users remain on 1.13.3 unless they opt in. The installer uses the existing backup/rollback flow and preserves settings, credentials, ports, and Lumia integration. The bundled Lumia 1.4.0 plugin is unchanged.

## Verification

The unstable application and core plugin were rebuilt. Regression suites cover streaming chat, quota persistence, broadcast isolation, Twitch startup, updater channels/handoff, and YouTube registration. The exact packaged helper and installer receive offline self-checks before publication. Live updater metadata and published asset checksums are verified separately.

No installed instance, live broadcast, or real chat account was used for testing. The interface remains experimental; live streaming delivery and actual quota savings require verification after updating. Previously consumed YouTube quota cannot be restored by this update.
