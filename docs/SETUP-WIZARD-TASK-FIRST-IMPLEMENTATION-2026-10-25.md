# Task-first setup: implementation and verification

This revision responds to immediate first-screen confusion and repeated compositions in the unstable 4 builder. The [baseline output audit](SETUP-WIZARD-OUTPUT-AUDIT-2026-10-25.md) identifies concrete deficiencies. [Twelve academic sources](SETUP-WIZARD-TASK-FIRST-RESEARCH-2026-10-25.md), [three streamer studies and seven official production references](SETUP-WIZARD-STREAM-WORKFLOWS-2026-10-25.md) inform the direction. This is research-informed implementation and an expert walkthrough, not a completed independent novice study.

## Result-oriented flow

The default route is Activity → Inputs → Views → Screens → Alerts → Review. The opening compares four activities on one dimension: games, conversation/shared content, creative/teaching work, or cameras/guests. Show completeness is a separate decision. A newcomer does not need to understand Stage, Look or Feed A/B/C to start.

Inputs are assigned once, by recognizable purpose. Existing input UUIDs and new capture definitions are reused rather than manually assigned under five Stage groups. Games and creative routes require content/work; cameras are optional. Guest/call support means using an existing picture source, not creating a new call service. Capture and sound still need an actual local check.

Main offers only views supported by distinct assigned inputs. Content-only and camera-only differ by visible source. Combined, detail/guest, genuine two-input comparison and three-input views have substantive subject/visibility differences. Seven maximum view definitions do not mean seven default selections. Position/crop changes are framing choices. The retained detailed builder handles saved layouts and specialist per-stage work without changing existing shows or their IDs.

## Usable local defaults

Starting, Break and Ending have opaque, legible static PNG cards with editable messages and a themed background. Ordinary image sources render these; no CEF instance, HTML timer, network call or idle polling is needed. A separate shared background sits below Main's pictures. Custom browser artwork replaces either holding format independently, with the built-in card retained for an unspecified format. A static Starting screen never claims to be a running countdown.

Fit/crop diagrams use known input dimensions and explicitly label unknown capture assumptions. New browser viewports use their own canvas dimensions. Game/content fit preserves the entire frame; camera/detail cropping requires an explicit choice. A landscape picture cannot fill portrait without either cropping or space around it. The implementation does not invent a universal platform-safe rectangle or promise that arbitrary game HUD, code or people are readable without inspection.

## Three logical alert layers

The visible optional Alerts step has three numbered cards, each grouping its landscape/portrait values. Layers 1–3 sit above pictures in deterministic order, with layer 3 highest. They can appear on Main or all generated screens. Empty/disabled slots create no browsers. A responsive link can produce H/V viewport copies; separately supplied links and existing compatible sources are supported. Reopening a landscape-only choice must retain it.

The guided mixed presentation policy permits static cards for graphics/backgrounds while keeping alerts browser-only, correctly oriented and scoped. The old detailed Lumia-only request policy remains intact. Existing audio, monitoring, browser warm-state and no-refresh behavior are retained. New browser copies, including browser content inputs, start with output audio off; the UI calls for choosing one intended audible copy and checking real sound. Static or inert test inputs cannot establish live alert delivery or stinger sound continuity.

## Verification boundaries

The final source-stable native build passed. The complete regression suite passed **8,349 checks**, including 15 completed-show scenarios and an actual default wizard Finish path. These inspect the created native scene/action graph, source identity and reuse, exact view sets, both canvas transforms, PNG generation and geometry, explicit cropping, three alert orders/scopes, audio-property preservation and rollback after real generated-file creation. The assertion count includes many checks within each scenario; it is not 8,349 independent user tasks. Minimal-platform font output does not establish glyph readability; a separate frozen-header Windows/Segoe UI rendering run passed 39 checks and its long-message images were visually inspected.

The real frontend walkthrough exposed a Windows file-handle conflict when a Stage catalogue already existed. The guided wrapper retained its read handle while creation attempted an atomic replacement. Its reader now leaves scope before the modal wizard opens. The same actual-wrapper Finish case was extended with an unrelated existing show and sentinel fields: it reproduced the exact failure before the fix, then passed 138 focused checks with the fix, including exact preservation of the existing entry. The native failed attempt also verified rollback to the original three fixture inputs, no new Looks/browser inputs/generated artwork, and inactive outputs. Detailed setup already released its reader correctly.

The readable Windows guided-UI run passed **204 checks** at **1100 × 800**, **780 × 620** and **740 × 560**. It verifies six pages and focused editors, preserved Back/Review answers, cancelled draft side effects, separate custom artwork/fallbacks, deliberately cleared view choices, browser feed compatibility, and visible/reachable controls. The source manifests and executable hash remained unchanged throughout both final phases. The supplied test stylesheet combines a resolved frontend baseline with current setup rules; its test executable lacks some frontend checkbox icon resources, so actual native checkbox appearance is checked separately.

The UI review separately checks first-use goal/action/feedback, Back/Review corrections, intentionally cleared choices, conditional questions and small-window geometry. Automated technical completion, visually useful native output and independent novice understanding are different claims. A real novice retest remains necessary to establish the last of those; neither an agent's smooth walkthrough nor passing geometry assertions substitutes for it.

Actual native output verification uses a new portable fixture, labelled image inputs, local inert alert pages and OBS WebSocket scene screenshots. It copies no user's show, opens no camera/capture device, connects no account and starts no stream/recording. The full test and native render receipts are kept in the workspace test evidence; representative coverage is reported by scenario, not an invented percentage of all streamer use cases.
