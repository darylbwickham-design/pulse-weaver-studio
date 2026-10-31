# Optional YouTube application credentials

Action → Connections → YouTube now includes a daily quota notice and an expandable **Your YouTube app** section. Users can enter a Desktop app OAuth client ID and client secret, or save both fields empty to select the bundled registration. A six-page, click-through guide covers project creation, enabling YouTube Data API v3, Google Auth platform consent/audience, the requested `youtube.force-ssl` permission, Desktop client creation, connection and ongoing access.

The editor only receives a user's override. It never receives the bundled registration, including when older settings contain a local copy of the publisher's client ID. Custom secrets remain masked. Custom pairs are stored as a single Windows DPAPI-protected value in the existing private app credentials file, outside show exports. Legacy custom pairs remain readable. Saving a partial or malformed pair is rejected before writing.

Changes require outputs to be stopped and the account to be disconnected. Each OAuth object retains the registration that owns its tokens; refresh, quota pauses and stream caching use that registration. Saved token/client bindings reject a later load under another client ID. Existing unbound accounts acquire a binding on their first successful load. No browser login, real chat message or moderation action is exercised by the synthetic checks.

## Sending chat

The active reader is the anonymous web-chat transport in `integrations/youtube-chat-stream/WebChat.cs`; the official gRPC receiver remains implemented separately. This change leaves discovery and reading untouched. The anonymous reader has no authenticated YouTube website session and cannot send messages. Google documents `liveChatMessages.insert` for posting, currently at 20 units per request. `streamList` is a receive-only method. Sending and moderation continue to use the supported authenticated Data API.

An interactive YouTube chat pop-out is another way for a person to post on YouTube itself. Turning the anonymous transport into a sender would require a separate authenticated website session and undocumented write operations; it is not an equivalent extension of the current reader.

There is no automatic switching between projects when quota is exhausted. A custom project remains subject to Google's limits and policies. For a public client requiring more allowance, Google's quota extension process remains the supported route; custom configuration should not be presented as permission to evade restrictions or shard quota across projects.

## Primary sources checked 31 October 2026

- [YouTube quota costs and daily reset](https://developers.google.com/youtube/data/determine_quota_cost)
- [Posting a chat message](https://developers.google.com/youtube/data/live/reference/rest/v3/liveChatMessages/insert)
- [Official streaming chat receiver](https://developers.google.com/youtube/data/live/reference/rest/v3/liveChatMessages/streamList)
- [Desktop OAuth clients, PKCE and loopback redirects](https://developers.google.com/identity/protocols/oauth2/native-app)
- [Google Auth platform branding, audience, test users and scopes](https://developers.google.com/workspace/guides/configure-oauth-consent)
- [Refresh token expiration in Testing](https://developers.google.com/identity/protocols/oauth2#expiration)
- [Personal-use verification exceptions](https://support.google.com/cloud/answer/13464323)
- [YouTube quota policy and project sharding](https://developers.google.com/youtube/terms/developer-policies-guide)

## Verification

`tests/YouTubeRegistration` covers fallback, legacy resolution, publisher-field exclusion, DPAPI storage, complete-pair saves, invalid input, other-credential preservation and client/token binding. Its UI executable checks masking, error propagation, collapsed fields, return to shared mode, button fit at 560/650/720 px, and six-page guide navigation. Run on Windows with the matching Qt `bin` on PATH and `plugins` in QT_PLUGIN_PATH; this Qt distribution includes the `minimal` platform rather than `offscreen`.

The native build must include `ENABLE_BROWSER=ON` and `ENABLE_BROWSER_PANELS=ON` with a compatible CEF SDK. `feature-youtube.cmake` gates `YOUTUBE_ENABLED` on the browser-panel target. A build with browser support disabled can compile the connection card while excluding OAuth and the actual YouTube output/chat implementation; it does not validate this feature.
