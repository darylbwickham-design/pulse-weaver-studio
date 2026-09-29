# Pulse Weaver 1.14 source audit and local refinements

## Scope

Reviewed the current native 1.14 branch across the product shell, destination startup/stop, YouTube transport and quota handling, Twitch/Kick account and chat lifecycles, audio and video settings, stage routing, motion editor/playback, overlays, local automation, update selection and installer recovery. Also inspected the dormant WPF app and separate hosted Kick relay source. The deployed native payload does not contain that older WPF app.

The work combines source review, independent review of important changes, production-code fixtures and local startup checks. It is not a claim that every line of upstream OBS, Qt, Chromium or every third-party source plugin has been verified, or that live streaming cannot fail.

## Refinements made

### Requests, accounts and output control

- Preserved the preceding local YouTube fix: one persistent transport per distinct active chat, saved continuation tokens, cancellation when outputs stop, reusable ingest streams, bounded authentication retries, and a visible Save quota/Responsive choice. YouTube follow/like/subscriber polling stays disabled.
- Fixed overlapping Twitch device-token requests and stale account callbacks that could restore a disconnected account, overwrite a replacement login or change stream credentials after an output started.
- Coalesced Kick refresh/channel requests, bounded relay retry behavior, discarded stale relay/account completions and prevented a retired output's callbacks from changing its replacement's status. Failed chat sends retain the draft.
- Coalesced destination changes within an event-loop turn; rejected YouTube format changes while its existing output is active and kept both selectors synchronized. Rapid Off/On changes are covered by the extracted production callback tests.
- Settings now check all native output objects, including independent YouTube/Kick and portrait recordings. The former primary-only guard could allow disruptive audio device/routing changes during those outputs. Update handoff also respects pending Twitch startup.
- Protected profile-dependent audio routing before startup has loaded a profile. Bandwidth save errors are now reported accurately.

### Broadcast preservation

- A leftover YouTube broadcast ID is now cleaned up according to its actual lifecycle. Previously the next preparation could unconditionally delete an older replay after a failed stop/cleanup.
- Only explicitly unused `created`/`ready` broadcasts may be deleted. Live/testing states are completed, completed/revoked broadcasts are retained, and unknown/malformed states are preserved with an error. An explicit empty successful lookup is treated as already absent; authentication/network/quota failures are not.
- Corrected preparation text to say **unlisted**, matching the actual privacy setting.

The lifecycle decisions follow Google's [broadcast resource documentation](https://developers.google.com/youtube/v3/live/docs/liveBroadcasts) and [transition API](https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/transition).

### Looks, stages and storage

- Stage completion waits for both landscape and portrait transitions; portrait completion no longer overwrites the landscape activity flag.
- Saved-look editor transforms use the same source-resize adjustment as live playback. Grouped input UUID lookup survives input renames.
- Collection changes stop motion timers and release old scene references.
- Unsupported/malformed motion stores are protected; compatible unknown fields survive writes. Failed import/delete writes roll back their in-memory changes.
- Stage exclusions retain source UUIDs across renames, reloads and source absence. A different source reusing an old name does not inherit that source's identity. Explicit checkbox removals remain removed.
- Active private destination copies keep renamed excluded sources hidden and remove items deleted from the original scene. Item references are retained during concurrent access.
- Stage saves now check complete writes and atomic commit, preserve malformed existing files and report failures without repeatedly opening the same warning on periodic refresh.

### Integrations and delivery tooling

- Fixed an asynchronous Lights callback that could access a deleted device row; credential-bearing URLs are not shown in its status text.
- Lumia plugin source cancels queued work and ignores late results after unload/settings reload. Its tests cover those lifecycle changes.
- Hardened installer archive paths, duplicate/link detection, protected configuration and recovery root normalization. Packaging validates required runtime components and rejects private state before and after staging.
- Corrected README/BUILD descriptions for the 1.14 development branch; no release version was incremented.

## Verification

All tests below use synthetic data, fake platform responses or isolated local runtime fixtures. No test starts a real broadcast, posts a chat message or modifies an account.

| Area | Evidence |
| --- | --- |
| Native app/plugin | Both final `RelWithDebInfo` targets build successfully; logs `artifacts/build-source-audit-final.log` |
| YouTube | 51 lifecycle checks including 10 broadcast cleanup classifications; quota, transport, registration and broadcast-isolation suites |
| Platform/control | Twitch 25, Kick 28, destination selector 10, Lumia 16 checks |
| Motion | 96 production-engine checks |
| Stage persistence | 26 identity, absence/reappearance and atomic-write checks |
| Runtime/overlays | CameraEditor, RuntimeSafety, StageExclusions, OverlayStore and OverlayAlerts passed |
| Audio/transitions | 1,500 concurrent source retirements, 5 audio shutdown cycles and 2,100 transition reentries across 7 routes |
| Output safeguards | Six independent/native route fixtures confirm active outputs block disruptive settings |
| Presentation | ChatRenderer, VisualFeedback, OutputUptime, TwitchStartup; 231 SVG assets at 1x/2x and three theme variants |
| Installer/update | 70 recovery checks, 12 packaging checks, 2 updater suites, synthetic DPAPI migration, registration allowlist and TLS checks; installer source builds without a release payload |

The first root runner invocation had a Windows argument-splatting error and selected an unavailable Qt `offscreen` plugin. The runner was corrected to pass argument arrays and use the bundled `minimal` plugin; the affected suites were rerun successfully. A new output fixture also needed a local video context and the correct libobs data path. These were fixture issues, not passing application results.

Detailed reports:

- [Requests and quota investigation](PULSE-WEAVER-REQUEST-AUDIT-2026-09-29.md)
- [Motion and overlays](AUDIT-MOTION-OVERLAYS-2026-09-29.md)
- [Platforms and control](AUDIT-PLATFORM-CONTROL-2026-09-29.md)
- [Updates, storage and separate components](AUDIT-UPDATES-STORAGE-2026-09-29.md)

## Important remaining findings

1. New source additions to an already-created private destination scene copy still require route/stage recreation. Removal is now synchronized; automatic additions require a broader membership design.
2. Some motion scene/group container references still use names. Input-source rename is covered; arbitrary container renaming across every stored action is not claimed fixed.
3. Uninstall still reports success before queued directory cleanup finishes. The historical public installer has weaker recovery guarantees than the current channel installer.
4. The dormant WPF app and hosted Kick relay have separate storage/queue/body-limit findings recorded in the delivery report. They were not deployed or rewritten as part of this local native patch.
5. Live platform behavior, long broadcasts, hardware encoders, third-party capture plugins and all UI workflows still need real-world validation. The independent Google-client probe showed the server also ends quiet chat RPCs after about ten seconds; this patch does not claim zero-cost chat or that all server reconnects were client duplication.

## Local delivery boundary

The regular local `Pulse Weaver` installation receives the rebuilt frontend and first-party plugin. Its already-fixed YouTube helper is retained. A complete configuration backup and both previous binaries are verified before replacement; the installation receipt and recovery script live in `artifacts/local-source-audit/`.

The separate `Pulse Weaver 1.14 Clean Test` installation is unchanged. Installer/packager and Lumia JavaScript refinements remain in source; the installed external Lumia plugin is unchanged. No release, tag, push, updater manifest or website publication is performed.

### Installed and checked

Installed locally on 29 September 2026 at 23:26 BST. Verified startup at 23:29 BST:

- App and plugin hashes match the final build; the earlier YouTube helper hash is unchanged.
- All 1,554 configuration files were identical throughout replacement. The 20 scene/profile files remain byte-identical after startup.
- App responds, local Lumia API remains on port 18765, streaming and recording are idle, and there are zero YouTube request log entries during startup/idle verification.
- Show, Control and Settings rendered successfully. Control exposes Edit selected look, New look and Duplicate look. No look was saved, source edited or output started during the UI smoke check.
- The final root regression matrix is 13/13 passing, in addition to the agent-owned suites listed above.
- Recovery folder: `artifacts/local-source-audit/recovery-20260929-232604`. Its verified binary restore script retains current settings. Its separate `config` backup is available for deliberate recovery.
