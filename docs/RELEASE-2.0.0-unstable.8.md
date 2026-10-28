# Pulse Weaver 2.0.0-unstable.8 — Receive-only Discord chat

Connect Pulse from the main Chat options menu using **Discord channels…**. Approve the installation in your browser, then choose a server and up to five channels. Private moderator channels are supported when both your account and Pulse can read them. Active threads also appear in the selector.

Discord messages join the native combined feed and can be filtered by Discord or individual channel. The connection is receive-only; the composer sends only to Twitch, YouTube and Kick. Selected channel access is checked on every poll. Disconnect in the chat menu or manage connections on Pulse's connection page.

This first version refreshes bounded snapshots every six seconds, shows up to fifty recent messages per channel, reconciles edits and removals, and displays attachment filenames. It does not enumerate all archived threads or provide an unlimited message archive. The client uses Windows DPAPI for its separate connection credential; no bot token is included in the desktop.

The regular installer updates the existing unstable installation. The **2 Preview** installer updates the separate preview installation. Existing show configuration is retained through the installer backup flow. Regular 1.14.3 and the Mac preview are unchanged.

Validation: the native application compiled, native Discord rendering/filter/edit/removal tests passed, the Pulse suite passed 284 tests, and the deployed endpoints rejected unauthenticated feed access. Real-account browser pairing and an actual Discord-to-desktop message remain acceptance tests for the connected installation. No live broadcast or punishment test was performed.
