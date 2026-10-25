# Setup wizard source, copy and flow audit — 25 October 2026

Historical pre-rebuild source audit. Findings and line references describe baseline `e6c4bc12c7f40970730ae2d4ed833aab54ad7855`, before the wizard helpers were rebuilt. See the [implementation notes](<SETUP-WIZARD-REBUILD-2026-10-25.md>) for current behavior and verification.

Read this source-based audit alongside the [native wizard audit](<SETUP-WIZARD-AUDIT-2026-10-25.md>) and [onboarding research](<SETUP-WIZARD-ONBOARDING-RESEARCH-2026-10-25.md>). This document records source evidence and source-derived review criteria. Native results and research conclusions are reported in those companion documents; source-only qualifications here do not indicate that their work is unfinished.

Scope: read-only inspection of the current wizard, flexible setup, choice and stinger helpers, creation backend and guide. No production files, installed settings or app processes were changed. No show was created. Baseline HEAD is `e6c4bc12c7f40970730ae2d4ed833aab54ad7855`; the existing unrelated settings/frontend/layout-audit WIP was preserved. Native appearance, focus behavior and external link availability are outside this source-only evidence; consult the companion native audit and research document for their coverage and results.

## Actual flow and prerequisites

There are four sequential QWizard pages, with no conditional page IDs or extra Finish branch. The two main branches are inside page 3: recommended layouts or a compatible saved layout for each stage group.

| Page | Controls and choices | What enables progression |
|---|---|---|
| 1 — Prepare your show | Stage/Look/source definitions; Lumia installation, plugin and template-import tasks; three reminder checkboxes; two download buttons and a guide dialog. Also introduces optional game challenges and several other services. | Always complete. None of the reminder boxes is stored or validated, and no connection is tested. [W94], [W113], [W128] |
| 2 — Choose stage groups | Show name plus Starting, Chat, Game, Projects, Celebration and Intermission cards. All except Projects start selected. | Trimmed name 1–90 characters, at least one group, no selected Stage/scene name collision in the saved catalogue or live sources. Suggested names search for a free suffix. [W38], [W132], [W168] |
| 3 — Choose Looks and options | Preview chooser and paired diagram; stage tabs; recommended/saved-layout choice where configured; Look selection; expandable layout controls; separate H/V transition controls; stinger add/edit; previous/next stage buttons. | At least one Look for every selected group; readable, permitted-extension file for selected new stingers. Feed assignments are not required yet. [F409], [F428], [F440], [F506], [F813] |
| 4 — Assign sources and review | One current source question, searchable assignment chooser, required-assignment count, previous/next/skip/attention navigation; existing/new/edit source actions; artwork link entry and placement; shared layer scopes; movement duration; Look review diagrams and summary. | No active OBS outputs, name still valid, required inputs assigned, staged files/URLs valid, overlay scopes valid, no duplicate shared source for an overlapping Look, no shared source already retained by a copied saved layout. [W180], [F573], [F813] |

Back is standard QWizard Back, except that the first page hides it. The code does not register fields or implement cleanup/reset handlers, so the same widgets and in-memory draft choices remain while navigating backwards. Changing groups retains inactive choices and restores the current question by identity where possible. Cancelling destroys this local wizard and its source/stinger drafts; there is no saved/resumable wizard draft. This source review does not establish actual Back/scroll/focus restoration or retry-after-failure behavior; see the companion native audit for rendered and interactive evidence. [W69], [W192], [F762], [F899]

Opening the wizard can first show the existing editor's “Save this look?” prompt if a Look has unsaved edits. That prompt concerns the existing Look, not a new wizard draft. [M1901]

Lumia is required for the supplied Lumia templates/widgets, not for every possible show: a one-feed Chat show can be created without a Lumia artwork assignment. A real camera is not mandatory. Ordinary feeds can use cameras, display/window/game capture, media, images or browser inputs; camera is only the first default source type in the creation dialog. Artwork and shared overlay slots are deliberately browser-only, and native Pulse Weaver artwork is excluded. The browser policy actually accepts ordinary HTTP(S) URLs as well as Lumia/local plugin URLs; it does not establish that a link is a working Lumia service. [F272], [F613], [C41], [B1642]

Both canvases are required by the backend even if the operator currently streams one orientation. That is an implemented dual-format product constraint, not evidence that the user needs two cameras or two copies of every feed. [B1606]

## Default-path interaction load

The source inventory is not a mandatory click count: “Next source needing attention” skips empty optional questions, and Finish can become enabled without visiting every optional question. Empty optional layers create no browser source. [F198], [F596], [F948]

| Default selected group | Default Looks | Required feed assignments | Optional feed assignments | Required H/V artwork assignments | Stage questions |
|---|---|---:|---:|---:|---:|
| Starting | Countdown | 0 | 1 (A) | 2 | 3 |
| Chat | Just chatting | 1 (A) | 0 | 0 | 1 |
| Game | Gameplay | 1 (B) | 1 (A) | 0 | 2 |
| Celebration | Celebration reaction | 1 (A) | 0 | 0 | 1 |
| Intermission | Be right back; Ending | 0 | 2 (A/B) | 4 | 6 |
| Total before shared layers | 6 Looks across 5 groups | 3 | 4 | 6 | 13 |

The wizard always appends three shared layers for each canvas: **6 additional optional questions**. Therefore the fresh default path has **19 questions: 9 required and 10 optional**. It produces 5 Stages and 6 paired Looks if completed. The counts were independently calculated by parsing the actual setup catalogue and applying the initial-selection rules. [W145], [T95], [F512], [F684], [F742], [F762]

Minimal alternatives:

- **Chat → Just chatting:** 1 group, 1 Look, 1 required Feed A assignment + 6 optional shared layers = **7 questions**. No artwork or camera specifically required.
- **Starting → Countdown:** 1 group, 1 Look, 2 required artwork assignments + 1 optional Feed A + 6 optional layers = **9 questions**. No camera required.
- **Game → Gameplay:** 1 group, 1 Look, 1 required Feed B + 1 optional Feed A + 6 optional layers = **8 questions**.

Those minimum paths require manually deselecting the other default groups. The present page does not offer a minimal “just my game/camera” starting choice.

## Prioritized findings

### 1. Progress and repair navigation can disagree with the actual Finish blocker — source-proven, high priority

The progress line says “%1 of %2 required sources are ready.” Its count and attention button consult only `assignmentProblem()`. Duplicate shared-layer reuse and a shared source already retained inside a copied layout are checked later only in `problem(true)`. Once individual inputs are valid, the attention button can be disabled and the required count complete while Create my show is still disabled. The error is in the page-level label rather than a repair path to the relevant two layers. [F150], [F198], [F867]

Reproduce without Finish: complete a minimal Chat feed; select the same existing landscape browser for shared layers 1 and 2, both targeting Every Look. The collision should be reported, but the attention navigation has no target for it. Existing backend tests verify collision rejection, not consistency with this progress/repair control. [TEST1007]

Recommendation: one shared validation result should identify the affected question(s), drive Finish, progress and repair navigation, and distinguish incomplete assignments from cross-layer conflicts. This is a flow issue; changing the word “ready” alone will not fix it.

### 2. Repeated link entry can create unnecessary permanently retained browser sources — source-proven, resource relevance

For a pasted link, `draft()` creates a definition keyed by **question identity** (group/role/Look/canvas), not URL. The backend's reuse cache is keyed by definition identity plus orientation. Consequently, pasting the identical URL into separate same-orientation questions makes separate browser sources. A selected existing source UUID, or the same new-source draft selected again through Use existing, is shared. [F899], [F340], [B1935]

The new non-native browser sources have shutdown disabled and activation refresh disabled so countdowns/widget state survive Look changes. Those settings are intentional and should be preserved; blanket source shutdown would risk the Lumia behavior the user wants. The preferable resource direction is making reuse discoverable and warning about genuinely equivalent repeated definitions. Do not merge merely by provider/host, across orientations, or when independent state is intended. No RAM/CPU saving is numerically claimed without native measurement. [B1984]

The default 6 required artwork questions typically represent 3 genuinely different templates in H/V, so six browser instances may be appropriate. The six optional shared prompts allocate nothing when empty. Unused new-source/stinger drafts are omitted from the creation request. [F905], [F918], [F948]

### 3. “Ready” means assigned, not tested; important audio work is too distant — source-proven

No camera feed, overlay response, plugin connection, media decode, stinger duration or live transition is tested in this wizard. URL checks establish HTTP(S) syntax/import-link exclusion; stinger checks establish readable path and a video extension. Calling required sources “ready” can overstate what was checked. The diagram disclaimer is good and should remain prominent. [F150], [C41], [S22], [F413]

For example, `https://example.invalid/countdown-h` satisfies the URL policy and increments the required-assignment count. Once other assignments are complete, this URL is accepted by both the wizard and creation backend; it is **not** a ready-then-later-rejected URL-policy mismatch. The host is not checked for Lumia ownership or successful response. Invalid syntax, native-overlay links and template-import links are rejected by both layers. [F150], [C41], [B1642]

The page-level error order is separately confusing: its first pass checks every required choice ID before validating URL text. Selecting URL mode gives the current landscape question a nonempty ID even when its text field is empty, so the footer can ask for missing portrait artwork rather than explain the current empty landscape field. This follows the missing-assignment pass at flexible lines 829–836 before URL validation at 848–855. [F813]

Every newly created browser source starts with output audio mixers set to zero. The shared-overlay question explains enabling sound later, but the per-Look artwork/new-browser dialog does not make this consequence equally visible. Someone configuring alerts or a browser with audio can complete all required assignments and discover silent output after creation. The full guide has the right explanation, but it is buried in the Custom Game Challenges paragraph. [F673], [B2030], [G30]

Recommendation: use “assigned” for structural completeness and add a compact final checklist covering preview, one audible overlay owner, microphone/desktop audio, destinations and countdown completion. This does not require activating devices/pages during planning.

### 4. Look selection and Look preview are different interactions, but the distinction is weak — source-proven interaction; visual evidence is separate

Clicking a Look check-card changes inclusion. Its toggle callback does not preview that newly selected Look; `previewLook` is used when layout options change. Preview another Look opens another chooser. Switching stage tabs does select a matching preview. The guide says the diagram follows the “tab or Look you choose,” which is ambiguous about selecting an inclusion card. [F552], [F555], [F754], [G11]

Recommendation: make inclusion and preview separately identifiable. Preserve click-through cards; avoid turning inclusion into a surprise toggle just to inspect a layout. Native review should check what a newly selected alternate card actually shows.

### 5. Feed A/B/C names omit the immediate purpose — source-proven copy gap

Page 4 says, for example, “Game · Feed B,” with a generic paragraph about all source types. The contextual meaning—main gameplay/content for Gameplay, supporting camera/reaction for A—was on a previous page. A/B/C are per-stage slots and can point to the same source in different groups, but the prompt makes the user remember their mapping. [F608], [F675], [T106], [G7]

Recommendation: retain A/B/C as stable identifiers, but lead each prompt with its job and the selected Look(s) that use it. Do not relabel all A feeds “camera”: ordinary feeds deliberately allow other content.

### 6. First-page specialist content obscures the short route — source-proven content density; appearance is covered separately

The first page combines vocabulary, Lumia branding/install, plugin import, H/V artwork imports, optional game challenges, legacy command-bundle advice, and a list of countdown/Chat Physics/Newsroom/alert/stinger topics. Most of that is not required for a one-feed show, while five groups are selected next by default. The checkbox disclaimer is accurate but appears after the preparation tasks. [W94], [W108], [W113], [W127], [W145]

Recommendation: make “Plan now; finish Lumia later” immediate, explain which selected features need Lumia, and move advanced/legacy integration details behind contextual help. This is not a recommendation to remove Lumia or skip required artwork for Looks that need it.

### 7. Several small labels imply actions or options that are not present — source-proven

- **“Paste Lumia link”** only chooses URL mode, shows an empty field and focuses it; it never reads the clipboard. Rename to Enter Lumia browser link… unless the action actually pastes. [F632]
- **“Using your pasted Lumia browser link.”** appears immediately whenever the selected ID is `url`, including when its field is empty. It confirms a completed action that has not happened. Use an empty-entry prompt until text is present, and avoid claiming it was tested. [F359]
- **“No matching choices. You can go back and create a new source or stinger.”** is reused for Look previews, assignment navigation and layer-scope selection too. Creating a source/stinger does not repair all these empty results. Use contextual empty-state guidance. [CH73], [F411], [F576], [F671]
- **Previous stage / Next stage** remain enabled at the first/last visible stage and silently do nothing there. There is no current-stage index beside these buttons, and the wizard has a separate Next button for changing pages. [F565]
- The long default transition card combines “Keep saved transition” and “use Fade for a new layout,” so its effective result depends on a different choice higher up. Display the effective transition in plain terms for the selected branch. [F450]

### 8. Saved-layout assignment volume is larger than selected Looks suggest — source-proven, intent-sensitive

The saved branch inventories inputs by recursively walking both reference scenes, then activates every saved-input question for that group. It does not filter questions against the chosen saved Look targets. Sources retained only as hidden scene items can therefore still require assignment; old native artwork must be replaced even if hidden. The backend intentionally clones the retained graph and rejects hidden native artwork, so simply deleting these questions or treating them as optional would be unsafe. [F694], [F772], [B1779], [C65]

Recommendation: explain that copying a saved layout carries its retained source structure and label questions by affected Looks/retained items. A genuine prune-unused-input feature would require separate backend design and validation.

### 9. Some validation happens later than the local form suggests — source-proven

The new-source dialog checks the entered base name, but backend creation also reserves generated ` · horizontal` / ` · vertical` browser names. A base name can be accepted locally and then fail only on Create my show because a generated name is taken. Existing source existence/compatibility is also checked more fully in the backend than in the per-question progress calculation. [F324], [B1695], [B1710]

Recommendation: validate generated names before closing the source draft dialog and point to the editable assignment on failure. Keep the final backend recheck for races.

### 10. Documentation has one definite unrelated geometry drift and several test-dependent claims

The chat-panel appendix says its minimum width is 270 px; the current frontend uses 280 px. The “plain white” heading claim should be checked against the new tactile theme rather than retained as a historical visual promise. [G54], [UI1995]

The page-1 sentence “Page 3 offers optional stingers” is true but under-describes that page's primary Look-selection job; it is **not** a wrong page-number bug. Both the wizard and guide use a hardcoded 1.4.4 companion-plugin asset under GitHub `releases/latest`; whether that exact redirect/asset works is an external verification item, not established by source reading. [W120], [W129], [G9]

## Six contextual copy examples

These are proposed wording for review, not implemented changes.

| Context | Proposed wording |
|---|---|
| First-page introduction | “Choose the parts of your show, then assign your feeds. Lumia supplies the optional templates, countdowns and widgets. You can plan now and finish Lumia setup later. A feed-only show does not need Lumia artwork.” |
| Definitions | “A **Stage** is a section of your show, such as Starting or Game. A **Look** is a saved arrangement of that Stage's sources, with a layout for each canvas. Switching Looks can animate those sources. A **source** is the reusable camera, capture, media, image or browser input.” |
| Gameplay source question | “Choose the main gameplay or content view for Gameplay (**Feed B**). Reuse a source you already have, or create a capture/media input. The supporting feed is optional.” |
| Link entry | Button: “Enter Lumia browser link…” Help: “Import the template in Lumia first, then copy its browser link using Lumia's chain button. A template import link cannot be displayed as a source.” |
| Progress / conflicts | “9 of 9 required assignments complete. One shared-layer conflict still needs attention.” Repair: “Review conflicting layers.” This requires the validation/navigation correction in finding 1. |
| Final review | “This adds your Stages, Looks and selected new sources. It does not start a broadcast. After creation, check both canvases and transitions. New browser sources have output audio off; enable sound on one intended copy, then check microphone, desktop audio and destinations before going live.” |

Feed-slot clarification: “Feed A, B and C belong to this Stage. Reusing the same source shares its content and source settings; its placement can differ between Looks.” [F675], [F122], [G7]

## Source-derived native review criteria

The following criteria support the separate native audit; coverage and results belong in that companion document, so this list is not an outstanding-work checklist. The source review itself did not invoke Finish or create a show. Relevant native checks include four pages at 1180×720 and a typical larger window/DPI; expanded layout/transition panels; long source names and two-column card text; current versus maximum hidden-tab geometry; scroll-to-error visibility; Back preserving choices, context and focus; selection versus preview; all six optional layers and the minimal feed-only route; duplicate-layer conflict versus attention status; source/stinger dialogs and cancellation; and output-state changes while the modal wizard is open. No claim of current clipping is made from source alone.

Formatting risks are specifically: helper titles wrap against a fixed 220 px text measure rather than available card width; long unbroken source names have no custom elision; page 3's diagram is capped at 210 px although its underlying picture uses 18 px text at 760 px width; and normal QTabWidget page sizing can be influenced by an expanded noncurrent tab. These are source-identified risks, not claims of current clipping; use the companion native audit to distinguish confirmed rendering defects from risks that did not occur. [CH17], [F412], [F414], [F969]

## Evidence links

[W38]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L38
[W69]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L69
[W94]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L94
[W108]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L108
[W113]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L113
[W120]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L120
[W127]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L127
[W128]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L128
[W129]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L129
[W132]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L132
[W145]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L145
[W168]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L168
[W180]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L180
[W192]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-wizard.cpp#L192
[F122]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L122
[F150]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L150
[F198]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L198
[F272]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L272
[F324]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L324
[F340]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L340
[F359]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L359
[F409]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L409
[F411]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L411
[F412]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L412
[F413]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L413
[F414]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L414
[F428]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L428
[F440]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L440
[F450]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L450
[F506]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L506
[F512]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L512
[F552]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L552
[F555]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L555
[F565]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L565
[F573]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L573
[F576]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L576
[F596]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L596
[F608]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L608
[F613]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L613
[F632]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L632
[F671]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L671
[F673]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L673
[F675]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L675
[F684]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L684
[F694]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L694
[F742]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L742
[F754]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L754
[F762]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L762
[F772]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L772
[F813]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L813
[F867]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L867
[F899]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L899
[F905]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L905
[F918]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L918
[F948]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L948
[F969]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp#L969
[CH17]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-choices.hpp#L17
[CH73]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-choices.hpp#L73
[S22]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-stingers.hpp#L22
[C41]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-source-canvas.hpp#L41
[C65]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-source-canvas.hpp#L65
[T95]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-templates.hpp#L95
[T106]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-templates.hpp#L106
[B1606]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L1606
[B1642]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L1642
[B1695]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L1695
[B1710]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L1710
[B1779]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L1779
[B1935]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L1935
[B1984]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L1984
[B2030]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-show-builder.cpp#L2030
[M1901]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/plugins/pulse-weaver-core/pulse-motion-engine.cpp#L1901
[G7]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/docs/SETUP-WIZARD-GUIDE.md#L7
[G9]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/docs/SETUP-WIZARD-GUIDE.md#L9
[G11]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/docs/SETUP-WIZARD-GUIDE.md#L11
[G30]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/docs/SETUP-WIZARD-GUIDE.md#L30
[G54]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/docs/SETUP-WIZARD-GUIDE.md#L54
[UI1995]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/engine/obs-studio/frontend/widgets/OBSBasic_PulseWeaver.cpp#L1995
[TEST1007]: https://github.com/darylbwickham-design/pulse-weaver-studio/blob/e6c4bc12c7f40970730ae2d4ed833aab54ad7855/tests/MotionAudit/main.cpp#L1007
