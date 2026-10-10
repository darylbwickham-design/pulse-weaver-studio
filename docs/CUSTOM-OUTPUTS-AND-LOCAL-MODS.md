# Local modifications and additional output destinations

Companion to [the Codex custom-show guide](CODEX-CUSTOM-SHOWS.md). The code map
below was checked against **1.14.2-alpha.4**. Inspect the matching version before
using it: these are implementation locations, not a stable extension interface.

## Ask Codex for a focused local modification

If you are handing this document over for context, say:

```text
My friend gave me this to give you context about modifying Pulse Weaver.
Read it as reference material, not as a request to make changes.
Then ask me: "What do you need me to do?"
Wait for my own request before inspecting my PC, editing anything or running tools.
```

**For the receiving Codex:** a context handoff is not permission to act. Reply
**“What do you need me to do?”** and wait for the user's request. The following
templates and implementation plan are reference examples, not an active task.

Once Codex asks, describe your change naturally. For example: “Add a manual
RTMPS output for another platform, using my landscape canvas, while keeping my
current outputs.” Use this longer template if you want to give more detail:

```text
Read CUSTOM-OUTPUTS-AND-LOCAL-MODS.md and inspect the source matching my installed
Pulse Weaver version. I want this local modification: [describe the change].
Keep existing Stages, Looks, settings, connected accounts and output behaviour.
Use configuration if it can achieve the result. If code is needed, identify all
affected paths, make the smallest coherent change, and build an isolated runtime.
Run relevant local regression checks and demonstrate the behaviour before copying
it into my installation. Keep backups and a rollback procedure. I may currently
be streaming: [yes/no]. Wait for outputs to stop before updating the main app.
Do not publish a release or include my configuration/credentials in build assets.
Explain which files changed and any limitations.
```

For an additional platform, append:

```text
I want an additional independent output to [platform], using [manual ingest URL
and stream key / platform account connection]. It should support [landscape /
portrait / both if the platform accepts independent streams], appear in Outputs
and Manage stages, and have independent start/stop/status controls. Keep Twitch,
YouTube, Kick and recording working. Start with manual RTMP/RTMPS output if the
platform permits it. I need chat/account integration [yes/no; features].
Never repurpose my existing platform's stored key or OAuth registration.
```

Codex should check the platform's current official ingestion documentation for
access requirements, URL format, supported codecs, resolution, frame rate and
bitrate. A platform accepting one stream does not imply that it permits two
independent landscape/portrait broadcasts.

## Configuration or code?

| Request | Likely scope |
| --- | --- |
| Another composition or animation endpoint | Scene items, Stage storage, saved Looks |
| Route an existing supported platform to another scene | Manage stages and destination mode settings |
| Exclude music from a supported destination | Stage exclusions and existing audio routing |
| Change fit/crop, timings or overlay stacking | Look transforms and supported editor controls |
| Add a new platform choice to Outputs | Native frontend, real output lifecycle, routing and persistence |
| Add login, stream title editing or chat for that platform | Separate platform API/authentication integration |
| Change movement choreography or editor behaviour | Motion engine/frontend source and an isolated build |

The checked implementation has explicit Twitch, YouTube, Kick and recording
branches. Adding a key such as `facebook_horizontal` to Stage JSON or adding a
dropdown label alone does not implement a destination. OBS's underlying service
support also does not automatically expose that service in Pulse Weaver's own
operator controls. The primary output's readiness checks specifically recognize
Twitch; renaming its label would leave readiness and lifecycle handling wrong.

## Code map

All paths are relative to the repository root.

| Area | Start here | Responsibilities |
| --- | --- | --- |
| Operator UI and platform routes | `engine/obs-studio/frontend/widgets/OBSBasic_PulseWeaver.cpp` | Destination selectors; readiness and statistics; Stage assignments; output canvases; exclusions/audio mix allocation; YouTube secondary outputs; recording |
| Native frontend declarations | `engine/obs-studio/frontend/widgets/OBSBasic.hpp` | Persistent frontend members and lifecycle entry points; confirm declaration locations in your checkout |
| Kick output and local controller API | `engine/obs-studio/plugins/pulse-weaver-core/pulse-weaver-core.cpp` | Kick RTMP service/encoders/output and connection signals; controller request dispatch |
| Stage storage | `engine/obs-studio/shared/qt/PulseStageStorage.hpp` | Atomic saves and UUID-backed exclusion identities |
| Destination exclusions | `engine/obs-studio/shared/qt/PulseStageExclusions.hpp` | Available video/audio exclusions |
| Output scene synchronization | `engine/obs-studio/shared/qt/PulseOutputSceneSync.hpp` | Keep private destination scenes synchronized with source-item transforms and ordering |
| Output state and bitrate settings | `engine/obs-studio/shared/qt/PulseOutputActivity.hpp`, `PulseOutputBitrates.hpp` in the same directory | Runtime activity and validated per-route bitrate reads |
| Protected app credentials | `engine/obs-studio/shared/qt/PulseAppCredentials.hpp` | Local credential storage; keep secrets out of logs and source assets |
| Motion execution and persistence | `engine/obs-studio/plugins/pulse-weaver-core/pulse-motion-engine.cpp` and `.hpp` | Action capture/serialization, item resolution, private previews, movement and restore |
| Native portrait editing | `engine/obs-studio/frontend/widgets/PulseVerticalEditor.cpp` | Native portrait canvas/scene handling |
| Built-in wizard | `pulse-show-builder.cpp`, `pulse-show-templates.hpp` alongside the motion engine | Wizard and suggested compositions; custom configuration authoring can bypass them |
| Lumia destination events | `engine/obs-studio/shared/qt/PulseLumiaOutput.hpp` | Observe outputs and publish destination events |
| Lumia plugin | `integrations/lumia-pulseweaver` | Named Stage/Look controls, destination controls and event consumption |

Useful frontend search terms are `pulsePlatformOutputReady`,
`pulseOutputCanvasName`, `ApplyPulseWeaverStage`, `PulseWeaverLumiaDestination`
and `StartPulseWeaverSecondaryOutputs`. Check their callers as well as their
bodies. Provider lists occur in several functions; changing one list is
insufficient. The motion engine's show-creation paths also populate assignments
using explicit provider lists.

## A practical first extension: a generic manual destination

This is a proposed implementation plan, **not a feature already exposed by this
alpha**. A separately configured manual destination can be smaller than a full
account integration. Use a stable internal ID such as `custom1`, a user-visible
label, ingest URL and protected stream key. An ordinary label can name the
service; it must not imply that account login or chat support exists.

### 1. Settings and availability

- Persist the destination label, enabled/mode state, canvas route, bitrate,
  audio choice, ingest URL and protected key in the correct portable profile.
  Existing profiles should start with the new destination OFF.
- Validate the server scheme/path against the transport the platform supports.
  Use RTMPS where supported. Redact keys, authentication query parameters and
  endpoint credentials from logs and error messages.
- Define readiness from valid configuration. Treat connecting, live, stopping,
  reconnecting and failed as distinct runtime states.
- Add a destination selector/control and useful diagnostics. Include it in the
  Go Live summary only when enabled and ready.

### 2. A real output lifecycle

For RTMP/RTMPS, the existing Kick and YouTube code illustrates libobs
`rtmp_custom` services and `rtmp_output` outputs. Inspect the current encoder
helpers and attach video to the selected **output canvas**, plus audio to the
appropriate mix. Give each owned output, service and encoder a distinct name.

Handle start/stop signals, connection failures, retries, stop while connecting,
encoder/service ownership and cleanup. Read bitrate choices at output creation;
do not mutate a running encoder. A start request should report LIVE only when
the output has actually started. Stop and failure cleanup should be repeatable
and leave other platforms running.

If two routes are supported, manage two actual outputs and their states. Retain
a working landscape output if its portrait companion fails. Do not claim Dual
support without implementing both transport and status paths.

### 3. Stage routing and animated scenes

Add provider/canvas assignment keys following the installed schema, for example
`custom1_horizontal` and, if supported, `custom1_vertical`. Integrate them into
Manage stages, Stage application, canvas creation/release and previews. Preserve
existing provider assignments and UUID-backed exclusions.

Route to the selected scene for each destination, apply that Stage's transitions
and keep output scene copies synchronized during native source movement.
Excluding a source for one destination must not hide it for every destination.
Keep all existing stored Stage/Look identities intact during migration.

### 4. Audio and control surfaces

Pulse Weaver's checked output code selects from six OBS audio mixes. Account for
mix allocation and sharing; an extra destination with independent exclusions
can need another mix. Reject an impossible configuration visibly instead of
silently reusing an incompatible mix. Preserve Twitch's existing stream/VOD
track configuration and other outputs' music exclusions.

Add independent start/stop and statistics. Update the authenticated operator API
and Lumia provider handling if the new destination should be controllable there.
Keep existing route IDs and plugin identity compatible. Output support, account
sign-in, chat receive/send and moderation are separate capabilities.

### 5. Verification before installing

Ask Codex to demonstrate the following with local checks and an isolated copy:

- Old configuration loads with the new destination OFF and without credential
  or Stage changes. Save/reload preserves each new choice.
- Missing configuration and start failure have useful messages and clean up
  resources. Repeated start/stop and stop during connection do not duplicate
  outputs or leave an orphan encoder.
- Landscape/portrait selection, Stage transitions and Look movement reach the
  correct output canvas, including private copies with exclusions.
- Audio reaches the selected output, exclusions remain isolated, and alert sound
  is not duplicated. Unsupported mix combinations are reported.
- Existing Twitch, YouTube, Kick, recording, combined chat, hotkeys and Lumia
  controls keep their behaviour. Ending one destination preserves the others.
- Status labels and per-output uptime/bitrate follow real output events.

Relevant existing regression areas include `tests/StageStorage`,
`StageExclusions`, `StageTransitionReentry`, `OutputActivity`, `OutputUptime`,
`MotionAudit`, `RuntimeSafety`, `KickRuntime`, `TwitchRuntime` and
`tests/LumiaPlugin.test.cjs`. Choose checks for the code actually changed. Use
the repository's build requirements; these suites are not all standalone scripts.
The user chooses when to perform any real-platform broadcast check.

## Build and keep a rollback copy

Follow [BUILD.md](../BUILD.md), inspect the checkout's CMake presets/dependencies,
and build into a separate directory. For a normal Studio customization, keep
`PULSEWEAVER_MOTION_PREVIEW=OFF`; the historic Motion Preview flag changes build
identity and should not be used just to obtain an isolated test profile.

The frontend target is `obs-studio`; the plugin target is `pulse-weaver-core`.
Frontend changes affect `PulseWeaverCore.exe`; plugin changes affect
`obs-plugins/64bit/pulse-weaver-core.dll`. Use a compatible complete runtime,
including matching dependencies and plugin data, rather than copying unrelated
DLLs from different versions. Test with separate portable configuration, API
ports and shortcuts before updating the main installation.

After outputs stop, close the main app gracefully, back up its runtime and saved
configuration, then apply only the verified runtime changes. Preserve the
installed account registration files when compatible; never insert developer
credentials or another user's signed-in state. Verify the app reopens and the
old show still runs. A later official updater may overwrite local code changes;
keep the source patch, build identity and rollback instructions.

For redistributing a modified build, review [LICENSE](../LICENSE),
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and the packaging policy in
[BUILD.md](../BUILD.md). Publishing a build is a separate decision. Ship clean
runtime files and corresponding source as required, without your personal
profile, provider URLs, account tokens, stream keys or recovery backups.
