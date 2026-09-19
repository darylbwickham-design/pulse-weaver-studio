# Overlay renderer browser regression

Build `tests/OverlayAlerts` first, then run `node tests/OverlayBrowser/server.cjs` from the repository root. It prints a temporary loopback URL. Generated fixtures use the actual production C++ renderer, with synthetic credentials, legacy event handlers and instrumented audio/TTS; they do not access the installed application.

Open `/overlay/fixture#token=synthetic-browser-test-token` and `/preview/test/fixture#token=preview-test-token` at that address. Use JSON POSTs to `/control/event` and `/control/preview` to send event payloads. Only the corresponding page should change. The diagnostics show legacy event delivery, parent-origin access blocked, and sound/TTS counters. The intentionally hostile CSS/font fields must not execute code in the trusted parent.

Send an alert such as `{"user":"First","__pwAlert":{"sound":"https://example.invalid/test.wav","tts":true,"message":"Hello","remainingMs":60000}}`. The fixture stubs audio, so no actual network media or speech is played. Sending another alert must keep `maxActive` at 1. `{"__pwControl":"mute","muted":true}`, `{"__pwControl":"complete"}` and `{"__pwControl":"skip"}` must make `active` 0; mute must leave the current visual text intact. Unmute with the same control and `muted:false`.

POST `{}` to `/control/publish`: the page must change from Revision 1 to Revision 2 and `/state` must show another render fetch. POST `{}` to `/control/reconnect` while an alert is active: the existing alert snapshot must arrive without increasing the audio play count. Ordinary alert duration expiry must also stop audio. Close tabs and terminate the fixture server after testing.

This is production-renderer/browser coverage over a synthetic transport. Native queue timing, authentication validation and migration are separate native tests; do not describe this fixture as a complete application integration test.

For the disposable native startup smoke, copy the built runtime (excluding config) to `artifacts/alert-repair-native`, launch it in portable mode with `bin/64bit` as its working directory, then close it normally. Run `node tests/OverlayBrowser/native-fixture.cjs` once and restart that copy. The script seeds a schema-1 overlay and existing scene source without reading installed settings. Keep a browser connected when closing the test app to cover the repaired socket-teardown crash. See the repair validation report for the quarter-pixel fixture caveat and remaining release gates.
