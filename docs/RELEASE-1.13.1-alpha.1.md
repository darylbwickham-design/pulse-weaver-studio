# Pulse Weaver 1.13.1 alpha 1

This alpha repairs Kick title updates and moderation. The Stream Details screen now sends title-only `PATCH /public/v1/channels` requests and accepts Kick's empty HTTP 204 response. Kick moderation verifies the connected user token's active grant, app identity, scopes, and expiry before sending bans, timeouts, unbans, or message deletion. A missing grant is shown with a Reauthorise Kick action. Refresh token rotation is saved, and failed requests show sanitised HTTP errors instead of apparent success.

The account used in the isolated preview was verified against Kick: its user token is active, `channel:write` and `moderation:chat_message:manage` are granted, and `moderation:ban` is missing. Reauthorise Kick after installing this alpha to request that permission. No viewer was moderated during verification. The title PATCH was not sent live because the current title is empty and there was no value to preserve.

The app continues to use the existing Kick developer app and Cloudflare relay for OAuth and events. Title and moderation requests go directly from the desktop client to Kick, so this update needs no relay deployment. Streaming and chat routes are unchanged.

This installer also includes a verified profile-adoption command for copying an isolated local configuration into the installed alpha while retaining the installed app's update channel. It creates a full recovery backup and checks copied files before replacement. Run it only after closing all Pulse Weaver windows; the original isolated configuration remains untouched.
