# Pulse Weaver 1.14.0 experimental alpha 2

This alpha retains the Control interface from alpha 1 and reduces avoidable YouTube API requests.

Google Cloud metrics for a short stream showed `liveChatMessages.streamList` running about once every eight seconds. The application could treat a short, clean stream closure as success, reconnect after five seconds, and reset its failure count after each response. Now three early closures within a minute of opening a chat stream move that chat to the existing 15-minute REST polling fallback, which waits at least 15 seconds and honours longer intervals returned by YouTube. Streaming is retried after the fallback period.

Pulse Weaver no longer polls YouTube's recent subscribers to generate follow alerts. Lumia can continue handling those events independently. Live chat and requested chat moderation remain available; membership and Super Chat messages arriving through the chat connection are still processed without a separate event poll. OAuth scopes, connected accounts, credentials, ports and output routing are unchanged.

The 1,576-unit Google Cloud total is shared by every installation using the same developer project. The graph confirms repeated `streamList` traffic, but it does not identify which individual installations contributed each call or disclose Google's per-call quota charge for `streamList`. This package has been compiled; live quota savings have not been measured after installation.
