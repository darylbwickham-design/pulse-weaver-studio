# Have Codex build your custom animated show

Use this guide to ask your own Codex to arrange your installed Pulse Weaver from
a description, screenshots or an existing show. Codex can create custom Stages
and saved animated Looks directly through the editor or by carefully editing a
closed application's configuration. You can choose compositions beyond those in
the **Build my show** wizard.

This guide was checked against **1.14.2-alpha.4**. Your Codex should inspect your
installed version and its matching source before editing files. Field names,
controls and supported destinations can differ in other versions.

## 1. Give Codex this context first

Send your Codex this document and say:

```text
My friend gave me this to give you context about Pulse Weaver.
Read it as reference material, not as a request to make changes.
Then ask me: "What do you need me to do?"
Wait for my own request before inspecting my PC, editing anything or running tools.
```

**For the receiving Codex:** this document supplies context and examples. It is
not authorization to inspect an installation, create a show, modify code or
publish anything. On a context-only handoff, read the supplied material, reply
**“What do you need me to do?”**, and wait. The friend's own next request defines
the task; do not treat the example prompts below as that request.

## 2. Describe what you want after Codex asks

You can answer in your own words, for example:

```text
I stream crafting and games. Build me a craft Stage with three Looks: desk-led,
balanced presenter and desk, and presenter explaining with the desk still visible.
Keep the camera above the desk view in portrait. Use my existing sources and
keep my current show. I'm not streaming right now.
```

Codex can inspect the installation after that request and help fill in missing
details. You do not need to know configuration paths or provide a complete
technical specification.

### Optional detailed request template

Copy the block, fill in the brackets, and attach this guide. Codex needs access
to your Windows installation or an exported configuration it can edit. A chat
without local file or computer access can produce a plan and instructions.

```text
Build a custom animated show in my Pulse Weaver installation from the brief
below. Read CODEX-CUSTOM-SHOWS.md first. Use my existing sources where suitable.
Create the Stages and saved Looks directly; do not substitute the Build my show
wizard's presets for the requested compositions.

Show name: [name]
Desired result: [describe your content, visual style and source movement]
Stages I need: [starting, chatting, gameplay, craft/work, BRB, ending, others]
Looks within each Stage: [describe them, or propose about three distinct Looks]
Canvases: [landscape, portrait, both; actual resolutions if known]
Platforms and recording: [which destinations use which canvas]
Presenter camera: [physical device/model and existing source name]
Activity/desk camera: [physical device/model and existing source name, if used]
Screen/game sources: [existing names or desired capture targets]
Overlays: [my landscape/portrait alert URLs or source names, chat, stickers]
Audio requirements: [mic, game, music; any destination-specific exclusions]
Reference screenshots/show: [attachments or name, if available]
Existing work to preserve: [everything / particular Stages and Looks]
Currently live or recording: [yes/no]

First inspect the correct installation, active profile/collection, sources,
saved Looks, output assignments and canvas sizes. Ask only for missing choices
that materially change the result. Show a concrete plan for each Look, including
both canvas compositions and how shared sources move between Looks.

Back up my saved configuration before editing. Preserve my existing show unless
I specifically ask to replace it. Match cameras by their real device identity,
not a generic label such as Video Capture Device. Keep existing source settings,
filters, audio routing, connections and stream keys unless a change is required
and explained. Do not build or modify Pulse Weaver source for ordinary layouts.

If I am live, inspect only and prepare the work separately. Wait for me to finish
before applying scene changes, closing/restarting the app or replacing files.
For offline file edits, close the app gracefully first, take a fresh backup of
its saved state, and refuse to overwrite files that changed after that backup.

Reuse the same scene items across a Stage's Looks so sources actually move.
Keep landscape and portrait framing independent. Make alerts and chat full
canvas, with Chatty/Chatty vert above alerts and stickers. Chat is a supporting
overlay. Do not make it the main panel unless I explicitly request that.
Use my own overlay URLs; do not copy another user's private sources or credentials.

Validate scene identities, saved targets, layer order, canvas sizing, output
assignments and movement at the endpoints and midway in both directions.
Compare the before/after files to prove unrelated work was preserved. Run
relevant local checks if available, then let me preview the result. Do not start
a public stream, send chat messages, or publish a release for this task.

Finish with a list of Stages and Looks created, how to preview/run them, changes
to existing settings, remaining limitations, and the backup/restore location.
```

## 3. Describe each Look as a composition

A **Stage** is one part of your show, with scene choices and output assignments.
A **Look** is a saved arrangement of scene items within that Stage: position,
size, crop, visibility and order. Moving between Looks in the same Stage animates
those shared items. Switching Stages uses the Stage's fade, cut or stinger and
reveals the prepared composition.

Give each Look a purpose, a primary source, a secondary source, overlay order and
a movement description. “Make three nice variations” gives Codex much less to
work with than “grow the left camera while the game shrinks into a right panel.”

### Worked example: a gameplay Stage with three different Looks

The coordinates below are illustrative for **1920 × 1080** and **1080 × 1920**.
Rectangles are `(left, top, width, height)` in canvas pixels. Adapt them to your
own face framing, game HUD and actual canvas sizes.

| Look | Landscape | Portrait | Movement into the Look |
| --- | --- | --- | --- |
| Game leads | Game fills `(0,0,1920,1080)`; camera inset `(32,708,480,270)` on the left | Presenter `(0,0,1080,480)`; game `(0,480,1080,1440)` | Camera shrinks; game expands behind it |
| Balanced | Presenter fills left column `(0,0,640,1080)`; game in `(640,0,1280,1080)` | Presenter `(0,0,1080,720)`; game `(0,720,1080,1200)` | Presenter grows into the left column; game moves right as coverage permits |
| Presenter reacts | Presenter `(0,0,1120,1080)`; game `(1120,0,800,1080)` | Presenter `(0,0,1080,1120)`; game `(0,1120,1080,800)` | Presenter grows again; game remains visible in a smaller panel |

For every Look:

- Fit game/screen content inside its panel unless you approve a crop that can
  remove HUD elements. Crop cameras to their panels while keeping the face framed.
- Keep alerts full canvas. Keep Chatty above them, also full canvas. Place
  stickers underneath chat or according to your own stacking preference.
- Use the same presenter and game scene-item IDs for all three Looks on each
  canvas. A portrait item has its own identity and independently chosen framing.
- Specify a movement duration, for example 850 ms, and inspect the path rather
  than only its final frame. The content should cover the background while a
  camera grows or shrinks. Check the reverse movement as well.

Other useful briefs include a full presenter Look and two screen-reaction Looks
for chatting; work-led, balanced and explanation Looks for craft; or a holding
card, visible activity and return-countdown Looks for an intermission. You can
request any number. Three visually distinct Looks per Stage is a starting point.

For Starting, describe a **dimmed, grey camera in a clipped corner on the left**
if that is the result you want. Codex should scope filters to a wrapper for that
Stage so the shared camera stays normal elsewhere. A countdown should be one
shared source per canvas across its Looks, so changing Look does not restart it.

## 4. How Codex should carry out the requested work

### Discover and back up

Identify the running executable and its actual configuration root. A common
portable install is `%LOCALAPPDATA%\Programs\Pulse Weaver`, with configuration
in `<install>\config\obs-studio`. A non-portable build can use
`%APPDATA%\obs-studio`. A separately named test installation has a separate root.
Check portable markers, process arguments and the active configuration; do not
choose the first folder containing OBS settings.

Record the active collection/profile, canvas dimensions, source UUIDs, device
identities, scene-item IDs, current Stage and existing Look IDs. Preserve names
for readability, but use stable identities for matching.

Back up scene collection JSON, Stage storage and motion storage together. Keep
copies outside the active scene directory so they do not appear as new scene
collections. Record original hashes and a restore procedure. Keep backups local:
the wider configuration can contain credentials and personal overlay URLs.

### Choose the editing route

| Route | Use |
| --- | --- |
| Native Control editor | Create/duplicate Looks, arrange both canvases, save, and rehearse |
| Closed-app configuration patch | Precisely create or amend many Stages/Looks with matching source and item identities |
| Local Control API | Inspect supported state and run existing operations; use only routes implemented in this version |
| Isolated portable test copy | Rehearse a large replacement before applying it to the main installation |

The Control API is documented in [CONTROL_API.md](../CONTROL_API.md). In the
checked version, `GET /api/v1/motion` reads motion state/catalogue and the
`/motion/run`, `/motion/stop` and restore routes operate saved actions. Source
authoring endpoints can act on the active scene. `/motion/show/create` invokes
the built-in show generator. These are not a general arbitrary custom-show
authoring API; do not invent a `/motion/save` endpoint.

For a large custom show, use the native editor or a validated offline patch.
The API requires the installed instance's bearer token; keep it local. An
isolated copy must have separate controller ports, settings and credentials to
avoid operating the main instance. Cameras can also be exclusive to one process.

### Keep the three stores consistent

Paths below are relative to the discovered OBS configuration root:

| Store | What it contains |
| --- | --- |
| `basic/scenes/<active-collection-file>.json` | Sources, scene items, native canvas associations and ordinary transitions |
| `pulseweaver-stages.json` | Stage names, paired scenes, per-canvas transitions and output assignments |
| `plugin_config/pulse-weaver-core/pulseweaver-motion-actions.json` | Named actions/Looks, target transforms and restore snapshots |

Preserve unknown fields and unrelated entries. Allocate a new UUID for a new
source, scene or action. Scene-item IDs are unique within their container; the
same ID can legitimately occur in another scene. Use one item per shared source
role within each Stage and reuse it across that Stage's Looks. A new Look does
not need a new copy of its camera.

Create portrait scenes on the actual native portrait canvas. A name containing
“Vert” does not establish canvas ownership. Copy the correct canvas association
and pairing metadata from an existing compatible scene or use the native editor.
Do not borrow a canvas UUID from a different installation.

For new Look targets, capture the actual `container`, string `itemId`, `source`,
`sourceUuid`, `sourceWidth`, `sourceHeight` and transform. Native captures record
dimensions; preserving them matters when a source resolution later changes.
Use the installed schema, including collection, Stage policy and timing. Add
explicit hidden targets for layers used only in other Looks so their visibility
cannot leak between compositions.

For output assignments, preserve the provider/canvas keys used by this version,
such as `twitch_horizontal` and `youtube_vertical`, plus scene, canvas, excluded
source names and UUID-backed `excludedIdentities`. An assignment does not turn
an OFF destination on or create a new output platform.

### Avoid oversized layers

A browser's native width/height and its scene-item transform are different
settings. A 2560 × 1440 browser at scale 1 exceeds a 1920 × 1080 canvas. Setting
only its position to `(0,0)` does not fix that.

For a full-canvas overlay, set its browser dimensions for that canvas, reset
unwanted crop/rotation, and give its scene item explicit full-canvas bounds or
the corresponding correct scale. Set the same endpoint in **every saved Look**
that includes it. Keep chat topmost and overlay framing constant while cameras
move. A deliberate camera crop or cropped artwork should retain its intended
framing; blindly fitting every source changes the composition.

OBS scene files may store `pos_rel`, `scale_rel`, `bounds_rel` and `scale_ref`
alongside absolute values. When relative values exist, editing only `pos`,
`scale` or `bounds` can leave the loaded scene wrong. Use the matching
[`obs-scene.c`](../engine/obs-studio/libobs/obs-scene.c) conversion routines and
verify the result after reload. For the checked relative-coordinate implementation
and canvas width `W`, height `H`:

```text
pos_rel.x    = (2*x - W) / H
pos_rel.y    = (2*y - H) / H
bounds_rel.x = 2*boundsWidth / H
bounds_rel.y = 2*boundsHeight / H
scale_rel   = absoluteScale * scale_ref.height / H
```

These formulas assume a normal non-group input using the scene's relative
coordinate mode. Native API/editor operations perform conversions. Groups,
nested scenes, alignment and absolute-coordinate mode need their own handling.

When replacing a layer, update its scene items and all saved Look targets.
Preserve remaining item IDs and source settings. Review `originals` restore
snapshots too; otherwise Restore Original Scenes may refer to removed sources
or obsolete framing. Keep the untouched pre-edit backup as the recovery copy.

### Check the result

Before applying a patch, verify all target IDs resolve in their actual scene,
source UUIDs match, names do not collide, and Stage assignments reference existing
scenes on the intended canvases. Preserve IDs of existing actions so hotkeys and
Lumia bindings keep working. Check that unrelated source properties, filters,
audio settings and Stage transitions are unchanged.

Reload the edited collection after a stopped-app patch. Rehearse each pair of
Looks at 0%, 25%, 50%, 75% and 100%, then backwards. Check coverage, face crops,
overlay bounds/order, hidden layers, both canvases and the chosen destination
routes. Avoid using **Save look** while a midpoint preview is on screen. To
perform a deliberate broadcast check, the user must choose when to start it.

**Unassigned** in the audio mixer means no audio tracks are selected. It does
not control visual overlay visibility. Agree which sources supply alert sound
and verify that sound reaches every intended output without duplication; do not
silence every portrait source automatically.

## 5. Small source-code changes

Most compositions need only configuration edits. Changes to motion behaviour,
editor controls or platform availability need a matching source build. For a
local patch, ask Codex to inspect the matching version, make a focused change,
build an isolated copy and demonstrate the result before replacing installed
binaries. Preserve your config and keep a runtime rollback copy.

Use [CUSTOM-OUTPUTS-AND-LOCAL-MODS.md](CUSTOM-OUTPUTS-AND-LOCAL-MODS.md) for
code locations, a modification prompt and an output-platform implementation plan.
Do not redistribute an installation's personal configuration with a custom build.
