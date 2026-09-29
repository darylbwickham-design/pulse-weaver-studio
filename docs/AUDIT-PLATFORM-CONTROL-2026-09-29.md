# Platform and local control audit — 29 September 2026

## Scope

Reviewed the Twitch and Kick runtimes in `pulse-weaver-core.cpp`: authentication, token refresh, connection recovery, category/title changes, chat send/moderation, event delivery, output callbacks, and shutdown. Also reviewed local HTTP dispatch and framing, `pulse-lumia-bridge.hpp`, the Lumia JavaScript integration, and the Lights page's asynchronous controls. Independently reviewed the destination routing callback and the frontend changes requested by the main audit.

This work changes source and workspace test artifacts only. It does not publish releases, modify installed profiles/accounts, call real streaming APIs, change Lumia's configured port, or start/stop real outputs. The user's configured port 18765 is preserved. Existing manifest defaults are unchanged.

## Confirmed defects and changes

| Area | Defect | Refinement |
| --- | --- | --- |
| Twitch login | A repeating device poll could issue another request while the previous request was waiting on its longer network timeout. | One pending device request at a time; schedule the next poll only after an authorization-pending response. Stop/new-login invalidates older replies. |
| Twitch destination | A stream-key reply could arrive after disconnect/account replacement or after a stream had started. | Account generation guard and a second active-stream check before applying the key. |
| Twitch account UI | Metadata, category search, and badge callbacks could overwrite the status/data of a newer account. | Replies are accepted only for the account generation that issued them. |
| Kick authentication | Token exchange, refresh, channel, metadata, moderation and subscription callbacks could complete after disconnect or a replacement login. | Account generation guards across all asynchronous account replies; clear pending ownership at disconnect/new login. |
| Kick refresh | Concurrent callers were rejected while another token refresh was already running. | One refresh request serves the waiting operations. Old-account operations are discarded. |
| Kick initialization | Repeated channel loads could overlap and create duplicate relay sessions/subscription requests. | Coalesce pending channel loads, retain one active relay session, and avoid duplicate successful subscriptions within the same account session. |
| Kick relay | Delayed session responses and uncancellable retry callbacks could restart reception after stopping it. Failures retained the normal polling cadence. Replayed sequence numbers could trigger events twice. | Separate relay generation, owned retry timer, one pending poll, 3–30 second error backoff, return to 1.5 seconds after recovery, and sequence deduplication. Stop during event dispatch cancels the remaining old batch. |
| Kick OAuth callback | The first TCP fragment or an unrelated browser request consumed the login; sockets were not explicitly cleaned up. | Bounded complete HTTP framing, five-second idle timeout, handled-once flag, matching-state requirement, and socket cleanup. Unrelated requests leave the expected login pending. |
| Kick chat | The draft was erased before success and repeated clicks could duplicate a pending send. | Keep failed/new drafts, clear only the matching successful draft, and allow one send at a time. Existing bounded 401 refresh/retry remains. |
| Kick output status | A delayed start/stop from an old output could mark a replacement output live/stopped. | Retain the signalled output through queued delivery, verify it is still the current output, and disconnect callbacks before releasing an output. |
| Lumia plugin | Queued controls could execute after unload/settings reload. Late HTTP completions could restore stale state; suspended event handlers could trigger alerts after unload. | A separate lifecycle generation cancels queued/late work, request completions check their lifecycle, and event handling rechecks after asynchronous steps. |
| Native Lumia bridge | Events queued before shutdown could still be delivered after the bridge stopped watching. | Delivery checks the shutdown state. |
| Lights UI | A network callback held a raw device row that the user could delete before completion. Status text displayed a full Hue URL containing its credential. | Use a persistent model index for row updates and display only the endpoint, without the credential-bearing path. |
| YouTube selector integration | A rapid horizontal → Off → Dual sequence could bypass the new live format guard, and a rejected hidden selector could disagree with the main selector. | Validate against the prepared output mode and explicitly restore the hidden selector with signals blocked. Other routing coalescing changes are owned by the main audit. |

## Verification

All tests below are deterministic and use fake platform replies or a loopback-only fixture. They do not use account credentials or contact Twitch, Kick, Google, or the external relay.

| Suite | Result | Coverage |
| --- | --- | --- |
| `tests/TwitchRuntime` | **25 checks passed** | Compiles the actual Twitch runtime; fake HTTP and WinHTTP transport. Existing readiness, reconnect/backoff, token refresh, send/draft, revocation, event deduplication and handover cases plus device-poll and delayed stream-key cases. |
| `tests/KickRuntime` | **28 checks passed** | Compiles the actual Kick request/runtime and output callback code with fake HTTP/output objects. Covers coalescing, old-account replies, relay/session cancellation, backoff, duplicate events, send drafts, old output signals, and fragmented/unrelated OAuth callback requests. |
| `tests/LumiaPlugin.test.cjs` | **16 tests passed** | Existing start/stop/state/source/motion/port/reconnect tests plus queued controls, stale state and late alerts after unload. |
| `tests/DestinationRouting` | **10 checks passed** | Extracts the actual frontend selector callback into a Qt fixture. Covers coalescing, transient enables, live format rejection, hidden selector synchronization, rapid Off/format changes, and idle changes that must not start output. |

Total: **79 passing checks/tests**. The destination fixture reproduced the hidden-selector failure before the correction and passed afterward. Workspace `git diff --check` passed. Builds are under `artifacts/audit-platform`; full application compilation is coordinated by the main audit.

## Existing controls checked and retained

- Twitch EventSub handover retains transferred subscriptions; ordinary reconnect backoff remains capped, and event IDs are deduplicated.
- Kick title/category/moderation still use verified permissions and the existing bounded authorization retry. No requested scopes or platform features were removed.
- Local operator HTTP remains bound to loopback with bearer authentication, a bounded request buffer, complete body framing, handled-once dispatch and connection limits.
- Lumia uses pushed state/events with heartbeat and bounded queues, and does not poll YouTube events. Its stop controls still bypass a pending start.
- No YouTube Data API polling path exists in these Twitch/Kick/Lumia changes; the YouTube transport audit is documented separately.

## Limits and follow-up

The tests verify application control flow and request construction using fake services. They do not prove real platform availability, vendor rate-limit behavior, OAuth approval in a browser, or real encoder/stream performance. Kick output stop/release integration and the Lights widget correction also need the main native build; real hardware light endpoints were not exercised. The Kick relay service itself and third-party browser-source scripts are outside this source slice. The Lumia plugin source changes need a deliberate local plugin update to affect an already installed Lumia plugin; this audit does not silently overwrite that installation.

User-authored automation rules can deliberately call HTTP endpoints or create event cycles. This pass reviewed their dispatch and lifetime boundaries but did not redefine those user-authored behaviors. No claim is made that all upstream OBS code or every external plugin is free of defects.
