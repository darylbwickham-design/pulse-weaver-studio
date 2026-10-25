# Stream workflow and useful-output research

Retrieved 25 October 2026. Read-only product/source inspection plus primary research and official service documentation. These findings concern the outputs a setup wizard should help a streamer produce. The separate academic-usability investigation addresses how to teach and navigate the task.

## Judgment

The current catalogue contains useful ingredients but organizes many choices around generic feed roles and alternative proportions. A meaningful operating view should change what the viewer can understand or what the streamer can safely do next. A different camera size alone is usually a configuration option. A different subject, source, visibility state or production segment can justify a separate Look.

The evidence supports representative gaming, social, creative and teaching workflows. It does **not** establish that any fixed catalogue covers a measured majority of all streamers, or validate specific panel percentages. Coverage should be stated as tested cases, not “90% of use cases.”

## Primary research

### A1. Hamilton, Garretson and Kerne (Texas A&M, CHI 2014)

[Streaming on Twitch: Fostering Participatory Communities of Play within Live Mixed Media](https://ecologylab.net/research/publications/streamingOnTwitch.pdf), DOI [10.1145/2556288.2557048](https://doi.org/10.1145/2556288.2557048).

An ethnographic study including interviews with 11 streamers and four viewers describes gameplay, webcam composition and participation through chat. It also observes substantial out-of-game interaction. This supports treating content focus and direct audience conversation as different activities. It does not experimentally compare camera sizes or establish a universal scene layout. The study is historical and gaming focused; its contemporary platform statistics and commercial details should not be reused as current facts.

**Product inference:** offer a primary content view and a genuinely camera-focused conversation state, sharing the same input source. Keep the broadcaster's chat-reading tools separate from an optional chat overlay in the transmitted picture.

### A2. Fraser et al. (UC San Diego/Adobe Research, C&C 2019)

[Sharing the Studio: How Creative Livestreaming can Inspire, Educate, and Engage](https://research.adobe.com/publication/sharing-the-studio-how-creative-livestreaming-can-inspire-educate-and-engage/), DOI [10.1145/3325480.3325485](https://doi.org/10.1145/3325480.3325485). [Author PDF](https://ailiefraser.ca/Creative_Live_Streams.pdf).

Content analysis, interviews with eight streamers and a survey of 165 viewers identified teaching, making, socializing and performing streams with differing goals. The official abstract and indexed author-PDF passages were retrieved; direct PDF opening exceeded the browser fetch size. The evidence supports looking beyond gameplay when choosing cases. It does not show that an onboarding interface or a prescribed set of panel ratios works.

**Product inference:** distinguish a digital work/screen route from a physical work camera route. A real close-up camera is a useful additional view. A duplicate composition that merely enlarges the same face feed is better handled within configuration.

### A3. Pirker, Steinmaurer and Karakas (Graz University of Technology, ITiCSE 2021)

[Beyond Gaming: The Potential of Twitch for Online Learning and Teaching](https://jpirker.com/wp-content/uploads/2021/04/2021_ITICSE_Twitch_Paper-1.pdf), DOI [10.1145/3430665.3456324](https://doi.org/10.1145/3430665.3456324). [University publication record](https://graz.elsevierpure.com/en/publications/beyond-gaming-the-potential-of-twitch-for-online-learning-and-tea).

Two university courses, Programming 101 and Game Development, used live lectures, content, questions and chat interactions. Survey participation and completion were partial. Findings include perceived participation benefits and distractions/spam. These are real teaching cases, not a controlled layout study.

**Product inference:** a screen demonstration route must preserve readable content, alongside an optional presenter. A “teaching” label on a camera-dominant crop does not make slides or code usable. Separate local picture/audio rehearsal from structural source assignment.

## Official production guidance

### P1. Twitch: Stream Scenes & Overlays

[Current Creator Camp chapter](https://www.twitch.tv/creatorcamp/en/level2/better-broadcasts/stream-scenes-and-overlays/).

The chapter organizes examples by Starting Soon, Just Chatting, Game Play/Main Feature, BRB and Ending. Its strategy is to match scenes to the current activity and avoid excessive simultaneous information. This directly supports distinct lifecycle screens and content-versus-conversation focus. It is platform guidance, not a randomized trial or a required count of scenes. Its purpose does not justify producing all possible variants by default.

### P2. Twitch: Creative, Coding & Co-working

[Current Creator Camp chapter](https://www.twitch.tv/creatorcamp/en/level1/content-categories-on-twitch/creative-coding-co-working/).

The creative setup guidance explicitly describes a face camera plus overview of the work, including a camera/smartphone combination. It discusses Show & Tell and distinguishes coding and co-working practices. A work-overview view and a different detail view are meaningful when those inputs exist. This does not require a second camera for everyone or establish one ideal split size.

### P3. Twitch: Just Chatting, ASMR & Talk Shows

[Current Creator Camp chapter](https://www.twitch.tv/creatorcamp/en/level1/content-categories-on-twitch/just-chatting-asmr-talk-shows/).

The chapter covers solo audience conversation and host/guest discussions. It supports a camera-first route and an optional guest/call case. It does not provide a guest transport SDK or prove that capturing a call window automatically captures the right audio.

### P4. OBS: Sources Guide

[Official Sources Guide](https://obsproject.com/kb/sources-guide).

OBS distinguishes game, window, display, video-device, image, media and browser sources. Source ordering determines visual stacking; sources can be hidden; fit and crop are different transforms. This supports asking what the streamer is showing before asking for a capture type, and explicitly naming alert order. It does not imply that every running window is capturable or that source existence proves live output.

### P5. OBS: Browser Source

[Official Browser Source reference](https://obsproject.com/kb/browser-source).

The reference separates URL/local file, viewport dimensions, unloading when hidden and refresh on activation. Those lifecycle properties matter for alert state and audio continuity. Preserve the existing app policy: source reuse across views, imported alerts kept warm and no automatic refresh. Avoid applying a generic echo workaround that destroys alert state. The reference is not evidence that a particular third-party alert has connected or played successfully.

### P6. Twitch: Dual Format

[Current official dual-format guide](https://help.twitch.tv/s/article/dual-format-vertical-video).

The guide calls for vertical framing of important content and checking it on a mobile device. It says Twitch's mobile UI already overlays chat, so an additional vertical chat overlay is not required. Its text references safety-zone examples but the current fetched HTML does not expose a reliable numerical rectangle. Do not invent fixed universal coordinates. Mobile overlays and other destinations differ.

### P7. YouTube: Live formats

[Official live-streaming guide](https://support.google.com/youtube/answer/2474026?hl=en).

Current documentation distinguishes horizontal/vertical layouts and lists centre crop, fit-to-phone width and stacked arrangements. It also supports separate encoder feeds. These alternatives reinforce that portrait is a separate composition decision, rather than a blind landscape crop. Service routing and eligibility details are outside the proposed wizard change and should not be inferred from diagrams.

## Current product facts that explain confusion

Inspected `pulse-show-templates.hpp`, `pulse-setup-flexible.hpp`, `pulse-source-canvas.hpp` and `pulse-show-builder.cpp` in `worktrees/alpha2-installer-release` at the frozen unstable.4 source.

- Setup currently has six Stage groups and 17 recommended Looks. Several use the same two feeds with corner/split/rail changes. Their purposes are not always distinct.
- Feed A/B/C are Stage-local identities. Asking repeatedly by those letters forces a novice to remember their meaning and manually reuse the same camera across groups.
- Three shared alert/overlay layers **already exist per canvas** (six optional assignment questions). They start behind an unchecked “Add shared layers” option. A user wanting alerts may never recognize that label.
- Generic valid HTTP/HTTPS overlay URLs are already accepted. The UI's Lumia-only wording is narrower than the validator; the main restrictions reject native generated overlays and Lumia template-import links.
- The creation backend already accepts `builtin_graphic`, creates local HTML per Look/canvas and can render a countdown. Current flexible setup does not expose this as a useful default. Existing generated HTML has transparent background and white text; it needs deliberate background, contrast and relevant copy before being offered as a polished built-in screen.
- Current portrait layout is stacked, but layout eligibility, content visibility and fit/crop must be tested with actual generated sources. Role labels alone do not guarantee a readable game HUD, code or call grid.
- Existing audio/monitoring and warm-browser policies are preservation requirements. A BRB picture must not claim it mutes a microphone unless that behavior is explicitly implemented and tested.

## Proposed route and catalogue (design judgment)

First ask **What will viewers watch?** Present recognizable activity cards, not “Full show” versus a technical starting state. Then identify the required content source and optional face camera once, reusing their identities throughout the generated show. Offer another activity or angle later. Standard starting/break/ending screens should work immediately with built-in visuals; custom Lumia artwork remains an option.

| Route | Minimum input | Purposeful operating views | Optional capability |
|---|---|---|---|
| Game | PC game or console/capture-card feed | Main game; game only when camera exists; talk to viewers when camera exists | Another real capture/angle |
| Screen demonstration / reaction | Window/app/browser feed | Content focus; content only; camera focus when provided | Change priority/composition inside Configure |
| Camera / conversation | One camera, avatar or existing video feed | Full conversation view | Add content sharing |
| Physical work / performance | Work overview camera | Work overview; face-only conversation when provided | Detail view only when a separate detail feed exists |
| Guest / call | Existing call-window or browser feed | Whole call; host-only when separately available | Independent two-person views only if supported explicitly |

These are representative coverage cases, not claims about the percentage of streamers served. “Game only” is not an extra output when the user has no camera; avoid emitting two identical views. Camera size/corner, fit/crop and split proportions belong to configuration, not a catalogue full of near-duplicate names. Preserve internal Stage/Look identifiers where required by compatibility, while improving the presented language and generated plan.

### Built-in lifecycle screens

Offer one working Starting screen (optional countdown), one BRB screen and one Ending screen. Each must contain visible high-contrast text/background on both canvases without any external URL. Custom text and external artwork are optional. Explain that selecting Ending does not stop a broadcast and a countdown does not automatically start one. Do not call an away screen a privacy/microphone mute feature.

### Three alert slots

Expose **Alerts & overlays — up to 3 layers** as an obvious step/card. Show numbered layers and which is on top. Let the user add one layer, then optionally a second/third; never require three. Pair horizontal/portrait choices in each layer, clearly explaining when the provider supplies separate artwork. Retain one source identity per layer/canvas across its selected Looks, rather than creating one per Look. Empty layers create nothing. Provide explicit visibility scope and preserve existing audio properties. A test-event/rehearsal action must describe actual behavior; an assigned URL is not “tested.”

## Acceptance tasks for usable results

1. **No-camera gamer:** choose a game and built-in lifecycle screens; create without a camera or Lumia URL. Exactly one game view is emitted, no duplicate camera-less views or empty camera prompts. Game fixture edge/HUD markers stay visible with fit.
2. **Camera gamer:** choose one camera and one game. The camera UUID is reused. Main, content-only and camera-focused views have genuinely different visible source sets; changing corner does not add a fourth redundant Look.
3. **Screen tutorial:** use a synthetic slide/code window with known edge labels and small text. Verify fit versus crop, landscape readability and portrait framing visually; do not certify text legibility from rectangle bounds alone.
4. **Physical work:** choose overview + optional face. Add a real detail source later; only then produce the detail view. Confirm the alternate view uses the detail UUID, not a duplicate overview composition.
5. **Guest call:** fit a synthetic multi-person call grid so all participants remain visible. State that call audio still needs verification. Do not imply built-in guest invitation/transport exists.
6. **Lifecycle:** generated start/BRB/end screens visibly render their intended text/background in both formats without external services. Validate countdown semantics and creation cancellation/rollback.
7. **Alerts:** assign 0, 1, 2 and 3 layers, both formats; inspect source identity, target Looks and top-to-bottom order. No fourth layer is accepted. Disabling a layer removes its planned creation without damaging shared existing sources. Exercise an actual provider/local mock event separately from URL validation.
8. **Continuity:** apply Looks and Stage/stinger transitions while an explicitly configured alert/audio test runs. Check actual playback and monitoring, not just retained property values. Do not silently change existing sources or enable duplicate audible copies.
9. **Novice opening task:** give a new participant a plain outcome (“I want to stream a game with my webcam and alerts”). Record their first choice, requests for help, wrong turns and whether the created result matches their intention. Distinguish uncoached completion from an expert walkthrough. The friend's report is evidence of a problem, not a completed usability study.

## Limits and explicit exclusions

No live broadcasts, hardware devices, guest services or actual alert events were operated for this research. The sources do not establish universal safe zones, exact camera percentages, audio correctness or performance under gaming load. A complete show-shaped graph is necessary but insufficient: practical validation needs visible generated output and representative local transitions/audio, plus later novice feedback.
