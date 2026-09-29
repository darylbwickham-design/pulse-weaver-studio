# Bounded live reference comparison

`probe.py` adapts the request/channel/response loop from Google's official Python
example: https://developers.google.com/youtube/v3/live/streaming-live-chat .
Generate Python bindings from `integrations/youtube-chat-stream/stream_list.proto`
using `grpcio-tools` and make their output directory available on `PYTHONPATH`.
Use a workspace virtual environment; do not install into the application runtime.

This is an opt-in live diagnostic, not a routine test suite. Run only with explicit
user authorisation while their YouTube broadcast is already live. It reads the
specified install's protected YouTube credential using Windows DPAPI. Tokens,
message content, chat IDs, cursors, and raw exception details are never output.

Invocation: `python probe.py <install-directory> [--refresh]`.

Without `--refresh`, a rejected stored access token stops the diagnostic. The
optional refresh obtains one temporary token from Google's OAuth endpoint using
the existing registration and refresh credential. It never changes installed
credentials/configuration. No token is passed on the command line or in an
environment variable. Standard Python/gRPC trace logging must remain disabled.

Bounds per invocation: one broadcast lookup, at most six StreamList calls, and
100 seconds total streaming time. A call is locally cancelled at 45 seconds or
the remaining overall budget, with that cancellation explicitly marked. There
are no automatic error retries. The diagnostic never creates or changes a
broadcast or sends/moderates a message. The first distinct active chat is used
for both arms; other chats are not probed.

The first three calls use Google's demo fields (`snippet`, max_results=20);
the next three use Pulse Weaver's fields (`id`, `snippet`, `authorDetails`, no
max_results override). Both use the same retained Python gRPC channel. Each arm
retains its own resume cursor across calls. There is a one-second pause between
calls. A new request is made only after the previous iterator has finished.

This runs alongside the app's existing subscriptions, so server behaviour under
that concurrency is a limit of the comparison. Six quiet-chat responses cannot
establish all behaviour of active chats or a StreamList quota price.

## 29 September 2026 result

During the user's authorised live test, active-broadcast discovery returned two
distinct chats. All six probe calls returned gRPC OK, exactly one empty batch,
and an advancing resume cursor. Durations in milliseconds:

- Demo fields: 10625, 10344, 10469.
- Pulse Weaver fields: 10375, 10500, 10406.

No call hit the local time limit. Pulse Weaver's simultaneous calls in its own
log continued to complete around 10.8–11.0 seconds. This reproduces the short
successful RPC completions independently of the .NET helper, its process
lifecycle, and Pulse Weaver's UI scheduler. It does not prove why Google's
service chooses this response lifetime.

The diagnostic's initial stored-token attempt returned HTTP 401 and stopped.
One explicitly enabled in-memory OAuth refresh then succeeded. Total live
diagnostic traffic across both invocations: two broadcast lookup attempts, one
OAuth refresh, six StreamList calls. No broadcast, message, or installed file
was modified.
