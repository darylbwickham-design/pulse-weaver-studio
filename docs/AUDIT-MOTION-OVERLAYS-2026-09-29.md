# Motion, stage output copies and overlays audit

Date: 29 September 2026. Scope: local source refinements in the 1.14 unstable worktree. No release, platform API calls or installed profile changes were made by this audit segment.

## Scope inspected

- Motion engine editor actions, draft capture/restore, source identity resolution, saved source dimensions, live execution/restore, collection lifecycle, and JSON load/save/import/delete.
- Stage output transform reconciliation, exclusion visibility and removal of items from private destination scenes.
- Overlay schema migration, atomic writes, preservation of unknown JSON, draft/published revisions, migration journals, runtime socket limits and shutdown, preview isolation, alert queue ordering and renderer boundaries.
- Existing CameraEditor, RuntimeSafety, OverlayStore, OverlayAlerts and StageExclusions regression implementations.

This is a targeted manual audit of the listed first-party components, supported by executable regressions. It does not certify every upstream libobs/browser implementation or every possible source plugin.

## Corrected findings

1. **Unsupported motion stores could be overwritten.** `load()` left storage writable when JSON parsed successfully but used an unknown version or an invalid actions shape. The next save could replace that document with an empty version-1 catalogue. Unsupported/malformed authoritative shapes now block writes; compatible unknown root fields survive saves.
2. **Import and delete could falsely succeed after a failed write.** Both paths changed the in-memory catalogue without respecting `save()` failure. They now roll back and retain the selected look; import reports zero committed actions on failure.
3. **The design canvas used different resize behavior from live playback.** Applying a saved draft directly deserialized transforms while playback adjusted scales and crops for changed source dimensions. Draft loading now uses the same adjustment, and undo snapshots retain source UUID/dimensions. Flips, crop and displayed size remain consistent.
4. **Renamed grouped inputs could lose their action target.** UUID lookup searched only top-level items, then fell back to the old name. UUID resolution now descends through groups only when the caller allows recursive lookup; direct layout lookups stay within their container.
5. **Collection changes could retain old motion timers and references.** Pending movement, preview/restore state and request-result cache now clear when a collection changes or is cleaned up. Active temporary placement restores before the old collection is saved; cleanup itself only releases state and does not mutate removed sources.
6. **Renaming an excluded source could reveal it in an active output.** Private output synchronization evaluated only its current name. Each active sync now pins excluded source UUIDs, keeping both repeated instances hidden after rename. Persistent exclusion identity handling is coordinated separately in the frontend audit.
7. **Removed original items remained in active private output copies.** The synchronizer now removes the corresponding destination item on the source scene's removal signal. Scene-item references are acquired under enumeration so frame reconciliation and removal do not use a borrowed pointer after its lifetime ends.

## Verification

- `MotionAudit`: **96 checks passed**, compiling the production motion engine against the current libobs/frontend API runtime. Fixtures cover malformed/future JSON, exact original byte preservation, unknown fields, write failure, recursive/direct lookup, rename, resize, crop, flip, mismatched UUID rejection and collection lifecycle.
- `StageExclusions`: passed existing landscape/portrait transform and local frame assertions plus excluded-source rename, duplicate occurrences and removal checks. Uses synthetic color/video inputs and local raw frame capture only.
- `CameraEditor`: passed legacy landscape/portrait layout retention, same-name scene identity, programme isolation, concurrent rendering, duplication, undo and managed-source migration/journal retry coverage.
- `RuntimeSafety`: passed the legacy callback reproduction, 100 protected editor teardowns, HTTP framing/limits and 100 grouped item lifetime cases.
- `OverlayStore` and `OverlayAlerts`: both passed their existing offline suites, including atomic failure handling, draft/published separation, unknown JSON retention, future schema protection and alert queue ordering/pause/skip.
- Build outputs are isolated under `artifacts/audit-motion/`. The motion, CameraEditor, RuntimeSafety and StageExclusions CMake projects accept the current build path rather than relying on a missing historical build directory.
- Full application compilation and the broader regression matrix are coordinated by the root audit task.

### Output-copy lock review

The source removal signal runs while libobs holds the source scene lock. Frame reconciliation enumerates under that same source lock. Both paths acquire the destination scene lock in the same source-to-destination order; there is no new destination-to-source nesting. Item references are acquired during locked enumeration, the ID map remains immutable after construction, and teardown unregisters the video tick before releasing the scene references. No new inverse lock order was found. This relies on the normal application model in which private output copies are not independently edited by the user; this is not a general-purpose synchronization primitive for arbitrary plugin mutations.

## Preserved behavior and limits

- No change to motion action IDs, external trigger names, Lumia binding contracts, saved timing, easing, routing, overlay capability URLs or published overlay revisions.
- Source additions to an already-created destination scene copy still require that route/stage to be recreated. This pre-existing limitation needs a separate membership synchronization design; this patch only propagates removals and does not add a mutable cross-thread map.
- Scene/group container selection still uses stored names in parts of the motion document/editor. Input-source rename is covered here; arbitrary renaming of container scenes across all saved stage/action references needs coordinated frontend migration and is not claimed fixed.
- Overlay renderer browser behavior, third-party content and actual hardware capture need interactive validation. The atomic store/alert queue design appeared consistent on review; no speculative changes were made there.
- Tests use synthetic input fixtures and preserve installed settings. Live multistream verification remains separate.
