# Pulse Weaver 1.14.0 unstable 9

This showcase build includes the same YouTube request reduction as experimental alpha 2 while retaining unstable 8's Show Control interface.

- Detect repeated chat `streamList` connections that close cleanly within one minute. After three, use the existing REST polling fallback for 15 minutes and then retry streaming. Fallback requests wait at least 15 seconds and respect a longer interval from YouTube.
- Remove Pulse Weaver's separate recent-subscriber poll. Lumia can continue handling follow and like events. Chat messages and chat moderation remain available.
- Keep existing OAuth access, accounts, scenes, credentials, port numbers and output routing.

The change responds to project metrics showing roughly one `streamList` request every eight seconds during a short stream. The exact contribution of one installation to the project's daily quota and the live effect of this update still require measurement.
