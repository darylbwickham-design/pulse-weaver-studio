# Pulse Weaver 1.13.2-alpha.2

## Twitch output fixes
- Twitch now offers Off, 16:9 and Dual. Legacy portrait-only selections migrate to Dual.
- 16:9 clears the additional portrait canvas. Existing Enhanced Broadcasting quality settings are retained; Dual enables Enhanced Broadcasting and attaches the current portrait canvas automatically.
- Each stream start re-evaluates Enhanced Broadcasting and rebuilds its streaming configuration, including when recording, replay buffer or virtual camera keeps the output handler alive.
- Before connecting, the prepared video encoders must match the requested format. Pulse Weaver refuses an accidental portrait output in 16:9 or a landscape-only fallback in Dual, instead of reporting the wrong format.
- Connecting is now a distinct startup state. An accepted asynchronous start no longer produces the false "Twitch did not start" message before becoming live.
- Cancellation invalidates old callbacks, including deferred secondary destinations. YouTube/Kick startup following Twitch waits for Twitch's connected event instead of a fixed 1.2-second timer.
- Changing Twitch format while connecting/live is blocked with an inline explanation; stop Twitch, select the new format, and start again. Off can cancel a connection.

## Verification
Production routing/start-state regression tests cover Dual-to-landscape switching, stale portrait clearing, standard/enhanced landscape, mismatched negotiated formats, delayed connection, failure and cancelled callback generations. Broadcast-isolation and updater regression suites also passed. These are isolated tests, not a live Twitch transmission test.

## Update and rehearse
Open Studio > Updates with Alpha enabled and update to 1.13.2-alpha.2. After updating, test 16:9, stop, test Dual, stop, then test 16:9 again. Check Twitch's received formats as well as Pulse Weaver's status. Existing accounts, ports, scenes and audio/VOD settings are preserved. No Lumia plugin update is required.