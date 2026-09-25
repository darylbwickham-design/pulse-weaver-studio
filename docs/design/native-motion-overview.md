# Native Motion: product and implementation overview

## Goal

Pulse Weaver should own source movement as part of the show rather than require the OBS Move plugin. A streamer creates a clearly named action once and can run that same action from Pulse Weaver, Lumia Stream, LumiCon, Stream Deck, a hotkey, or the local API.

The design keeps existing Pulse Weaver choices: Stages remain the main show model, actions use ordinary language, platform routing stays separate, the app runs directly on libobs, and external controllers receive a limited local operator API.

## User model

The editor uses four ideas:

| Idea | Meaning |
| --- | --- |
| Stage | The part of the show currently on air |
| Layout | Saved positions, crops, sizes, visibility and order for selected sources |
| Action | A named operation such as `Camera close-up` or `Guests side by side` |
| Trigger | A button or automation that asks Pulse Weaver to run an Action |

Users never need to remember slot numbers. Names appear consistently on every controller.

## First preview slice

The `codex/native-motion-engine` preview implements:

- Camera close-up actions with frame-preserving crop and scale compensation.
- Multi-source layout actions captured from the current canvas.
- Stage policies: switch then run, only run on a named Stage, or use the current Stage.
- Animated interpolation, hold, stop, restore, optional return to the previous Stage, and repeat-to-extend for close-ups.
- Manual Stage change ownership: an operator Stage change cancels or restores an active action.
- A shared execution path used by the native UI and local controller API.
- Lumia and LumiCon actions populated from the saved motion catalogue.
- A stable OBS hotkey for every saved action, suitable for Stream Deck's standard Hotkey action.
- Authenticated HTTP endpoints suitable for advanced Stream Deck HTTP actions or a future first-party property inspector.
- Conservative import of Lumia Layout Studio states and absolute OBS Move Source transforms.
- Separate preview installer, portable profile, update channel, port and Lumia plugin identity.

## Editor concept

The Motion studio builds Starting, Intermission, Hangout, Gameplay and Celebration stages from the imported source references. It creates a landscape scene and a portrait scene for each stage, and saves 14 named animated looks. Starting has its own stage; BRB, Ending and Printer Break share Intermission. Hangout supports facecam, printer and screen share layouts. The new landscape scenes use Chatty, while the portrait scenes use vert chatty when that source is present. The imported scenes remain available. YouTube and Kick exclude the spotify and spotifysound sources from their audio mixes; Twitch keeps its existing stream and VOD track settings.

The design canvas edits a private scene copy: drag to move, scroll to resize, choose visibility and layer order, and scrub or play the transition. Each saved look appears in Pulse Weaver, Lumia and LumiCon through the same action catalogue. Hidden sources enter or leave the frame during animation. Before the first live motion, the runner saves an original snapshot for the scene collection; Restore Original Scenes restores its transforms, visibility, layer order and original on-air scene across restarts.

Motion lives in Show Control. The borderless Show / Control heading switches between the operating desk and the editor in place. Compact stage and look rows sit above paired portrait and landscape canvases. Clicking either canvas selects it for editing; its heading turns blue. Secondary commands live in Stage tools and Settings.

The form asks, in order:

1. What should this be called?
2. What should happen?
3. Which Stage owns it?
4. Which scene, group, camera or sources should it control? The user answers by clicking the live canvas.
5. How quickly should it move and how long should it stay?
6. Should it restore or return to the previous Stage?

A close-up uses a draggable crosshair over the subject and a cyan frame shows the resulting crop. Layout mode outlines only the selected source. Dragging, resizing, visibility and layer edits automatically include that source in the saved look. Both canvases are saved in one action, with separate private drafts and undo history. Save look persists the draft; Apply saved look runs it on programme. Adding an existing source creates a hidden scene item after protecting the scene's original snapshot. An overlay command can save consistent full-canvas framing across the collection's looks while its internal content remains externally controlled.

Browser sources receive balanced showing references while the editor is visible, so inactive browser overlays render in the private previews. Restore Original Scenes restores saved framing and hides sources added after the snapshot; added references remain available so saved looks can run again.

The existing-source picker supports searching by any part of the name. Newly added items are explicitly hidden in the stage's other saved looks, preventing a layer introduced for Ending or a printer view from leaking into another look. Changing looks with unsaved layout edits offers Save, Discard or Cancel.

Advanced details belong behind an optional section in the next iteration: easing curve, per-source delay, visibility timing, collision policy and nested groups.

## Execution contract

Every entry point calls the same runner.

```mermaid
flowchart LR
    UI[Pulse Weaver Motion] --> R[Native motion runner]
    L[Lumia / LumiCon] --> A[Authenticated operator API]
    S[Stream Deck] --> A
    H[Hotkey / automation] --> R
    A --> R
    R --> G[Stage guard]
    G --> T[Transform tracks]
    T --> O[libobs scene items]
    R --> E[State and completion events]
    E --> UI
    E --> L
```

An execution owns only the scene items listed by its action. It captures their state immediately before movement, retains stable scene item references during the action, and can restore that state. One action runs at a time in the first preview so ownership is unambiguous.

The safe sequence for an action assigned to another Stage is:

1. Validate the Stage and every controlled source without changing output.
2. Capture the restore state.
3. Ask Pulse Weaver to activate the Stage.
4. Wait for the longest saved Stage transition plus a small readiness margin.
5. Re-capture the animation start state and run the tracks.
6. Hold, restore, or leave the saved layout according to the action.
7. Return to the previous Stage only if the operator has not changed Stage since the action began.

## Conflict rules

- A manual Stage change always wins.
- A close-up triggered again extends its hold instead of stacking another crop.
- A new saved layout can replace an active layout transition from its current position. Other conflicting action types are rejected with an understandable message.
- Stop with restore returns only sources owned by the active execution.
- Restore Last applies only to the most recently completed layout and discards its restore point after use.
- Missing Stage or source references fail before output changes.
- Imported drafts cannot run until the user reviews and saves them.

The later scheduler can allow concurrent actions when their source ownership sets do not overlap. That should be added only with visible conflict resolution in the editor.

## Compatibility import

Compatibility has three separate promises:

1. **Import compatibility:** read useful saved setup from Move and the existing Lumia plugins.
2. **Behaviour compatibility:** reproduce the common visible result with native Pulse Weaver tracks.
3. **Trigger compatibility:** let external controllers run the converted named action.

The preview importer accepts JSON and scans nested structures defensively. It converts Lumia multi-state transforms and absolute Move Source transforms. It reports relative coordinates, filter actions, chained triggers, audio/media operations and exact easing as review items. It never silently interprets unknown fields.

Future import work should read OBS scene collection filter settings directly, show a comparison table, resolve missing sources, and offer `Import`, `Skip`, or `Keep as reference` for each operation.

## Controller surface

Lumia exposes:

- Run motion action, with a dynamic named action list.
- Stop and restore motion.
- Restore the last completed layout.
- Current motion state and current action variables.

The local API exposes the same catalogue, state, run and stop operations. Controller requests may include an idempotency key so retries do not start duplicate executions. Lumia-labelled callers remain restricted to `/api/v1/lumia/*`.

Every saved, reviewed action also registers `Pulse Weaver Motion: <name>` in the OBS hotkey system. A user can assign a key once in Pulse Weaver and select it in Stream Deck without installing another bridge. The hotkey calls the same runner; it cannot bypass Stage or restore rules.

The release Lumia package retains `pulseweavercontrol` and port 18755. The isolated Motion Preview package has its own `pulseweavermotionpreview` ID, result namespace, and default port 18765. Token discovery stays within the selected installation. `packaging/Build-LumiaMotionPreview.ps1` produces the separate package without embedding credentials.

## Storage

Actions are stored as versioned JSON in the portable profile. Each action uses a UUID and records names plus stable OBS item IDs. Names are retained as a recovery fallback and for readable diagnostics.

No configuration, source media, credentials, logs or recovery backups are packaged in the installer.

## Next iterations

1. Extend the current main/portrait transition activity checks to explicit completion signals for every destination route.
2. Extend paired-canvas transition scrubbing with per-source timing controls.
3. Add easing choices and per-source delay with safe defaults.
4. Add a capture/update comparison before overwriting a layout.
5. Add source-set conflict analysis and compatible concurrent actions.
6. Import Move filters directly from scene collections and preserve supported easing/delay fields.
7. Add a first-party Stream Deck property inspector that reads the action catalogue directly.
8. Add action folders, search, duplicate and export/import bundles.
9. Add failure recovery for a source removed during an animation and richer execution history.
10. Promote the preview identity to the release channel only after action migration and uninstall coexistence have been exercised.
