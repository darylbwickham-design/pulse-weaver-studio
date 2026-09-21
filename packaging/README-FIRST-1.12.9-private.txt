Pulse Weaver 1.12.9 BETA - YouTube registration and privacy update

Close Pulse Weaver before updating. Setup automatically creates and verifies a
full backup of app files and settings before committing the update. BACK UP NOW
creates an additional restore point. RESTORE BACKUP / ROLL BACK restores the app
version and settings together.

This build adds the Pulse Weaver Privacy Policy and Terms of Service to the
installed docs folder and links their public versions from the app. YouTube
connection now explains the requested access, requires acceptance before OAuth,
uses the YouTube force-SSL scope, protects locally stored tokens with Windows
DPAPI, and provides an in-app disconnect/revoke control.

The hosted Kick relay now queues events only for active desktop sessions, removes
expired events before they can be returned, and deletes delivered events from its
live database. Cloudflare recovery history can retain deleted database states for
the provider's recovery period, as described in the Privacy Policy.

Backups are stored outside the installation in "Pulse Weaver Backups". Keep them
private because they can contain account data. Revoking an account does not edit
old user-controlled backup archives; delete backups you no longer need.

No personal configuration or platform credentials are embedded in this payload.

