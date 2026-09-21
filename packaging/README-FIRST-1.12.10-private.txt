Pulse Weaver 1.12.10 BETA - Twitch chat recovery

Close Pulse Weaver and run setup to update. Do not uninstall first.
Setup verifies a full app-and-settings backup before replacing files. Existing
scenes, overlays, transforms, positions and account settings are preserved.
Use RESTORE BACKUP / ROLL BACK to restore a matching app and settings backup.

Twitch chat now recovers from connection drops and missed keepalives, follows
Twitch's server handover protocol, and shows readiness only after its chat
subscription succeeds. Tokens are checked periodically and renewed before expiry;
unauthorized sends and moderation requests refresh access and retry once.
Failed direct Twitch sends retain the draft, and errors appear beside chat.
Revoked access and delayed replies after disconnect are handled explicitly.
Repeated EventSub notifications do not run actions twice within the recent-event
window. Handover and failure cases are covered by isolated runtime tests.

Privacy and Terms remain bundled in docs. Backups can contain account data;
keep them private. This payload contains no personal configuration or credentials.
