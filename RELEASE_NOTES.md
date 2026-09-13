# Public Dist 0.03

YouTube chat now follows the live chat attached to each new broadcast instead of keeping one earlier chat identity. Landscape and portrait broadcasts are discovered and polled independently, first-page messages are retained, outgoing messages reach each active route without duplicates, and moderation targets the exact chat that supplied the message. Temporary chat-ID and API failures retry with visible status feedback.

Kick's visible Show Control route is now the authoritative route. The native Lumia bridge publishes route changes immediately, and the included Lumia plugin 1.1.3 refreshes the current output plan before Start Show or Start Platform. A newly enabled Kick, Twitch or YouTube route can no longer be skipped because Lumia held an older snapshot. End Show remains an explicit stop operation and all Lumia alerts remain off by default.

The public build contains no developer registrations, signed-in accounts or personal configuration. Testers enter their own Twitch, Kick and YouTube app details in Action → Connections; client secrets are protected locally with Windows DPAPI.

Validation covers the native frontend and core build, dual-route YouTube chat/session behavior, bounded chat rendering, and the Lumia action suite. These automated fixtures do not start real platform broadcasts.
