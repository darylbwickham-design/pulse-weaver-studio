# YouTube streaming chat transport

Uses Google's documented gRPC `youtube.api.v3.V3DataLiveChatMessageService/StreamList` method over verified TLS. The native frontend starts one process per unique active chat, passes its user access token through stdin, and consumes protobuf responses serialized as NDJSON on stdout. No credentials are written to disk or process arguments. Parent EOF cancels the connection; native cancellation also terminates the process.

`stream_list.proto` is from https://developers.google.com/youtube/v3/live/streaming-live-chat (retrieved 2026-09-28). Google's code samples are licensed under Apache 2.0: https://www.apache.org/licenses/LICENSE-2.0 . Other files follow the repository license.

Build: `dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o <runtime>/bin/64bit/youtube-chat`.

Offline protocol checks: `PulseWeaver.YouTubeChat.exe --self-test`. This does not contact YouTube or load application credentials.
