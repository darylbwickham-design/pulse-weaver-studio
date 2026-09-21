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
