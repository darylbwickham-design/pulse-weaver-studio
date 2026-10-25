# Task-first setup: academic evidence and proposed acceptance criteria

Research retrieved 25 October 2026. Research notes for the Pulse Weaver setup revision, not a report of a completed Pulse Weaver usability study. No product files were edited for this research task.

The reported failure is immediate: a friend was confused on opening Build my show. The user's other requirements are usable generated results, meaningfully different Looks, a click-through interface, and up to three alert layers. These are product requirements and user feedback, not conclusions supplied by an academic paper.

## Decision in plain terms

The strongest change is to organize the first route around what the streamer wants viewers to see, bind the real content/camera once, and then generate a small useful show. The internal Stage → Look → Feed model can be explained when the resulting show makes those concepts concrete. Repeated visual treatments should be configuration within a meaningful layout family, rather than separate mandatory choices.

One concrete problem in the current opening is that Full show describes scope/completeness while Game and content and Conversation describe activities. Someone who wants a complete gaming show can reasonably find both applicable. Separate the activity decision from whether to include Starting/BRB/Ending screens. This is a design analysis of the current choices, not an additional participant observation.

The sources below support this direction and a method of checking it. They do **not** establish a universal card count, an optimal camera percentage, that every list is bad, that a 3D button is more usable, or that this proposed wizard covers a quantified majority of streamers. Streaming-specific practice and native output tests must supply that part of the evidence.

## Twelve annotated primary sources and university materials

### S1 — University of Washington: task analysis before interface mechanisms

[CSE 440, Task Review](https://courses.cs.washington.edu/courses/cse440/26au/assignments/milestone-3/assignment-3-1), Autumn 2026, sections before Submission.

**Evidence:** The assignment asks designers to consider current and desired work, learning, other tools, time constraints and failures. It requires representative tasks spanning functionality, stated as accomplishments without prescribing a particular interface. Tasks should evolve with research and feedback.

**Pulse interpretation:** “Set up gameplay with a camera and three alerts” is a useful task. “Choose a Stage and select Feed A” presupposes the current mechanism and hides the novice's problem. Use a coverage matrix of complete shows, not a catalogue of controls.

**Limit:** This is HCI teaching guidance, not an experiment comparing Pulse setup flows. Its six-task assignment is not a universal optimum.

### S2 — University of Washington: contextual inquiry

[Amy Ko, INFO 360 contextual-inquiry activity](https://faculty.washington.edu/ajko/courses/info360/activities/contextual-inquiry.html).

**Evidence:** Learners observe an actual task in its real context, adopt an apprentice stance and ask about work as it happens. The activity distinguishes observing the participant's practice from imposing a prepared interaction script.

**Pulse interpretation:** Observe the friend's opening attempt, record the first misunderstood wording and what result they expected. Ask what they were trying to set up, what tools/inputs they actually have, and where they looked first. Do not treat confusion as a failure to read our preferred model.

**Limit:** A classroom activity is not a sampling or prevalence study. One friend is valuable formative evidence, but cannot establish how frequently the problem occurs.

### S3 — University of Colorado authors: concrete, complete tasks

[Lewis and Rieman, Task-Centered User Interface Design, chapter 1](https://cspages.ucalgary.ca/~tam/2001/hci_topics/papers/LewisRiemanBook/chap-1.html), 1993/1994; University of Calgary teaching mirror, especially §§1.1–1.2.

**Evidence:** The method begins with users and their actual work. It recommends information in an order compatible with that work, representative tasks with concrete materials, and both simple and extended tasks because isolated actions miss problems.

**Pulse interpretation:** A camera and a game/window are concrete inputs the user can recognize. Start there. A successful scenario must end with the expected usable scenes, source reuse and recovery from a mistaken choice. A wizard that merely returns an “ok” result has not completed this evaluation.

**Limit:** This is a foundational methods book with old interface examples. It supplies a process, not current streaming conventions or recommended pixel geometry.

### S4 — University of Maryland: consistency, feedback and recoverable work

[Ben Shneiderman, Eight Golden Rules](https://www.cs.umd.edu/~ben/goldenrules.html), formulation from the 2016 textbook edition.

**Evidence:** The author recommends consistent wording and actions, feedback proportional to consequence, understandable completion, constructive correction and reversible work. He explicitly says the rules need adaptation and validation in each domain.

**Pulse interpretation:** Keep matching main-UI controls and terminology where users need them; preserve answers on Back. Summarize the actual chosen camera/content and the generated result. Make correction local. Distinguish source assignment from an actual picture/sound check.

**Limit:** These are design heuristics rather than a controlled Pulse experiment. Do not turn the page's historical memory rule into an arbitrary fixed maximum for cards, Looks or alert layers.

### S5 — UC San Diego: the user's intention versus the system's representation

[Hutchins, Hollan and Norman, Direct Manipulation Interfaces](https://hci.ucsd.edu/hollan/direct-manip.pdf), Human–Computer Interaction 1(4), 1985, pp.311–338; particularly pp.325–327.

**Evidence:** The paper distinguishes the effort of translating intentions into system operations from the physical means of expressing them. It explains that practiced users can experience an interface as direct even when novices still face substantial conceptual translation.

**Pulse interpretation:** Internal IDs and the Stage/Look/Feed hierarchy should not be the admission test for making a first show. Use camera/content names in the primary route; introduce the internal model beside the tangible result. A developer's smooth walkthrough cannot establish a novice's comprehension.

**Limit:** This is a theoretical account with interface examples, not an empirical comparison of these wizard labels. It does not justify hiding necessary consequences or making presets inscrutable.

### S6 — Leeds and Oregon State: cognitive walkthroughs catch misleading tasks and controls

[Green et al., Using the Cognitive Walkthrough to Improve the Design of a Visual Programming Experiment](https://www.cs.cmu.edu/~NatProg/papers/Green2000UsingCWsForStudies.pdf), 2000, original paper hosted at CMU; §§2 and 6–7.

**Evidence:** The authors use a goal/action/feedback walkthrough to improve a visual-programming evaluation. They identify issues in instructions, labeling and extraneous interface details that had previously seemed unimportant. The method helped a team after brief training.

**Pulse interpretation:** At every setup step ask whether the novice can identify their goal, see the relevant action, connect it to their intended result and recognize progress. Apply this to the opening itself. Record a failure explanation instead of assuming the correct click.

**Limit:** An evaluator's prediction is an inspection result, not a participant's observed success. This paper concerns experiment design and a particular programming system.

### S7 — Mississippi State, Wisconsin–Madison and Freiburg: worked examples

[Atkinson, Derry, Renkl and Wortham, Learning from Examples](https://assess.ucr.edu/sites/default/files/2019-02/atkinsonderryrenklwortham_2000.pdf), Review of Educational Research 70(2), 2000, pp.181–214; university-hosted original review.

**Evidence:** The review discusses integrated example components, meaningful labeling/segmentation, different examples that reveal underlying structure, and practice closely matched to those examples. The early skill-learning setting matters.

**Pulse interpretation:** Show a concrete example of the selected activity with labeled camera/content and then let the user bind their own inputs. Demonstrate the substantive difference between content-first, presenter-first and comparison views. Cosmetic variations should not masquerade as different concepts.

**Limit:** Educational problem solving is not streamer onboarding. A schematic can explain a layout but does not prove the generated show renders correctly or that a browser connection works.

### S8 — UC Santa Barbara: learner-paced segments and concept preparation

[Mayer, Segmenting, Pretraining and Modality](https://doi.org/10.1017/CBO9780511816819.012), Cambridge Handbook of Multimedia Learning, 2005 print edition, chapter 11, pp.169–182; publisher summary and author affiliation reviewed, online publication 2012.

**Evidence:** The chapter's summary reports evidence for learner-paced instructional segments and prior familiarity with the important concepts in multimedia learning. It addresses overload in relatively fast instructional messages.

**Pulse interpretation:** Keep the click-through pacing, introduce only the concepts needed for the current decision, and integrate a short example with that decision. Later explain how the chosen concrete views map to Stage/Look controls.

**Limit:** This does not demonstrate that every additional wizard page helps. Nor does the modality finding justify forced narration, autoplay or removing useful visible labels. Full chapter text was not available through the public publisher page.

### S9 — Basel, Mannheim and Indiana: fewer choices is not a universal solution

[Scheibehenne, Greifeneder and Todd, Choice-Overload Meta-analysis](https://scheibehenne.de/ScheibehenneGreifenederTodd2010.pdf), Journal of Consumer Research 37(3), 2010, pp.409–425. [University of Basel record](https://edoc.unibas.ch/entities/publication/1cd7cefa-8ab9-4be8-9a0f-5412feb5350d).

**Evidence:** Across 63 conditions from 50 experiments, the authors found a near-zero mean effect with substantial variation. They could not establish sufficient conditions under which larger sets necessarily cause overload.

**Pulse interpretation:** Do not respond to confusion by claiming that three or six choices is scientifically optimal. Remove redundant comparisons, clarify what each option accomplishes, and retain a route to uncommon needs.

**Limit:** Consumer-choice experiments are not streaming-software tasks. This finding also does not show that the current large or repetitive catalogue is good.

### S10 — Northwestern: complexity and uncertainty qualify choice overload

[Chernev, Böckenholt and Goodman, Choice-Overload Review and Meta-analysis](https://chernev.com/wp-content/uploads/2017/02/ChoiceOverload_JCP_2015.pdf), Journal of Consumer Psychology 25(2), 2015, pp.333–358; author-hosted original.

**Evidence:** The analysis of 99 observations identifies decision difficulty, choice-set complexity, preference uncertainty and decision goal as moderators. The presentation and relationship of options matter alongside their number.

**Pulse interpretation:** Ask about the streaming activity before visual preferences; recommend a suitable starting arrangement. Let someone preview actual compositional differences without comparing every inset position and mask style simultaneously.

**Limit:** This is consumer decision research and does not directly validate a particular wizard. Treat transfer as a hypothesis to test with novice task completion, not a guaranteed effect size.

### S11 — Stanford: explore distinct alternatives before polishing one

[Dow et al., Parallel Prototyping](https://aaalab.stanford.edu/assets/papers/2010/Parallel_Prototyping_leads_to_better_design_results.pdf), ACM TOCHI 17(4), article 18, 2010; especially §§3–4.

**Evidence:** A study with 33 completing participants compared parallel and serial advertisement prototyping. Parallel work produced more diverse alternatives and improved some performance and confidence measures. The task, timing and feedback were controlled.

**Pulse interpretation:** Compare genuinely different opening flows or recipe compositions before investing in another styling pass. Evaluate their differences against viewer/content tasks. Avoid repeatedly refining the same scene with new names and visual decorations.

**Limit:** These were banner advertisements, not generated stream scenes. More diversity alone was not a universal predictor of performance. The study does not imply that every user should receive every prototype as a permanent option.

### S12 — Toronto and Microsoft Research: preference feedback is not successful use

[Tohidi, Buxton, Baecker and Sellen, Getting the Right Design and the Design Right](https://www.billbuxton.com/rightDesign.pdf), CHI 2006, pp.1243–1252. [Publisher-author research summary](https://www.microsoft.com/en-us/research/publication/getting-the-design-right-and-the-right-design-testing-many-is-better-than-one/).

**Evidence:** Presenting one design rather than three alternatives inflated subjective ratings and reduced criticism in this study. The authors also caution that users' evaluation comments did not reliably supply constructive redesign solutions.

**Pulse interpretation:** Take the friend's confusion seriously without requiring them to design the fix. Compare alternative opening approaches if possible, and measure whether people can actually create and understand their show. “Looks good” alone is insufficient acceptance evidence.

**Limit:** The result concerns a particular comparative prototype study. Three alternatives is its experimental condition, not a required number of setup cards.

## Implementation implications to test

These are Pulse design judgments informed by the sources above, not measured outcomes.

1. **A concrete first question.** Ask what the person is streaming: gameplay, camera conversation, a screen/presentation, a desk/activity, or an advanced/custom setup. The exact grouping remains a hypothesis; use the supported workflow matrix to settle it. The primary route must work without learning the internal hierarchy first. [S1–S3, S5–S6]
2. **Bind real inputs once.** Offer a recognized camera and game/window/screen choice, with an explicit no-camera route. Reuse those source objects in generated views. Explain sharing when it has a consequence; do not ask for the same camera under new A/B/C labels. [S3–S5]
3. **Offer useful compositions, not disguised styling variants.** Distinguish content-only, content with presenter, presenter with supporting content, equal comparison and appropriate two-camera/workbench views where supported. Position, mask and border tweaks belong in customization. Do not indiscriminately deduplicate across stages when different content or operating purpose is meaningful. [S7, S9–S11]
4. **Create a useful local show.** Common Starting, Away/BRB and Ending screens need a legible built-in fallback. External Lumia artwork should be optional customization, not a mandatory dependency for a basic camera/game stream. This is a concrete task-completion requirement, rather than a direct result tested by these papers. [S1, S3, S7]
5. **Make three alerts understandable.** Expose three logical alert slots together in an obvious optional step. Put landscape and portrait values together per slot and show visibility plus intended audio ownership. Keep advanced targeting available. Three is the user's requirement; the research does not supply this number. [S4–S6]
6. **Show the result and explain the next action.** Review should identify the actual inputs, chosen views and omitted optional features. After creation, select the created show and tell users where to switch its views. Introduce Stage and Look terminology beside these concrete examples. [S4–S8]
7. **Keep teaching cheap and relevant.** Use a static labeled example first; avoid decorative diagrams that do not distinguish choices. A live source check should be explicit and bounded. Do not add autoplay or parallel browser/camera instances simply to explain a preset. Resource constraints are the user's requirements, not performance findings from the academic papers. [S5, S7–S8]

## Acceptance must separate three different claims

| Claim | Suitable evidence | Insufficient evidence |
|---|---|---|
| Technical creation works | Generated scene graph, source identity, native render and invariants checked after Create | The last page is enabled; result JSON says ok |
| The result suits the intended use | An expected-output scenario verifies content emphasis, readable fallback screens, source reuse, alert layering and both formats | Every catalogue ID can be generated; all Looks have different names |
| A novice understands setup | Observed first-use task with goal comprehension, independent decisions and correction recorded | An expert/agent can navigate it; screenshots fit; code checks pass |

## Proposed end-to-end coverage tasks

These scenarios are a starting coverage matrix, not evidence of market prevalence. Each should start from a fresh disposable configuration and finish by inspecting the real generated show. Use clearly labeled inert inputs for deterministic tests, then real selected-device checks separately.

| Task stated as an outcome | Essential assertions |
|---|---|
| Stream a game with no camera and no Lumia installed | No mandatory camera/artwork questions; game visible; usable local Starting/BRB/Ending; no empty camera-only views |
| Stream gameplay with one camera and three alert layers | Camera selected once and reused; three independently configurable slots; correct stacking/targeting on both canvases; no accidental fourth layer or duplicated audible owner |
| Talk to viewers with one camera | A working camera-focused view and readable framing; no mandatory game capture or unnecessary twin camera definitions |
| Explain a browser/app or slides with a camera | The selected content is readable and emphasized appropriately; a presenter-focused alternative is materially different, not merely renamed |
| Show a desk/craft/creative activity with a detail camera | The two inputs retain separate identities; wide/detail emphasis and any simultaneous view have an explainable purpose |
| Run the same basic show in landscape and portrait | Each actual result is contained and usable; no stretched content, hidden essential text or misleading same-layout assumption |
| Add one alert now, then return and add all three | Prior answers persist; new layer choices do not reset camera/content or silently alter existing source audio |
| Correct a mistaken source from Review and then cancel another draft | Change targets the intended assignment and returns coherently; unaffected answers survive; cancellation adds no partial show |

For every generated Look, compare a normalized signature of assigned roles, visibility, geometry, fit/crop and stacking. Identical signatures under new names are duplicate candidates. This check must not remove intentional operating-purpose distinctions across stages, and it cannot alone prove visual usefulness. Separately inspect actual renders containing labeled content and aspect-ratio cues.

For alerts, exercise 0, 1, 2 and 3 slots, multiple visibility targets, per-format assignments, and source reuse while switching views/stages. Inspect which source instance and intended audio path remains active; do not infer continuity solely from a checkbox. Preserve the existing stinger/Lumia/audio contracts unless a separately verified change is required.

## First-use study plan, clearly not yet performed

Begin with people unfamiliar with Pulse Weaver, including at least one person unfamiliar with OBS and one experienced streamer new to this app. A small formative batch is a practical starting point, not a statistical coverage guarantee or a claim that a fixed sample finds a known percentage of problems. If the reporting friend is available, retest their actual original goal with consent; the software agent cannot supply that participant evidence.

Give an outcome task without naming buttons or internal objects. Record the first intended action, time to a meaningful decision, misunderstood terms, coaching requests, backtracks, mistaken inputs and successful recovery. Ask the participant to explain what will be created before Create, then compare that explanation with the actual output. Record technical completion, usable output and independent understanding separately.

Opening-page comprehension is a primary checkpoint: can the participant identify the route for their real equipment/activity without a tutorial on Stage/Look/Feed? Numerical time or success-rate targets should be set as product goals and calibrated with the pilot, rather than attributed to the papers. Also test keyboard-only navigation and the supported small-window/display-scale conditions.

Do not report this plan, an evaluator's cognitive walkthrough or automated scenario checks as a completed novice study. “Most use cases” should be replaced in release evidence with the explicitly supported and tested scenario inventory until actual user research provides a broader basis.
