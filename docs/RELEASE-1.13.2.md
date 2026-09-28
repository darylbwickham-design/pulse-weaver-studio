# Pulse Weaver 1.13.2

Regular Windows release, including the guided setup and show editor improvements from 1.13.2-alpha.2 and its Twitch 16:9/Dual startup fixes.

## YouTube quota and chat reliability

- Respect the full polling interval returned by YouTube; longer intervals are no longer shortened to 15 seconds.
- Back off failed chat discovery and polling instead of repeatedly rediscovering the same broadcast. Keep the chat cursor during temporary failures.
- Pause YouTube API requests after daily quota exhaustion until the next midnight Pacific reset, with a one-minute allowance. This pause survives restarting Pulse Weaver and covers both YouTube canvases and broadcast preparation.
- Temporarily pause requests on rate-limit responses, including HTTP 429 without a JSON error body.
- Stop polling ended, disabled, or inaccessible chats. Check public subscribers at most once every five minutes, without repeating those checks after failed chat requests.
- Explain quota exhaustion and its local reset time in the interface. Twitch and Kick remain independent. Raw YouTube request and response debug logging has been removed.

This update cannot replenish quota already spent. Reconnecting a YouTube account does not replenish quota either. Wait until the displayed reset time before preparing a new YouTube broadcast. The bundled Google API project has shared quota; this fix reduces avoidable requests but does not increase Google's allocation. Chat still uses polling, not the streaming API.

## Updating

Use Studio → Updates, or download `PulseWeaver-Setup-1.13.2.exe`. The installer retains existing settings, credentials, and ports and uses the existing backup/rollback flow. This regular release also supersedes 1.13.2-alpha.2 for alpha-channel users. The separate unstable showcase is not changed.

The byte-identical `PulseWeaver-Setup-1.13.2-BETA.exe` asset is a compatibility filename for older updaters. Both assets install the same regular release. The existing Lumia 1.4.0 plugin is included for convenience; no plugin or port change is required for this fix.

## Verification

Quota regressions cover persisted pauses, automatic expiry, separate registrations, daily versus temporary throttling, DST reset dates, server polling intervals, bounded retries, cursor retention, and stopped chats. Existing broadcast-isolation and Twitch startup regressions are also run. Installer self-checks and current/legacy updater selection are checked before publication is considered complete.

No live YouTube broadcast or destructive moderation action was used for testing. The developer's installed Pulse Weaver instance and profile were not changed.
