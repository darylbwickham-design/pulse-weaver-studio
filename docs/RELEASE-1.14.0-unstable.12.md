# Pulse Weaver 1.14.0 unstable 12

Experimental automatic YouTube web-chat reception in the existing combined Twitch/YouTube/Kick chat box.

- Incoming YouTube messages use the web reader by default. Each new broadcast supplies its chat URL automatically; no pasted links or separate chat windows.
- Sending messages and moderation (delete, timeout and ban) retain the official authenticated YouTube API. Start/stop/title operations also retain the API.
- No automatic fallback to API chat polling. API receive modes remain explicit alternatives, selected for the next broadcast.
- Each distinct active broadcast has one receive session. Web requests follow YouTube's continuation delay; stop/restart cancels the old session. No follower/like polling is added.
- Existing profiles, scenes and connections are preserved. Lumia companion remains 1.4.1.

This reader uses an undocumented web format and is experimental. Private/restricted, disabled or otherwise anonymously inaccessible chat may not work. Emoji can appear as text equivalents. Web reception still makes web requests; it does not remove API costs for broadcast management, sending or moderation. Live end-to-end sending/moderation with this reader still needs user acceptance testing.

Verified: native Windows build; 25 offline web-reader checks; 22 existing helper protocol checks; native lifecycle, quota, transport and chat-rendering suites. A bounded anonymous public-chat check received two batches (75 and 6 messages) and respected the server's 10-second continuation delay, without OAuth or Data API requests.

Choose **Studio → Updates → Include unstable showcase builds**, or use `PulseWeaver-Setup-1.14.0-unstable.12.exe`. Stop outputs before updating. Alpha and stable channels are unchanged.
