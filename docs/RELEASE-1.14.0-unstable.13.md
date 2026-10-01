# Pulse Weaver 1.14.0 unstable 13

Local test build for YouTube moderation availability and smoother incoming chat.

- Resolve a missing API live-chat ID independently of the web reader. This restores sending and moderation controls when YouTube omits the ID at creation. Existing messages receive the resolved ID for their exact broadcast. Metadata discovery stops after success or three attempts; no API message polling is added.
- Shorten web receive waits to at most two seconds after active batches and five seconds after empty ones, with one request at a time and the existing rate-limit cooldown.
- Display incoming batches in small chunks over at most 600 ms instead of one UI burst. The buffer drains before the next request and cannot accumulate across responses. The resume cursor advances only after the last chunk, preserving messages across interrupted delivery.
- Lay out the chat once per displayed chunk, retaining auto-follow, paused scrollback and unread counts.
- Sending, deletion, timeout and ban remain official API operations. Incoming chat stays in the shared chat box through the web reader.

The web reader remains experimental. Faster web requests reduce the local polling delay but cannot guarantee YouTube delivery latency. Owner/moderator protections still disable unsupported actions. Live moderation against another account has not been exercised automatically.

Verification: 33 web-reader checks, 22 API-helper protocol checks, 60 native lifecycle checks, native transport and chat-rendering regressions. A bounded public-chat check delivered 74 messages in groups of five or fewer, then two messages on the next request, using the two-second active cadence with no Data API credentials.

Stop outputs and close Pulse Weaver before installing. Profiles and connections are preserved by the normal installer. No existing installation is modified while preparing this build.
