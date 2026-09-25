# Native Motion checkpoint — 25 September 2026

Work resumed in the isolated `native-motion-engine` preview. The programme is at `PW Starting · Starting`; no stream or recording was started. The installed Pulse Weaver release was not changed.

The current show has five Stages and 14 saved looks. The Control view uses large landscape and portrait canvases, Stage and Look selectors, a Layers rail, and contextual source controls. Corner dragging supports proportional resize, Shift stretch, and Alt crop. Full-canvas artwork and event overlays switch together at the midpoint; cameras continue moving. Layer-order corrections are in the active portable profile.

The final cue configuration was copied from the portable profile to `artifacts/completed-show/pulseweaver-motion-actions.json` and `artifacts/profile-backups/night-checkpoint-20260925/`. The latter also holds the previous completed-show cue file, Stage snapshot, and show guide. The active cue file and completed-show snapshot have identical SHA-256 `2068F6301F277F3E07190EE363BA08BE1D194F78721A0011B3ABC38D1FA516FB`. The separate Lumia package is `artifacts/lumia-motion-preview/PulseWeaver-Motion-Preview-Lumia-1.3.1.lumiaplugin` with manifest ID `pulseweavermotionpreview`.

Native build succeeded. The preview reopened without a safe-mode prompt. Thank you → Raid welcome and BRB → Ending were scrubbed at 30% and 70% on both canvases; neither showed overlapping artwork. Raid welcome and Starting were applied successfully. The desktop control tool could not hold Alt or Shift while dragging, so those gestures still need a hands-on check. An in-Lumia trigger was not part of this rehearsal.

On resume, clicking the Starting pixel-board camera inset selected the full-canvas Lumia overlay. Canvas picking now chooses the smallest visible source under the pointer, and a five-pixel move/resize threshold avoids dirtying a look when clicking near a handle. The revised preview build visually selected `second facecam` with a clean Save look state. A previous accidental save was discovered and replaced with the exact pre-edit cue file before relaunch. The 1.3.1 Lumia package gives Restore Last Motion Layout its own `/motion/restore` route and rejects restore while a motion is active. Lumia has the separate Motion Preview plugin active at 1.3.0; its update to 1.3.1 awaits action-time confirmation required by the Computer Use skill.

Continue from the show guide in `artifacts/completed-show/SHOW-GUIDE.md`. Keep work confined to this task's isolated preview and preserve the protected original scenes.
