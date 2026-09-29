# YouTube streaming chat transport

Uses Google's documented gRPC `youtube.api.v3.V3DataLiveChatMessageService/StreamList` method over verified TLS. The native frontend starts one process per unique active chat, passes its user access token through stdin, and consumes protobuf responses serialized as NDJSON on stdout. No credentials are written to disk or process arguments. Parent EOF cancels the connection; native cancellation also terminates the process.

`stream_list.proto` is from https://developers.google.com/youtube/v3/live/streaming-live-chat (retrieved 2026-09-28). Google's code samples are licensed under Apache 2.0: https://www.apache.org/licenses/LICENSE-2.0 . Other files follow the repository license.

Build: `dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o <runtime>/bin/64bit/youtube-chat`.

Offline protocol checks: `PulseWeaver.YouTubeChat.exe --self-test`. This does not contact YouTube or load application credentials.

The native caller sets `persistent: true` so one helper/client/channel handles successive RPCs, retaining the latest cursor. `saveQuota: true` backs off after empty-message completions (5/10/20/30 seconds); false uses one second. An empty response batch is not a message. Actual RPC starts/completions are emitted as `_pulse` control records, separate from API response batches. Terminal errors return to the native controller; there is no REST polling fallback. The native controller refreshes expired credentials with a bounded retry and may start a replacement helper.

Save quota is a user-visible latency tradeoff. Retaining a channel alone is not claimed to reduce API calls; the live Python reference also completed its quiet RPCs after roughly ten seconds. See `docs/PULSE-WEAVER-REQUEST-AUDIT-2026-09-29.md` for findings and verification limits.
