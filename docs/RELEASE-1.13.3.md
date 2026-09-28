# Pulse Weaver 1.13.3

Regular Windows release with more efficient incoming YouTube chat.

## Changes

- Prefer YouTube's official `liveChatMessages.streamList` gRPC connection instead of continuously polling incoming chat.
- Share one reader per unique live chat in the unified feed, preserving the latest message cursor for reconnection.
- Keep broadcast controls, chat sending, moderation, and subscriber checks separate from the persistent chat connection.
- Refresh expired access tokens and reconnect, while suspending repeated authentication failures. Credentials travel through a private process pipe, never command-line arguments or transport logs.
- Cancel chat connections when broadcasts are stopped, YouTube is disabled, the account is disconnected, or the application closes. Parent-pipe closure also stops the transport after an application crash.
- Back off interrupted connections. Unsupported streaming, or three transport failures, temporarily enables polling for 15 minutes before streaming is retried. Fallback waits at least 15 seconds and respects longer YouTube polling intervals.
- Quota, rate-limit, authentication, permission, and ended-chat errors do not trigger polling fallback. The persisted quota protection from 1.13.2 remains active.
- Preserve existing message, membership, Super Chat, and moderation processing. Public subscriber checks remain separate at five-minute intervals.

The streaming helper and its runtime are bundled; users do not need to install .NET separately. Existing settings, tokens, ports, and Lumia integration are retained by the normal updater. No new Google project or OAuth permissions are required.

Streaming reduces repeated incoming-chat requests. It does not restore spent quota or remove quota costs for broadcast management and sending chat. Actual savings depend on usage and successful streaming connectivity.

## Verification and limits

Offline tests cover gRPC framing and routing, authentication metadata with a dummy token, resume cursors, error classification, multiple responses, cancellation, shared readers, quota protection, and polling fallback eligibility. Existing broadcast-isolation, Twitch startup, and updater regressions are checked, along with the installer and packaged transport.

No live YouTube account or broadcast was used to validate this transport. Live delivery and actual quota savings still need confirmation after updating and after quota is available. The installed local Pulse Weaver instance was not changed.

Update via Studio → Updates, or use `PulseWeaver-Setup-1.13.3.exe`. The `-BETA.exe` asset is a byte-identical compatibility filename for older updaters; both install the regular release. Lumia 1.4.0 is unchanged.

Implementation references: https://developers.google.com/youtube/v3/live/docs/liveChatMessages/streamList and https://developers.google.com/youtube/v3/live/streaming-live-chat.
