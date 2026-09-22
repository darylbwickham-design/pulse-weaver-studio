# Pulse Weaver 1.12.14 — Live 16:9 Camera edits

Moving, resizing, cropping or animating a source in the 16:9 Camera editor now reaches the active programme preview and live output immediately. A Stage change is no longer required.

Pulse Weaver still keeps Camera scene selection preview-only: selecting another scene does not take it live. Once a Stage is activated, however, its programme scene remains a live reference instead of OBS Studio Mode's frozen private snapshot. Destination copies created for source exclusions also reconcile transforms every video frame for Move-style animation.

The amended 1.12.14 build opens its in-app Privacy Policy and Terms of Service links on the public LumiCon site used by the Google OAuth consent screen. Its bundled Privacy Policy includes the clarified Google user-data sharing, transfer and disclosure section.

The exact updater passed payload, layout, language and credential-migration checks. A disposable 1.12.13 → 1.12.14 update and full restore preserved every test file byte-for-byte without changing the installed app or registry. Close Pulse Weaver, run **PulseWeaver-Setup-1.12.14-BETA.exe**, and do not uninstall first.

---

# Pulse Weaver 1.12.13 — Bundled YouTube desktop registration

Fresh installations now include Pulse Weaver's Google Desktop app registration. Users click Connect YouTube and sign in with their own Google account; application credential fields remain hidden. No personal access tokens, refresh tokens, scenes or settings are distributed.

Existing local registrations and account settings are retained. Google's app verification, test-user and account restrictions still apply.

---

# Pulse Weaver 1.12.12 — Simplified YouTube connection screen

The YouTube connection screen no longer displays or edits the application Client ID or Client secret. Connect, disconnect/revoke, output selection and policy links remain available. Saved registrations and account settings are retained.

An installation without a configured YouTube application registration now shows a support message rather than directing users to removed developer fields. This update does not provision a registration for fresh installations. Removing the controls is a user-interface change, not a guarantee that locally stored credentials cannot be extracted.

Includes the Twitch chat repairs from 1.12.10 and the installer log-lock repair from 1.12.11. Full verified backups and rollback remain enabled. Close Pulse Weaver and any previous installer, then run **PulseWeaver-Setup-1.12.12-BETA.exe**. Do not uninstall first.

---

# Pulse Weaver 1.12.11 — Installer log-lock repair

Repairs an in-app update failure where setup could inherit the app's open log handle and keep it locked after Pulse Weaver exited. The native updater disables handle inheritance. Setup also starts a clean, non-inheriting process, so updates launched by older app versions receive the fix.

No settings or logs are skipped. Full verified backups, transactional replacement and rollback remain intact. Existing scenes, transforms, positions, overlays and account settings are preserved. A genuinely active writer still blocks backup safely.

Close the previous failed setup window and Pulse Weaver, then run **PulseWeaver-Setup-1.12.11-BETA.exe**. Do not uninstall first. Includes the Twitch chat repairs from 1.12.10 and bundled Privacy and Terms.

Regression checks cover the native launcher's inherited-log scenario and job shutdown, the installer's clean-process launcher, active-writer refusal, backup integrity and rollback. Packaged upgrade/restore checks use disposable data, not the user's installed app.

---

# Pulse Weaver 1.12.10 — Twitch chat recovery

Twitch chat recovers automatically after dropped connections and missed keepalives. Server-requested handovers use Twitch's supplied address and keep the old connection until the replacement welcomes the client, retaining transferred subscriptions.

Chat readiness now requires a working message subscription. Errors appear beside the chat composer, and other event subscriptions no longer mask a chat failure. Direct Twitch drafts remain until successful delivery; a reply cannot erase a newer draft. Duplicate EventSub notifications are suppressed before actions run.

Access tokens are checked periodically and renewed before expiry. Unauthorized sends and moderation requests renew access and retry once. Temporary network failures retain credentials, and delayed login/refresh replies cannot restore a disconnected account.

The native build, shared chat renderer suite and deterministic tests against the actual Twitch runtime passed. Runtime tests cover reconnect backoff, watchdog expiry, server handover, subscription failures, draft preservation, token renewal, send retry, revocation, deduplication and stale callbacks. They use simulated provider responses; a live Twitch broadcast was not used for validation.

Close Pulse Weaver and run **PulseWeaver-Setup-1.12.10-BETA.exe**. Do not uninstall first. The installer retains full verified backup and rollback support and preserves existing configuration. Privacy and Terms remain bundled. This release updates the Windows private/beta channel.

---

# Pulse Weaver 1.12.9 — Privacy, YouTube controls and relay repair

## Google and YouTube readiness

- Added public Privacy and Terms pages and bundled matching documents with the Windows app.
- Added a clear consent screen before YouTube API access, including links to Pulse Weaver, Google and YouTube terms.
- Reduced YouTube access to the `youtube.force-ssl` scope, protected saved access and refresh tokens with Windows DPAPI, and added in-app revocation and local credential deletion.
- Existing YouTube connections pause on first launch until the current notice is accepted.

## Kick relay retention repair

- The relay now accepts webhook events only while the broadcaster has an active desktop relay session.
- Expired events are deleted before reads, never returned after expiry, and delivered events are removed immediately.

## Safe update

Close Pulse Weaver and run **PulseWeaver-Setup-1.12.9-BETA.exe**, or use Studio → Updates. Do not uninstall first. The installer creates and verifies a full app-and-settings backup before replacing files. Scenes, overlays, transforms, positions, account settings and other existing configuration are preserved byte-for-byte by the upgrade path.

The exact installer passed payload, credential migration, language and layout tests. A disposable 1.12.8 → 1.12.9 update and full rollback preserved all 2,157 runtime/config files byte-for-byte and did not touch the installed app or registry.

---

# Pulse Weaver 1.12.8 — Overlays, alerts and safe recovery

## Overlay Designer and Alert Box v1

- Separate saved drafts and published overlays, with a private preview.
- Alert variations, event matching and queue controls in Action. Show Control remains the live-output interface.
- Repaired publication refresh, legacy custom-code event delivery, sandbox isolation, immediate audio mute and alert audio cleanup.
- Preserves unchanged legacy overlay fields, including fractional positions and durations.
- Fixed managed-source startup migration and an active-browser shutdown crash.

## Update with a verified backup

Close Pulse Weaver and run **PulseWeaver-Setup-1.12.8-BETA.exe**, or use Studio → Updates on the Windows private/beta channel. Do not uninstall first.

The installer verifies a full app-and-settings backup before committing an update. **BACK UP NOW** creates an extra restore point. Backups are stored in the sibling **Pulse Weaver Backups** folder, outside the installation. Replacement is staged and journaled; reopen setup to recover an interrupted operation.

## Rollback and restore

Keep **PulseWeaver-Setup-1.12.7-RECOVERY.exe** as a separate recovery tool. It embeds the original 1.12.7 app payload with the revised maintenance interface.

If you need to return to 1.12.7, close Pulse Weaver, run the recovery installer, choose **RESTORE BACKUP / ROLL BACK**, and select the backup made before updating. Confirm the saved version is 1.12.7. The current state is backed up first, then the matching app files and settings are restored together. Older binaries are not installed over incompatible newer settings.

Backups contain private account data: keep them private and on the same Windows account/machine. External media files outside the installation are not included. Older config-only ZIPs are not full recovery backups.

## Validation and scope

The exact installers passed payload, credential migration, language and layout tests, plus a cross-installer upgrade/restore using a private copied profile with byte-for-byte verification. Native scene/overlay preservation, renderer behavior and recovery failure-injection checks passed. Live provider/device behavior is not exhaustively covered.

This release updates the **Windows private/beta channel**. Public Windows and Mac remain on their existing release lines. Includes corresponding source and SHA-256 checksums. No personal configuration, backups or confidential platform credentials are included.
