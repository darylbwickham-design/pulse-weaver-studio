# Pulse Weaver 1.12.6 — Built-in Twitch and Kick connections

Pulse Weaver now includes its registered Twitch and Kick applications:

- Twitch connects with Pulse Weaver's public desktop Client ID and Device Code flow.
- Kick connects with the included public Client ID and PKCE; its confidential client secret is held only by the hosted Pulse Weaver relay.
- Kick token exchange and refresh no longer expose or store the application secret on the streaming PC.
- The Connections page no longer asks users to create or paste Twitch or Kick application credentials.

The Kick relay has also been upgraded to protocol 2 and continues to verify and deliver Kick webhook events for incoming chat.

## Updating

Use Studio → Updates → Check for updates, or run the matching installer manually.

- Private beta: **PulseWeaver-Setup-1.12.6-BETA.exe**.
- Public Preview: **PulseWeaver-Public-Dist-1.12.6-Setup.exe**.

Both installers preserve configuration and contain no confidential application secrets or personal credentials. Corresponding source archives, Lumia 1.1.3 and SHA-256 checksums are included. Mac remains on its separate preview release line.

Validation: native Windows build; hosted Kick relay production build and health check; invalid OAuth request rejection; installer payload and configuration-preservation checks; credential and archive-parity audit. A real Kick sign-in still requires the user to approve access in Kick's browser consent screen.
