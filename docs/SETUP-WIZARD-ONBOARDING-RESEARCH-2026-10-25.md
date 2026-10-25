# Setup wizard: onboarding research and design recommendations

Research retrieved **25 October 2026**. This document informs the companion [setup wizard audit](<SETUP-WIZARD-AUDIT-2026-10-25.md>). It records external evidence and proposed product decisions; it does not report a Pulse Weaver user study or certify the wizard's behavior.

The [current setup guide](<SETUP-WIZARD-GUIDE.md>) documents **Build my show…** and defines a **Stage** as a part of a show and a **Look** as a layout within a Stage. Those product definitions must remain consistent across setup, Show Control and editing. The [older theme-builder guide](<BUILD-MY-SHOW-GUIDE.md>) is historical. The [2.0 design research and parity contract](<PULSEWEAVER-2-DESIGN-RESEARCH.md>) supplies the broader native UI context.

## Scope and interpretation

The user's design preference is **click through instead of long lists**. Their operational priorities are low CPU/RAM, working multistream, preserved stingers and unchanged Lumia compatibility. These are product requirements, not findings derived from a generic design pattern.

The sources below support focused choices, recognition, useful feedback and recoverable work. They do not establish that cards always outperform lists or dropdowns, that fewer clicks always improve usability, or that one page count suits every setup. Recommendations marked **Pulse application** are our judgment and require source inspection and task-based validation. No source, configuration or application behavior was changed by this research.

## Eight annotated primary sources

### S1. MIT: Learnability

[Lecture 3: Learnability](https://designftw.mit.edu/lectures/learnability/index.html) — university HCI teaching. Relevant sections include *Learning by Doing*, *Recognition vs. Recall*, *Conceptual Models* and consistency in wording.

**Evidence:** Learning involves discovering actions, understanding their effects and remembering them. Visible cues reduce reliance on recall; the interface should communicate a model suited to the user's task. The lecture explicitly treats design guidance as conditional.

**Pulse application:** Show recognizable Stage/Look examples and explain the effect of choosing them. A technical identifier with a new label is insufficient. Keep terminology consistent with the setup guide and later controls.

**Limit:** This is teaching synthesis, not an experiment showing that cards beat lists. Its selection example successfully uses a conventional list with a preview.

### S2. Grossman, Fitzmaurice and Attar: software learnability

[A Survey of Software Learnability: Metrics, Methodologies and Guidelines](https://www.research.autodesk.com/app/uploads/2023/03/a-survey-of-software.pdf_recqWPTEYd6nsP0r8.pdf), CHI 2009, [DOI: 10.1145/1518701.1518803](https://doi.org/10.1145/1518701.1518803) — primary research hosted by the authors' institution. Relevant printed pages: 653–657.

**Evidence:** The paper distinguishes initial and extended learning. Its obstacle categories include task flow, awareness, locating functionality and understanding. A study of ten architecture students using AutoCAD compared think-aloud with expert question/suggestion coaching; coaching exposed more learnability issues.

**Pulse application:** Evaluate first-run completion and later revision/recovery separately. Record requests for clarification. Distinguish independent task completion from coach-assisted exploration.

**Limit:** One application and a small sample with domain experience. The authors warn that coaching changes natural usage and cannot establish independent recovery. The study supplies no expected improvement percentage or proof for a particular wizard layout.

### S3. Apple: Onboarding

[Human Interface Guidelines: Onboarding](https://developer.apple.com/design/human-interface-guidelines/onboarding) — official product design guidance.

**Evidence:** Apple favors interactive teaching, contextual tips, optional and revisitable tutorials, and postponing nonessential setup while providing usable defaults.

**Pulse application:** Help the user perform a meaningful operation in a safe local context. Keep optional customization outside the minimum useful route, with a visible way to return. Put short explanations near the relevant choice.

**Limit:** Transfer the principle, not Apple's platform appearance. The canonical page returned a JavaScript shell to the browser tool; official indexed content was retrieved through its [changes URL](https://developer.apple.com/design/human-interface-guidelines/onboarding?changes=_7). This is not verification of every current platform-specific detail.

### S4. GOV.UK: Question pages

[Question pages](https://design-system.service.gov.uk/patterns/question-pages/) — official service design pattern.

**Evidence:** Know why each answer is needed; start with focused questions; carry answers forward and provide Back. The guidance advises testing without a progress indicator first, cautions against a large clickable all-questions indicator, and requires an alternative to drag-only range answers.

**Pulse application:** Use a relevant category choice followed by a focused detail page. Explain recommended defaults and optional work. A slider needs keyboard or direct-value operation as well as dragging.

**Limit:** A public-service web form is not a streaming desktop application. Do not mechanically copy its layout, alignment or page count.

### S5. GOV.UK: Check answers

[Check answers](https://design-system.service.gov.uk/patterns/check-answers/) — official review and correction pattern.

**Evidence:** Review can help users notice mistakes and see what has been captured. Specific Change actions return to populated answers, then back to review without replaying the entire journey. The final commitment should be explicit.

**Pulse application:** Review consequential show, output and audio changes in compact groups, with a direct edit action for each. Use a final button that describes what it applies.

**Limit:** A summary is useful when it reveals consequential or otherwise hidden changes. This does not justify repeating every trivial choice.

### S6. W3C WAI: Multi-page Forms

[Multi-page Forms](https://www.w3.org/WAI/tutorials/forms/multi-page/) — official accessibility tutorial.

**Evidence:** Divide complex entry into logical groups, identify optional stages and progress, and preserve entered data when users revisit completed steps.

**Pulse application:** Stable named phases can orient users without displaying every question. Mark optional work as skipped or deferred rather than silently complete. Revisiting a step must retain the draft.

**Limit:** This is web accessibility guidance, not native Qt conformance certification. Its illustrated step indicator is an example, not a requirement for a large sidebar. Reconcile orientation with S4's caution about elaborate progress navigation.

### S7. W3C WAI: User Notification

[User Notification](https://www.w3.org/WAI/tutorials/forms/notifications/) — official accessible feedback tutorial.

**Evidence:** Combine clear overall feedback with relevant field-level messages. Explain corrections, associate errors with controls and expose dynamic changes to assistive technology. Validation timing should suit the input.

**Pulse application:** Identify the affected destination or source and offer a nearby corrective action while retaining other work. Use text and suitable native accessibility behavior, not a colored badge alone.

**Limit:** HTML/ARIA techniques require native equivalents and testing. Copying their appearance does not establish keyboard or assistive-technology accessibility.

### S8. Microsoft: Wizards

[Wizards, Win32 UX Guide](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-wizards) — official legacy desktop guidance.

**Evidence:** A wizard should serve one coherent task, distinguish navigation from commitment, preserve selections through Back/forward, and use functional pages with meaningful action labels. It rejects arbitrary length targets and pages with no useful decision.

**Pulse application:** Separate choosing and configuring from applying or starting anything. Keep navigation and geometry predictable; avoid forced tours or unrelated settings simply to create a wizard.

**Limit:** Microsoft explicitly identifies this as Windows 7-era guidance. Use its process reasoning, not obsolete chrome, glyph-only Back recommendations or numeric branching limits.

## Recommended direction for Pulse Weaver

These are hypotheses for the audit and subsequent design work, not descriptions of implemented behavior.

### Make the user's outcome the organizing principle

The minimum route should produce a useful show the user can inspect privately. Reuse existing work where appropriate. Optional integrations and detailed customization should not all precede that first result. A functional first page is preferable to an introduction that only adds a Continue click. ([S3](#s3-apple-onboarding), [S4](#s4-govuk-question-pages), [S8](#s8-microsoft-wizards))

Use a category choice, then relevant details with an obvious Back control. For Stage/Look selection, show an example or lightweight thumbnail, a short purpose and the effect of the choice. Avoid an unfiltered inventory of technical identifiers. Verify that examples match the actual Stage/Look relationship and supported behavior. ([S1](#s1-mit-learnability))

### Reveal detail when it becomes relevant

Advanced encoder, routing or automation controls can sit behind a clearly labeled detail action. The active default, prerequisites and consequential effects must remain visible. Do not replace a long list with an equally long sequence of empty pages, or use an arbitrary maximum such as “always three choices.” ([S3](#s3-apple-onboarding), [S4](#s4-govuk-question-pages), [S8](#s8-microsoft-wizards))

### Preserve work through navigation and review

Retain choices through Back and revisit. A review edit should return directly to review. A resumable draft and repeat-safe application are appropriate engineering goals: reopening or applying an unchanged draft should not duplicate scenes, sources or accounts. These behaviors need verification; the literature does not guarantee them. ([S5](#s5-govuk-check-answers), [S6](#s6-w3c-wai-multi-page-forms), [S8](#s8-microsoft-wizards))

Review the intended additions, replacements and retained settings. Provide focused Change actions, and distinguish **Apply setup** from any later **Go live** operation. Existing multistream routes, stingers and Lumia mappings are preservation requirements. Simplifying onboarding must not silently remove them.

### Give readiness a precise meaning

A proposed check can be **not checked**, **checking**, **needs attention**, **optional/deferred**, or **checked for a stated scope**. Show what was checked, what remains untested and how to fix a limitation. This is a Pulse-specific state model informed by feedback and explicit commitment. ([S7](#s7-w3c-wai-user-notification), [S8](#s8-microsoft-wizards))

| Milestone | Evidence it can establish | What it must not imply |
|---|---|---|
| Draft captured | Choices are retained and required entries are present. | Sources exist, connections work or outputs are ready. |
| Show created | Intended scene/Stage/Look data has been applied. | The result has been rendered or heard correctly. |
| Local checks complete | The named checks passed at that time. | Untested audio, service delivery or workload performance. |
| Private rehearsal complete | The explicitly selected render, audio or encoder path was exercised within its stated scope. | Public delivery or adequate headroom under the user's game workload. |

A rehearsal must describe its side effects before execution. It must not silently start a public broadcast, recording, audio playback, hardware command or account mutation. A static illustration, stored account token and actual encoder rehearsal are different evidence. Selecting settings is not a successful rehearsal.

### Keep teaching within the resource budget

Prefer lightweight static illustrations or cached thumbnails for browsing choices, clearly identified as illustrations. Learning guidance does not require dozens of simultaneous live previews. If an actual preview is necessary, consider one selected, opt-in preview with bounded work that stops when hidden; measure the cost before adopting it.

Avoid autoplay and parallel browser, video or camera instances merely to demonstrate options. This is an engineering recommendation based on the user's CPU/RAM priority, not a measured performance result or evidence that an illustration validates a real source. Preserve the actual multistream, stinger and Lumia capabilities while reducing presentation overhead.

### Make recovery accessible

Card-like choices should retain suitable single-choice or multiple-choice semantics, keyboard operation, visible focus and selected state. Associate instructions and errors with their controls. Restore focus sensibly after Back or correction, and keep essential help available without hover. Announce meaningful check results without requiring users to detect a color change. Native behavior must be tested; visual similarity to an accessible web example is insufficient. ([S4](#s4-govuk-question-pages), [S7](#s7-w3c-wai-user-notification))

## Proposed validation

These checks have not been performed by this research task. Flow-specific counts, current coverage and observed defects belong in the [companion audit](<SETUP-WIZARD-AUDIT-2026-10-25.md>), rather than being inferred from guidance.

1. **First meaningful task:** give a new user a concrete show goal without first explaining Stage/Look. Ask them to choose and describe the effect in their own words. Record discovery problems, misunderstandings and assistance requests.
2. **Revision and retained work:** change an earlier choice, return to review, close/resume and repeat application unchanged. Check preserved answers, stale dependencies and duplicate operations.
3. **Recovery:** introduce a missing source or disconnected account in an inert fixture. Can the user identify and fix the affected part without losing other work?
4. **Accessible operation:** complete the relevant route with keyboard and assistive technology, including constrained window size and text scaling. Check focus, choice semantics, selected state and announced results.
5. **Learning over time:** compare independent success, time to a correct useful result, assistance and recovery errors. Ask what the completed checks prove. Keep any later coached exploration separate from independent success; S2 does not make coached completion a substitute for autonomous use.
6. **Resource and feature preservation:** compare CPU/RAM and retained media/source counts before, during and after setup. Verify that dismissed previews stop background work. Preserve multistream/stinger/Lumia configuration, and arrange an explicitly authorized private rehearsal separately when actual playback or encoding is needed.

Research can guide the questions and interpretation. Source inspection establishes behavior; native fixtures establish their tested conditions; observed user tasks establish evidence about usability. None alone establishes the others, and no empirical Pulse Weaver usability result is claimed here.
