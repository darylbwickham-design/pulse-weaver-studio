# YouTube chat documentation audit — 29 September 2026

Scope: compare unstable 9 (source commit 16f2b40) with Google's current streaming-chat guide, API references, and the installed-build investigation. This document changes no application behavior.

## Documented behavior

Google recommends a persistent `liveChatMessages.streamList` connection. Responses arrive through that connection. Retain each `nextPageToken` for resumption. The official Python example includes an outer loop that calls StreamList again after its response iterator finishes. It does not specify a minimum connection lifetime or classify a short, successful completion as a transport failure.

Sources:
- https://developers.google.com/youtube/v3/live/streaming-live-chat
- https://developers.google.com/youtube/v3/live/docs/liveChatMessages/streamList

For REST `liveChatMessages.list`, retain the continuation token and wait at least the returned `pollingIntervalMillis`. This is repeated polling; it incurs requests even when chat is quiet. Google's quota table currently lists this method at one unit per call, but does not list a separate streamList price. Neither a connection-count promise nor an exact streamList quota saving follows from these documents.

Sources:
- https://developers.google.com/youtube/v3/live/docs/liveChatMessages/list
- https://developers.google.com/youtube/v3/determine_quota_cost

## Correct parts of Pulse Weaver

- The helper uses Google's gRPC hostname, service, and server-streaming RPC, with the user's bearer token.
- The reviewed request and response protocol fields match the official example, including the continuation-token field numbers.
- The helper consumes responses in an asynchronous loop. Receiving one batch does not itself start a new request.
- Native code preserves the latest cursor and resumes with it. It prevents concurrent requests for the same chat ID within this instance.
- Two distinct broadcasts may require two distinct chat subscriptions. Landscape and portrait alone are not a reason to duplicate an identical chat ID.
- REST fallback respects a longer server polling interval. Stopping outputs cancels workers and clears sessions.

## Confirmed implementation problems

1. **Unsupported short-completion rule.** `OBSBasic_PulseWeaver.cpp:6066` converts an otherwise successful stream completion under 60 seconds into `streamUnavailable`. After three such results, lines 5988–5990 choose REST polling for 15 minutes. The rule is not in Google's documentation. A clean completion should be distinguished from an actual transport failure and resumed with the cursor, subject to bounded reconnect protection.

2. **Automatic polling becomes the steady state.** Line 6083 sets a minimum REST interval of 15 seconds. With two separate chats and no longer server interval, that means about 480 requests/hour. The new screenshot's approximately 0.136 requests/second is consistent with that behavior. The short StreamList bursts are consistent with periodic streaming retries, but their individual outcomes are not logged.

3. **Locally imposed deadlines are treated as transport faults.** `integrations/youtube-chat-stream/Program.cs:73` sets a 45-minute RPC deadline. Its status mapping collapses DeadlineExceeded into `streamUnavailable`. Expected renewal should have a distinct outcome. This cannot explain a connection ending after only seconds, but it can contaminate failure counting during longer sessions. Microsoft documents that elapsed deadlines cancel the call and produce DeadlineExceeded: https://learn.microsoft.com/en-us/aspnet/core/grpc/deadlines-cancellation

4. **Failure counts do not represent consecutive unsuccessful connections.** `streamFailures` increments for eligible errors but resets only after a successful finished stream. Successful batches during a long-lived stream do not reset it. Failures separated by useful service can therefore accumulate into fallback.

5. **Diagnostic detail is lost.** The helper maps multiple statuses and exceptions to one generic result. The native launcher discards stderr; neither layer records safe connection-lifetime and result counters. The installed logs cannot establish whether the original sessions ended normally, timed out, had HTTP/2/network errors, or failed locally. The previous assertion that the graph proved a specific connection failure was too strong.

6. **REST offline handling is incomplete.** `GetLiveChatMessages` parses items and the polling interval but does not act on `offlineAt`. The streaming path does. An externally ended broadcast should stop REST polling when an offline indication is returned, without waiting for a later error. The ordinary local stop path already cancels polling.

## Evidence and limits

The installed unstable 9 core and helper hashes match the published package. The installed helper passed 12 local mock protocol checks. Those checks exercise protocol serialization, two mocked streamed responses, and error mapping; they do not reproduce Google's connection behavior or the full native/helper process lifecycle. No live API diagnostic requests were made during this audit. No installed files or settings were changed.

The confirmed root cause of the second test's repeated List traffic is the fallback policy. The exact cause and status of the original StreamList endings remain unknown. The daily quota total cannot be reconstructed from an instantaneous request-rate chart; per-operation counters are needed to account for setup, chat, and cleanup separately.

## Implementation and validation requirements

- Preserve distinct outcomes for normal completion, user cancellation, planned renewal, terminal API errors, and transient transport errors.
- Keep streaming as the normal path; resume with the latest cursor after completion. Bound repeated short reconnections without falsely labeling successful calls as failures or silently entering an unlimited polling cycle.
- Record only safe diagnostic fields: operation, route label, duration, batch count, numeric gRPC status, allowlisted error category, and retry/fallback decision. Never log tokens, cursors, chat contents, keys, or raw exception text.
- Make fallback visibly degraded and bounded; honor server intervals and avoid continuous polling of quiet or ended chats. Do not simply slow all chat until it becomes unusable.
- Count actual requests by operation, including retries. Mark estimates separately from Google's measured quota.
- Test native/helper lifecycle with multiple batches over one connection, clean short completion, cursor resumption, planned deadline, transient failures separated by successful service, terminal/offline responses, Dual deduplication, and cancellation.
- Verify the corrected behavior in a controlled live session before claiming reduced quota usage or publishing another quota-fix release.


## Implemented correction

Experimental alpha 3 / unstable 10 removes the under-60-second failure classification, automatic REST fallback, and the 45-minute RPC deadline. Normal streamed completion resumes with the cursor after a one-second scheduling delay. Only completions with no response batches receive increasing delays (5 seconds up to 5 minutes); that safeguard does not label a successful RPC as an error. Transport failures retain exponential retry delays and terminal errors stop. Healthy service resets error history.

Diagnostics record application-level REST attempts by allowlisted operation and streaming attempts with duration, batch count, numeric status, and normalized result. These counters are not claimed to equal billed Google quota. No real YouTube request was made for these tests.

Validation passed: 14 helper protocol checks, 16 native/helper lifecycle checks, and the changed frontend build against existing unchanged dependencies. Experimental packages support the next live verification; they do not constitute measured proof that the original live connection issue or daily quota total is resolved.
