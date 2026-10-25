# Setup wizard rebuild · 25 October 2026

Historical description of unstable 4's five-phase builder. The subsequent task-first revision is documented in [current setup instructions](SETUP-WIZARD-GUIDE.md), [academic research](SETUP-WIZARD-TASK-FIRST-RESEARCH-2026-10-25.md), [stream workflow research](SETUP-WIZARD-STREAM-WORKFLOWS-2026-10-25.md) and [implementation/verification notes](SETUP-WIZARD-TASK-FIRST-IMPLEMENTATION-2026-10-25.md).

The setup wizard has been rebuilt around five named phases: **Start → Sections → Looks → Sources → Review**. The design uses focused click-through choices and the main application's rounded, raised control style. The earlier audit describes the pre-rebuild baseline; this document describes the implementation.

## What changed

| Area | New behavior |
|---|---|
| Starting point | Full show, Game and content, or Conversation choose useful initial sections. The selection remains customizable and clearly becomes Custom when edited. A static illustration explains the two canvases. |
| Preparation | Lumia instructions are contextual, with installation, companion-plugin and artwork/command help behind a dedicated action. There are no pretend connection checkboxes. A feed-only route does not need Lumia artwork. |
| Section selection | Responsive cards keep the title, description and selected indicator inside one surface. The show name is checked against existing Stage/scene names. |
| Look selection | Preview and Include are separate actions. Selected and unselected Looks open a focused static preview, including saved Looks. The selection page no longer reserves a large preview above the cards. Its grid switches to one column when two panels cannot fit. Named previous/next Stage actions have proper boundaries. |
| Look configuration | A focused editor keeps the placement preview beside composition, fit/crop, inset position and portrait proportion. Simple one-feed Looks use a smaller initial dialog. Closing returns to the same draft; options no longer expand down half of the selection grid. |
| Transitions | A separate focused editor uses landscape/portrait tabs while preserving independent Fade, Cut and stinger policies. It shows the effective default. New stingers remain local-file drafts until creation. |
| Source questions | Prompts lead with the feed's purpose and retain A/B/C identifiers. New source types have contextual defaults, a compact current-type summary and click-through type/device/window selectors. |
| Browser entry | Enter browser link describes the actual action. Empty and entered states are distinct; assignment never claims a successful connection test. Creating a reusable Browser draft can prefill an entered link. |
| Reuse | Existing and newly drafted sources remain selectable. Help explains shared content/settings and warns that independently entered links create independent browser definitions. There is no automatic merging by host or URL. |
| Optional layers | Shared layers are off initially. Enabling them reveals three optional layers per canvas. Disabling them preserves in-session answers while excluding the layers from validation, diagrams and creation. |
| Errors and repair | A shared issue model drives page completion, local errors, the overall issue count and attention navigation. Overlapping shared-layer conflicts identify the affected assignments. Generated browser source names are checked before accepting a new draft. |
| Final review | A dashboard of Stage cards opens focused reviews of actual assignments, reuse and transition settings. Dialog height adapts to assignment count. Edit assignment returns to the exact source; Return to review stops at any newly incomplete step. |
| Commitment and completion | Create checks names, assignments and inactive outputs again. Completion offers four click-through topics for picture/movement, audio, Lumia commands and destinations/rehearsal. Structural creation is distinguished from actual local/live testing. |
| Keyboard and layout | Native checkable button state, keyboard activation, visible focus, responsive wrapping and contextual empty/search results replace fixed-width wrapping and detached descriptions. |

The standard Full show route retains five Stages and six paired Looks. With shared layers deferred, it has **13 active assignment questions: 9 required and 4 optional**. Enabling layers restores six optional questions. Conversation starts with one Chat Look and one required feed assignment; Game starts with one required content feed and one optional supporting feed. These inventories are not mandatory click counts.

## Preserved contracts

The transactional native creation backend and its draft schema are unchanged. The rebuilt UI retains dual canvases, existing saved-layout graph constraints, native movement, separate Stage transitions, stingers, shared-source identity and Lumia browser compatibility. No encoder, output-routing, source-shutdown or scene-activation-refresh policy was changed by the wizard work.

New browser sources still start with output audio off. Existing source audio/monitoring properties are preserved. The review and completion guidance now make the remaining audio check visible; the wizard does not enable multiple copies automatically.

Planning uses static illustrations and schematic diagrams. It adds no live camera/browser/video previews, autoplay or idle polling timer. CPU/RAM savings have not been numerically measured or claimed. Back preserves the current in-memory draft; closing/reopening is not a resumable draft feature.

The existing companion plugin URL was checked against the public latest stable release metadata and a successful redirected HEAD request. It still delivers `PulseWeaver-Lumia-1.4.4.lumiaplugin`; the existing compatibility path is retained. No account connection or Lumia command was exercised by that link check.

## Verification

The first native application build passed. A separate Qt control check compiled successfully and passed **40 focused checks** covering responsive wrapping, long unbroken names, column reflow, native checkable state, keyboard activation, accepted/vetoed model synchronization, model updates, enabled state, contextual empty results, search Enter behavior, screen bounds and wrapped-row height after reparenting. A pixel comparison confirmed an ordinary main button was unchanged by the added scoped styles. Those isolated control captures are not a substitute for the real frontend theme walkthrough.

Checks caught and corrected several implementation mistakes before handoff:

- Blocking the hidden section models' signals during preset changes also blocked their visible card mirrors. Presets now suppress only the expensive overall refresh while allowing view/model synchronization, then refresh once.
- Long nonwrapping checkbox text and navigation rows forced scroll contents wider than the window. Concise control labels, wrapped explanations and separate navigation rows retain readable text without horizontal overflow.
- Aligning the native card grid to the top capped wrapped rows at their width-independent size hint. A top-packed outer layout with a trailing stretch now lets the unaligned grid use the actual height for its width. An exact negative control failed with the old helper and passed with the corrected one; no tolerance was relaxed.
- A fixed two-column Look grid still exceeded the smallest viewport. The responsive grid now considers the actual minimum width of each compound panel, including its native checkbox and action buttons, before choosing its column count.
- Reparenting a visible utility button during grid insertion could hide it, and a flexible grid could absorb surplus vertical space above Review. The helper now establishes ownership before showing each widget, requests its natural height and maintains a guarded minimum derived from actual-width wrapping. Separate probes verified all four actions at three widths and rejected a candidate that collapsed the grid to zero height.
- Qt had already cached the ordinary style for early-created primary buttons. Explicit repolishing after applying the setup role gives Next, Create and draft-commit actions their intended cyan hierarchy. The native visual check, rather than the presence of a property alone, exposed the problem.

The final complete MotionAudit regression run passed **2,143 checks** with no unexpected OBS errors or installed-settings access. It exercises actual production wizard navigation, visible preset synchronization, Preview without inclusion, source error/repair, optional-layer deferral and restoration, Review edits/return, independent transition tabs and cancellation preserving the synthetic OBS graph and storage. Review actions must be visible and reachable at all three tested sizes. The native core build also passed; before/after source hashes confirmed that the build inputs stayed unchanged throughout compilation.

Strict layout checks passed at **1100 × 800**, **780 × 620** and **740 × 560**, covering all five pages plus included/excluded Look Preview, Configure, Stage transitions and focused Review. They require readable card height and horizontally contained scroll contents; vertical off-scroll content is recorded separately. Minimal-platform captures use placeholder glyphs and platform colors, so these are geometry/state checks, not evidence of the final typography or branding. The actual frontend theme is reviewed separately in the isolated build.

The actual Windows frontend was also walked through in a separate portable instance with fresh local settings. The Conversation route was customized, an excluded Look was previewed without becoming included, Configure and the stinger draft dialog were opened, optional layers were enabled and deferred again, and a Review assignment was edited and revisited. **Create my show** then successfully created `Wizard UI check · Chat`, one paired Just chatting Look and one bundled-logo image source. The logo appeared on both canvases. The completion screen and its audio guidance were inspected.

After a normal close, a file-level check confirmed exactly one image input, no browser/capture/audio inputs, no service files and no output-start markers in that test instance. The existing installed application remained running separately. This walkthrough did not test live streaming, real stinger playback or Lumia audio; those underlying policies were retained and covered structurally by regression checks. The test instance and its logo-only show are retained for review.

A final Windows frontend pass after the polish build confirmed the cyan primary buttons, both transition-format tabs, source reuse and compact Review utility spacing. The second draft was cancelled. A separate readable Windows Qt rendering pass passed **188 checks**, including primary foreground styling and visible Review controls at all three sizes. Those captures use a combined resolved base theme and scoped wizard rules; the actual frontend walkthrough remains the appearance evidence.

## Related material

- [Current user guide](<SETUP-WIZARD-GUIDE.md>)
- [Onboarding research and limits](<SETUP-WIZARD-ONBOARDING-RESEARCH-2026-10-25.md>)
- [Pre-rebuild audit](<SETUP-WIZARD-AUDIT-2026-10-25.md>)
