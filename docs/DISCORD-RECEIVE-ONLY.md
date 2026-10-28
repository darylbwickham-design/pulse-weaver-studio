# Discord in the main chat panel

Choose **Discord channels…**, approve the connection in the browser, then select a server and up to five readable channels. Both your account and Pulse must have access. Private moderator channels are supported. Active threads appear in the selector; not every archived thread is enumerated.

Use **All chats**, **Discord**, or individual channel filters. Discord is receive-only. The composer continues to send to Twitch, YouTube and Kick. **Disconnect Discord** clears Discord rows, removes the local credential and requests revocation. Connections can also be revoked at https://pulse-community.darylbwickham.workers.dev/connect.html.

Bounded snapshots refresh every six seconds, reconciling edits and removals without saving chat to a local log. Up to fifty recent messages per selected channel are shown. Attachments appear as filenames; reconnects cannot recover unlimited history. Credentials last up to thirty days and use Windows DPAPI protection.

This integration starts from published `v2.0.0-unstable.7`. The `tests/DiscordBridge` target checks rendering, edits, removals, duplicate suppression and filters without connecting an account or posting. Build with Qt Widgets and Network. Use the available `minimal` Qt platform plugin for the prepared dependency package; `offscreen` is not included.
