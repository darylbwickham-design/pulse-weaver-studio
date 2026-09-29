# Updates, installer, storage and packaging audit — 29 September 2026

## Scope and boundary

Reviewed the current `codex/unstable` channel packager; GitHub release selection, download and handoff; current regular/alpha/unstable installer; backup, restore and interrupted-transaction recovery; profile adoption; DPAPI registration storage/migration; and the archive contents of the existing `v1.14.0-unstable.10` payload. Also inspected the older workspace WPF source, sample module and Kick relay source as separate components.

This is a source review with offline regression checks, not a claim that every upstream OBS, Qt, .NET, third-party plugin or hosted service has been exhaustively verified. No installation, account, registry, website, release or live platform API was changed by this audit work. Test credentials and installation trees were synthetic.

## Refinements implemented

1. **Installer archive paths now use recovery's strict Windows rules.** The previous installer checked only normalized destination containment. It could accept Windows aliases such as trailing dots/spaces, alternate data streams or duplicate paths even though recovery rejected them. `PayloadPolicy.cs` validates the complete archive before extraction, rejects protected configuration, case/separator aliases, duplicate paths, file/directory conflicts and linked entries, and supplies normalized file names for the installed manifest. Existing clean unstable.10 payload passes this validation.
2. **Backup/work directory roots are normalized.** A caller supplying a trailing directory separator could previously construct `Install\ Backups` inside the app. The same root now always selects the same sibling backup and transaction directories. Recovery journal root comparisons use that normalization too.
3. **Channel packaging checks required runtime dependencies.** Previously it required only the core executable and first-party plugin. The packager now also requires OBS runtime, frontend API, Qt GUI/network, Windows platform/TLS plugins, locale index and the final chat helper. Missing or empty components stop staging.
4. **Channel packaging rejects more private state and linked paths before copying.** Configuration, browser profiles, logs and known profile/service/credential state are rejected. The complete staged payload is checked again after chat helper publishing. This complements the existing desktop OAuth registration allowlist; it does not remove the approved desktop client registration.
5. **Stage exclusions keep source identities.** Existing `excluded` string arrays remain readable; a parallel `excludedIdentities` array records OBS source UUIDs. Legacy names are pinned only when a source resolves. Cached stage reads and route assignment reads resolve UUIDs again after renames. An identified missing source produces a nonmatching runtime marker, so a new source with the old name does not inherit its exclusion; the original UUID is retained for reconnection/reload. Unavailable exclusions are visible and removable in the picker, and explicit checkbox removals remove their identity metadata too.
6. **Stage persistence failures are checked.** Writes validate existing JSON, verify byte counts and commit atomically. An unreadable/malformed prior Stage file is preserved rather than replaced by generated defaults. Explicit save failures report an error and stop follow-up selector/capture changes.

## Existing safeguards verified

- Update selection is separated by installation identity/channel, compares numeric revisions and blocks downgrades and draft/incomplete/foreign assets. Public/private installers are not mixed.
- Downloads require bounded size, exact GitHub asset URL and SHA-256; redirects are restricted to HTTPS GitHub/CDN hosts; installers are streamed to `QSaveFile` and committed only after verification.
- Automatic checks are daily, failed background checks wait an hour, and checking/downloading is guarded. No automatic installation occurs.
- Outputs are checked before offering/handoff and after confirmation; setup refuses a running Pulse Weaver process. Handoff excludes inherited log handles and survives the parent job ending.
- Current installer backs up and verifies app/config bytes, stages replacement separately, journals the same-volume moves and restores interrupted operations. Restore first backs up the state it is replacing. Corrupt archives are rejected before replacement.
- Legacy registration migration matches exact known binary hashes, preserves existing fields, protects secrets using current-user DPAPI, verifies writes and rolls back failure. No migration changes were necessary here.
- Profile adoption checks separate source/destination trees, rejects linked trees, backs up and verifies copied profile bytes, and retains the installed runtime/update identity.
- Existing unstable.10 ZIP contains 2,246 entries, no root `config`/`config-backups` entries and no WPF `PulseWeaver.App` entries.

## Validation

All final checks passed:

- Installer recovery suite: **70 checks**, including full backup/restore, synthetic scenes and Lumia credentials/port preservation, file replacement, move-boundary fault injection, interrupted recovery, locks, corruption rejection, new path/duplicate/link policy and the actual existing payload.
- Packaging policy: **12 checks**, including private-state rejection, required components and actual clean payload acceptance.
- Desktop registration packaging allowlist suite.
- Synthetic private credential migration suite with Windows DPAPI.
- Qt update suites: **2/2 passed** (selection policy and process handoff), plus packaged Qt TLS availability.
- Stage identity/storage suite: **26 checks** covering legacy migration, renames, absent/reused names, restored UUIDs, explicit removal, JSON round trips and failed-write preservation, using fake source resolvers and temporary files.
- Current installer source compiles with **0 warnings/errors**, with payload embedding disabled. This compile artifact is a test only, not a usable installer or release.

Build outputs are confined to `artifacts/audit-updates`. SDK access, DPAPI and Windows job tests required execution outside the restricted sandbox; the final runs used only synthetic fixtures. No API requests were made by these tests.

## Remaining limitations and separate components

- **Uninstall:** the current installer still queues `cmd.exe` removal and reports success before the cleanup result is known; uninstall itself does not first create a verified full backup. Existing recovery backups stay outside the install. A dedicated, verified cleanup workflow is warranted but was not redesigned during this runtime patch.
- **Historical public installer:** `packaging/PulseWeaver.Setup` is the older 1.12.6 public channel and does not have the current private/alpha/unstable recovery transaction implementation. It is not the current channel packager's output. Do not republish it without bringing it up to current recovery guarantees.
- **Older WPF app (`../../src/PulseWeaver.App`):** absent from the current native channel payload. `Services.Save` and `EventRuntime.Secrets.Save` use separate direct writes; the project JSON backup is not a transaction with encrypted token storage. Its managed OBS downloader checks archive shape/size rather than an authenticated expected digest. Its lighting discovery disables certificate verification for the probing client (requests shown are HTTP). These are dormant-source concerns, not evidence of a second YouTube reader in the installed native app. No edits made to this separate app.
- **Older sample module:** the managed sample clock only logs a timestamp when explicitly invoked; no network request loop was found.
- **Hosted Kick relay source (`../../services`):** separate from desktop packaging. Both working copies need a deployment-specific review before changes: `createSession` reads the oldest 1,000 sequences then chooses the last, and `readEvents` deletes delivered events across the broadcaster rather than tracking delivery per session. That can replay an older tail for large queues or let one client consume events another expected. Its OAuth request body limit trusts `Content-Length` before parsing, so a missing/false header bypasses the intended read bound. These findings were reported, not deployed or modified. They do not call YouTube APIs.
- No automatic cleanup of recovery history was added; preserving recovery evidence remains the priority. Disk-space exhaustion can still stop staging, with the existing installation retained.
- Live updating, live rollback, hardware capture and remote service behavior require separate real-world checks. Offline tests do not prove those external behaviors.
- UUIDs cannot be recovered for a legacy name that was already renamed before this code first sees the original source; those older unbound names remain available for manual correction. This persistence change does not claim to remap Stage scene names or transition names, which use separate existing paths.
