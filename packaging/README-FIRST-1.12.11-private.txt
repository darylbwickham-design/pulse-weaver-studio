Pulse Weaver 1.12.11 BETA - Installer log-lock repair

Close any previous setup window and Pulse Weaver, then run this installer.
Do not uninstall first. Setup starts without inherited app file handles so an
in-app update cannot keep the old log locked after Pulse Weaver has closed.
The app's update launcher also disables handle inheritance for future updates.

Full verified app-and-settings backups and rollback remain enabled. Existing
scenes, transforms, positions, overlays and account settings are preserved.
No logs or settings are skipped to work around locks. If another application
really has installation files open, close it before retrying setup.

Includes the Twitch chat recovery fixes from 1.12.10. Privacy and Terms remain
in docs. Backups can contain account data; keep them private.
