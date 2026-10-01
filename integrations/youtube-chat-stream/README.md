# YouTube streaming chat transport

## Default web reader (unstable 12)

The frontend automatically supplies the new broadcast's video ID with `transport: "web"`. The helper reads the anonymous YouTube Live chat page and its continuations and emits messages into the existing combined chat UI. There is one reader per distinct active broadcast, with cancellation on stop and no automatic Data API receive fallback. It never receives OAuth credentials in this mode. Sending messages, deleting messages, timeouts and bans still use the official authenticated Data API in the native frontend.

Unstable 13 bounds web request delays to 1–2 seconds after nonempty batches and 1–5 seconds after empty batches (using smaller server hints within those bounds). Responses are drained in at most 16 chunks, 40 ms apart, adding at most 600 ms of intentional smoothing. That delay is subtracted from the next request wait, and each batch drains before the next request, preventing a growing buffer. Resume cursors advance only after the final chunk. HTTP 429 still triggers the existing five-minute cooldown.

When broadcast creation omits the API live-chat ID, an independent metadata lookup enables sending and moderation without blocking the web reader or resetting its cursor. It stops after success or three attempts, spaced at least ten seconds apart, and backfills existing rows by their exact broadcast ID. It does not call the API chat-message reader.

This is experimental and depends on YouTube's undocumented web page format. Public/unlisted chats must be anonymously accessible; private/restricted or disabled chats may be unavailable. Web HTTP requests still occur, but they do not call the YouTube Data API. API broadcast operations and user-requested sends/moderation still consume API quota. The API receive modes are explicit alternatives for subsequent broadcasts.

## Optional official API reader

Uses Google's documented gRPC `youtube.api.v3.V3DataLiveChatMessageService/StreamList` method over verified TLS. The native frontend starts one process per unique active chat, passes its user access token through stdin, and consumes protobuf responses serialized as NDJSON on stdout. No credentials are written to disk or process arguments. Parent EOF cancels the connection; native cancellation also terminates the process.

`stream_list.proto` is from https://developers.google.com/youtube/v3/live/streaming-live-chat (retrieved 2026-09-28). Google's code samples are licensed under Apache 2.0: https://www.apache.org/licenses/LICENSE-2.0 . Other files follow the repository license.

Build: `dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o <runtime>/bin/64bit/youtube-chat`.

Offline protocol checks: `PulseWeaver.YouTubeChat.exe --self-test`. This does not contact YouTube or load application credentials.

The native caller sets `persistent: true` so one helper/client/channel handles successive RPCs, retaining the latest cursor. `saveQuota: true` backs off after empty-message completions (5/10/20/30 seconds); false uses one second. An empty response batch is not a message. Actual RPC starts/completions are emitted as `_pulse` control records, separate from API response batches. Terminal errors return to the native controller; there is no REST polling fallback. The native controller refreshes expired credentials with a bounded retry and may start a replacement helper.

Save quota is a user-visible latency tradeoff. Retaining a channel alone is not claimed to reduce API calls; the live Python reference also completed its quiet RPCs after roughly ten seconds. See `docs/PULSE-WEAVER-REQUEST-AUDIT-2026-09-29.md` for findings and verification limits.
