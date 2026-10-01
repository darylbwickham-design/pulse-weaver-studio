# Pulse Weaver 1.14.0

Regular Windows release promoting the tested 1.14 Studio interface and chat refinements.

## Chat

- YouTube incoming messages automatically use the web chat scanner in the combined Twitch/YouTube/Kick chat box. Every new broadcast connects automatically.
- Sending messages, deleting messages, timeouts and bans use the official authenticated API. Broadcast start/stop/title operations also retain the API.
- Remove the reader dropdown. Existing profile choices cannot switch reception back to API polling, and there is no automatic API receive fallback.
- Resolve missing API live-chat IDs independently, enabling moderation and sending without blocking reception. Discovery stops after success or three attempts.
- Active web-chat waits are bounded to 1–2 seconds and empty waits to 1–5 seconds. Incoming batches drain in small chunks over at most 600 ms, with one layout per chunk. Scrollback, unread counts, message ordering and cancellation are preserved.
- No follower/like polling is added. Web requests still occur; broadcast management and user-requested API actions still consume quota.

## Studio and upgrading

Includes the 1.14 Control interface, saved Look previews, guided show setup and the audited output, source, settings and installer recovery fixes from the preview builds. Lumia companion remains 1.4.1 with existing action IDs.

This release supersedes 1.14 alpha/unstable previews. Regular and preview installations can update to 1.14.0; older releases remain available as history. The installer preserves profiles, scenes and connections and creates a recovery backup. Close Pulse Weaver and stop outputs before installing.

Downloads: `PulseWeaver-Setup-1.14.0.exe`, `PulseWeaver-Lumia-1.4.1.lumiaplugin`, `SHA256SUMS.txt`.

The scanner depends on YouTube's web format and anonymously accessible live chat. Private, restricted or disabled chat may be unavailable. Web-side changes can require an update. This is not a guarantee of zero latency or zero API quota consumption.

Verification includes native Windows build, offline helper/lifecycle/chat-layout regressions, installer diagnostics and upgrade recovery. Public web-chat reception and bounded chunk delivery were checked; the user confirmed the preceding local test build works.
