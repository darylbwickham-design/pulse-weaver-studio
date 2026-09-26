# Pulse Weaver 1.13.1 alpha 2

Stream Details now includes a searchable Kick category picker beside the existing title control. Choose a category from Kick's results and select **Update Category**. Pulse Weaver sends only its numeric `category_id` to Kick, using the connected user's verified `channel:write` token. An empty HTTP 204 is success; other responses show a sanitised HTTP error. Category searches refresh an expired user token and never use a client-credentials token.

This builds on alpha 1's Kick title and moderation repairs. The streaming, chat, OAuth relay, and Lumia API routes are unchanged. The installer backs up the current installation and retains its profile, connected accounts, scenes, ports, and alpha update channel. No Cloudflare deployment is needed.

The connected Kick account previously granted `channel:write`, so category editing needs no new scope. Moderation still needs Kick reauthorisation to add the missing `moderation:ban` grant. No title, category, or moderation action was sent live while preparing this release.
