# Pulse Weaver request audit — 29 September 2026

## Scope and conclusion

Reviewed the request-producing paths across the packaged native frontend, Pulse Weaver core plugin, chat helper, OBS YouTube integration, browser docks, overlay configuration, Lumia bridge, and update components. Baseline: `73486912c5e202042023b860092a072852f45fe2`, published unstable 10 / experimental alpha 3. This is a request and lifecycle audit across the application, not a claim that every line of OBS, Chromium, third-party overlays, or every unrelated UI feature has been exhaustively verified.

**There are real avoidable-request and lifecycle defects. The inspected build does not contain a second active YouTube chat polling implementation or a YouTube subscriber/like poller.** Two distinct broadcasts currently mean two chat subscriptions. The earlier build's silent REST fallback was a separate confirmed defect, already removed before this local patch.

The independent live comparison reproduced Google's roughly ten-second successful StreamList completions using Google's Python example and a retained channel. This rules out Pulse Weaver's process teardown and request fields as necessary causes of those short completions. It does not explain Google's internal behaviour. See [the reference comparison](../tests/YouTubeReference/README.md) and [the earlier documentation audit](YOUTUBE-CHAT-DOCUMENTATION-AUDIT.md).

## Coverage

| Component | Request path and finding |
| --- | --- |
| Native Pulse Weaver frontend | `OBSBasic_PulseWeaver.cpp` schedules one receive worker per distinct chat ID. Inspected preparation, delayed Dual start, cancellation, cursor handoff, sends, moderation, quota pauses, and cleanup. One-second UI/session timers do not each issue a chat request. |
| Chat helper | `Program.cs`, `ChatSession.cs`, protocol and native `PulseYouTubeStream.hpp`. Streaming responses are consumed until completion; no REST fallback or follow/like polling. Checked metadata, errors, parent exit, cancellation, token renewal and continuation tokens. |
| YouTube REST wrapper | `YoutubeApiWrappers.cpp` is the shared Data API entry point. Counts individual attempts; serializes calls and observes quota pauses. `GetRemoteFile` performs one curl request per invocation; no application retry loop hidden below the wrapper. REST 401 retry is bounded to one refresh/retry. |
| Native OBS YouTube integration | `OBSYoutubeActions`, `YouTubeAppDock`, `YoutubeAuth`, `OBSBasic_YouTube` and `OBSBasic_Streaming`. User-driven setup/list/edit calls exist. Native manual-start readiness checks can issue up to fourteen stream reads, then stop; guarded by the actual primary service and auto-start setting. They are not continuous chat polling. |
| Pulse Weaver core plugin | Connection widgets delegate YouTube actions to the native frontend. The plugin contains event names and handlers but no YouTube Data API subscriber/like polling endpoint. Twitch uses its own OAuth/Helix/EventSub. Kick polls its relay every 1.5 seconds with an in-flight guard; those requests are to the relay, not this Google project. |
| Animation/audio/overlays | Motion and sample-clock timers operate locally. Overlay image/network loading and configured third-party browser sources exist. The installed scene-collection URL inventory contained no direct YouTube Data API source URL. Third-party page scripts were not packet-captured or exhaustively audited. |
| Lumia integration | Reads local state and receives pushed output events. Explicit start/stop actions call the local controller, followed by one state refresh; waiting for confirmation uses local events. It does not receive YouTube chat through an independent Data API client. Existing local port and settings are preserved. |
| Packaging/install/update | Regular installed core/helper were matched to the published payload before modification. The staged package has the native frontend and helper, not the legacy WPF app. Updater checks use release infrastructure, not YouTube. This patch changes two local executables only; no releases, tags, updater manifests or website changes. |

The unused REST chat-list wrapper remains for source compatibility; the call-site search found no active reader using it. A YouTube browser chat/Studio dock loads YouTube's web application, which is distinct from calls using Pulse Weaver's Data API registration.

## Defects and refinements in this local patch

1. **Empty batches defeated quiet-chat protection.** A response batch with zero messages counted as activity, resetting the delay. The helper now counts messages and retains its client/channel across RPCs. In Save quota mode, successive empty completions wait 5, 10, 20, then 30 seconds; a message resets the delay to one second. Responsive mode uses one second. Every returned cursor is retained. This reduces requests by introducing a visible latency tradeoff, not by claiming an undocumented streaming mechanism.
2. **Preparing a broadcast started chat before output.** Preparation no longer starts chat. Scheduling is gated independently by the actual landscape/portrait output. Inactive routes cannot discover, receive or send chat. Pending work is cancelled and drained before resuming; prepared state and cursors remain available.
3. **A delayed Dual start could outlive its session.** The 1.5-second callback now checks the session generation, prepared mode and whether a second output already exists. An old callback cannot start a second encoder for a replacement session.
4. **Fresh ingestion resources were created every show.** Pulse Weaver now caches reusable ingestion stream IDs separately for account and orientation, validates ownership, reusability, inactive state and format before reuse, and obtains the key from YouTube. The cache never stores stream keys. Rejected lookups fail without creating more resources. New broadcasts still represent new shows; an active cached stream is not taken over.
5. **Expired REST tokens wasted a failed API attempt.** Expiry is checked before the first request. Refreshed credentials are persisted. Simultaneous chat readers reuse an already-refreshed token instead of both refreshing the same rejected token.
6. **A chat ID already returned by creation was discarded.** Retain `snippet.liveChatId` from the creation response. Resolve it later only when YouTube has not supplied one yet.
7. **Transport status was inferred from process exit.** The helper now reports actual RPC completion/status. A clean process exit without a completed current RPC is an error. Logs separate helper sessions from actual RPC starts, completions, messages and delays.
8. **Persistent-session token renewal needed a different retry lifetime.** The old two-attempt wrapper was suitable for one RPC. A persistent session can outlive several access tokens. A healthy session now restores the single authentication retry budget; repeated immediate authentication failures still stop. This prevents the new retained-helper design from failing after a second token expiry.

## Expected effect and practical limits

- A server RPC that completes after about eleven seconds plus the old one-second delay produces roughly 300 starts/hour/chat. At the quiet cap, eleven seconds plus thirty seconds is roughly 88 starts/hour/chat. This is a model of quiet behaviour, not a measured post-patch quota result. Busy chats, reconnects and server behaviour change it.
- Reusing two compatible ingestion streams replaces two 50-unit inserts with two 1-unit reads on later Dual shows: approximately 98 units saved for those operations. The first patched show still creates its reusable streams. Channel lookup, broadcast creation, binding, cleanup and user actions have their own costs. See [Google's quota table](https://developers.google.com/youtube/v3/determine_quota_cost).
- The observed 3,898 → 4,536 increase was 638 units. The captured session's eight successful 50-unit writes plus four reads explain 404 units. The remaining 234 are not claimed to have been exactly allocated; Google's public table does not separately state a StreamList unit price.
- Save quota can add up to 30 seconds between quiet RPCs. It continues from the saved cursor. Responsive is available in the chat pane. Changes take effect on the next connection.
- Dual still creates two distinct YouTube broadcasts/chats. Google's Studio help describes a shared-chat dual-format workflow, but the reviewed public API does not document configuring its second encoder feed. No unsupported endpoint or guessed field is used.
- Multiple independently running installations could each subscribe to the same chat. The in-instance owner guard does not act as a machine-wide or account-wide lock. No additional Pulse Weaver process was running at the local replacement preflight.

## Validation

- Packaged .NET helper: **22 offline protocol/session checks passed**.
- Native helper lifecycle: **41 checks passed**, including shared-chat ownership, partial output failure, inactive outputs, cursor preservation, repeated long-session token expiry, and invalid final status.
- Existing YouTube quota, streaming transport and broadcast-isolation suites passed after rebuilding. An obsolete fallback assertion in the old transport suite was updated to the current streaming-only behaviour.
- Final native frontend compiled against the existing runtime dependencies.
- No new live YouTube diagnostic calls or broadcasts were made during this implementation pass. A subsequent live stream is still required to measure quota savings and message latency; offline mocks cannot prove those.

## Local installation and startup verification

The regular installation was updated locally on 29 September at approximately 22:57. The separate Clean Test installation was not changed. Only `PulseWeaverCore.exe` and `youtube-chat/PulseWeaver.YouTubeChat.exe` were replaced; their installed SHA-256 hashes match the final local builds. The visible release identity remains unstable 10 because no release was created.

A verified recovery copy contains both original executables and all 1,552 configuration files (about 672 MB), under `artifacts/local-youtube-quota-fix/recovery-20260929-225019`. Its `Restore-Executables.ps1` restores just the original executables after the app is closed. It does not overwrite subsequent settings edits. The full configuration backup is separate.

The first startup smoke check caught a bug introduced by the new preference widget: `InitPulseWeaverShell` runs before the profile exists, so reading it immediately caused a null-config crash. The control now waits for profile initialization, guards saves, and synchronizes when the active profile changes. The corrected executable reached `Startup complete`, remained responsive, and retained the Lumia listener on port 18765. No YouTube REST/session/RPC attempts appeared in the idle startup log. The app was minimized while the user was interacting, so no final visual inspection of the new dropdown was completed.

All existing profile and scene files remained identical. The credential file was re-encrypted by normal startup; an in-memory DPAPI comparison confirmed the same fields and secret values. No credential values were printed or sent anywhere. Normal logs/profiler retention changed diagnostic files during the startup checks. See the sanitized local `artifacts/local-youtube-quota-fix/startup-verification.json` and `installed-receipt.json` records.

## Documentation used

- [Official streaming example and protocol](https://developers.google.com/youtube/v3/live/streaming-live-chat)
- [StreamList reference](https://developers.google.com/youtube/v3/live/docs/liveChatMessages/streamList)
- [Reusable ingestion streams](https://developers.google.com/youtube/v3/live/docs/liveStreams#contentDetails.isReusable)
- [Broadcast chat ID](https://developers.google.com/youtube/v3/live/docs/liveBroadcasts#snippet.liveChatId)
- [gRPC channel reuse guidance](https://grpc.io/docs/guides/performance/)
- [YouTube Studio dual-format help](https://support.google.com/youtube/answer/2474026?hl=en)

Public reports of similar short EOF behaviour were also found, but were not treated as proof of Google's cause or as a documented fix. The bounded live reference experiment is the stronger evidence here.
