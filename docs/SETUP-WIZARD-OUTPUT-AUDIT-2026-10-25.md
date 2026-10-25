# Generated-show quality audit — baseline unstable 4

Read-only production-source audit. Baseline: `da59b025f9b7c77fd2234d3747c2e3db79afe09e` (2.0.0-unstable.4). No installed application was opened and no product files were changed. A small QtCore executable calls the actual `PulseShow::layout` function; `production-layout-results.json` records the output, and `main.cpp` records all assumptions. This establishes geometry, not real capture/provider/audio readiness or a novice usability result.

## Judgment

The previous tests establish significant preservation and interface correctness. They do not establish useful default production layouts. The catalogue repeats the same roles/compositions under activity-specific labels, and the default Full show route requires external artwork for ordinary starting/away/ending screens. A novice encounters the Stage/Look model before choosing their content or equipment. The friend's immediate confusion is consistent with those design demands, but a source audit cannot identify their precise misunderstanding.

## Concrete findings

### 1. Ten non-artwork choices reduce to four duplicated role/geometry families

The compiled probe calls the current production header using exactly the wizard's default options: recommended composition, top-left, 30% portrait top area, fit main/support. With the same inputs bound to common roles, both canvas geometries are identical within each family:

| Identical family | Current choices |
|---|---|
| Main content plus small presenter | Chat/Content reaction; Game/Gameplay; Projects/Activity |
| Presenter and supporting content | Chat/Explain or showcase; Game/Commentary; Projects/Explain the project |
| Single full feed | Chat/Just chatting; Celebration/Celebration reaction |
| Alternative feed plus presenter | Game/Secondary view; Projects/Detail view |

Source: `pulse-show-templates.hpp:110–136`. Probe field `identicalNonGraphicRoleGeometryGroups` is generated from actual role/rectangle/fill values, excluding artwork because different artwork content may legitimately distinguish two Looks. Different inputs across Stages can also justify the same composition; this is not a claim that every existing show is a visual duplicate. It is a problem for task-first setup that binds one camera and one content source globally: the repeated entries then produce redundant choices.

**Recommendation:** offer meaningful operating views based on the available content: Content + camera, Content only, Camera only, and a genuine demonstration/detail arrangement when a distinct additional input exists. Separate show phases such as Starting, Live, Away and Ending from layout variants inside the live phase. Keep old IDs resolvable for existing API/draft compatibility rather than silently deleting saved material.

### 2. Landscape differences collapse on portrait

`layout()` handles portrait before the corner, rail and split branches (`pulse-show-templates.hpp:241–247`). All two-feed arrangements therefore use one top/bottom pattern. Main Feed A gets the top portion; otherwise its support gets the top. `FlexibleSetup` sets every portrait proportion to 30% (`pulse-setup-flexible.hpp:726`), and `framing()` always serializes that value (`:192–196`). This also overrides any per-Look divider metadata.

Consequently Content reaction and Explain, Gameplay and Commentary, and Activity and Explain project have the same portrait role geometry within their respective Stage. A title saying the presenter leads does not change their 30% top allocation.

**Recommendation:** portrait needs its own functional composition logic. Preserve complete game/slide content by fitting it; crop only a camera when explicitly chosen, with a clear subject framing choice. Do not assume a universal platform-safe rectangle or crop a game's HUD automatically.

### 3. The diagrams conceal fit/letterbox consequences

The current diagram fills the abstract panel rectangles with color (`pulse-setup-flexible.hpp:1354–1365`); it does not show the rendered inner-fit extent of a real source. Backend `position()` uses `OBS_BOUNDS_SCALE_INNER` when crop is off (`pulse-show-builder.cpp:98–132`). All generic feed crop checkboxes start unchecked (`pulse-setup-flexible.hpp:727–728`). Thus a panel-looking-full illustration can lead to a sparse real portrait output.

The probe's explicit geometric assumption is that every non-artwork feed is 1920×1080, fitted against a plain background. A full-feed portrait fills only **31.640625%** of 1080×1920 pixels. The default paired portrait layouts occupy about **60.085%** in total. These figures are mathematical consequences under those assumptions, not measured app performance or a live screenshot. Actual transparency, background artwork, source aspect and cropping change the result.

**Recommendation:** preview the true fit/crop extent using known source dimensions, or clearly identify the assumed aspect ratio for uncreated feeds. Use labelled example images for camera/content/detail so a user can understand the composition before linking actual hardware. Assess generated source transforms and rendered fixtures, not just widget containment.

### 4. Full show makes an integration a prerequisite for basic phases

Full show selects Starting, Chat, Game, Celebration and Intermission (`pulse-setup-wizard.cpp:271–275,346–348`). It selects Countdown, Just chatting, Gameplay, Celebration reaction, BRB and Ending (`pulse-setup-flexible.hpp:700–704`). This yields **13 assignments: 9 required and 4 optional**. Six required assignments are separate landscape/portrait artwork for Countdown, BRB and Ending. The required graphic questions accept browser sources only (`:861–864`), and the draft always declares `overlayProvider: "lumia"` (`:1319`). A generic fully usable starting/away/ending route is therefore unavailable to a newcomer without importing external graphics.

**Recommendation:** task-first native starting/away/ending cards should be usable immediately. Let Lumia/browser artwork be an explicit customization. Existing Countdown/Welcome native HTML support is available in `createThemedShow`, but requires a carefully scoped mixed policy; simply removing the Lumia flag would weaken other alert validation.

### 5. Repeated source assignment encourages repeated capture creation

In Full show, Feed A appears in five Stage groups. The backend can reuse one non-browser source across all Stages and both canvases (`createSource` caches by definition key, `pulse-show-builder.cpp:1941–1955`). The UI, however, treats each group's feeds as independent and requires repeated manual Use existing decisions (`pulse-setup-flexible.hpp:933`). It even defaults Projects/Feed B to a window capture although a making/tabletop use case often needs a camera (`:136–140`).

**Recommendation:** assign the camera, main content and optional close-up once. Use those common bindings by default in all relevant phases. Allow a clearly labelled per-view override for the legitimate different-source case. Do not automatically merge unrelated sources by URL or hardware name. Preserve source UUID identity and source property/audio settings.

### 6. Three alert layers exist, but they are disguised as six overlay questions

The model already allows exactly **three ordered slots per canvas** (`pulse-setup-flexible.hpp:995`; backend `pulse-show-builder.cpp:1825–1833`). They are hidden behind an initially off **Add shared layers** checkbox (`flexible:807–811`), then appear as separate Landscape overlay 1/2/3 and Portrait overlay 1/2/3 assignments. The default UI name does not make the requested alert capability obvious.

Layers are ordered bottom-to-top, fitted full canvas, locked, and scoped to selected Looks; copied saved layouts are supported too (`builder:2081–2110,2221–2246`). The same existing/drafted source is reused across selected Stages. Duplicate overlapping instances of the same source are rejected (`flexible:1105–1114`, `builder:1825–1865`). New browser sources get audio mixers 0 while existing audio is retained (`builder:1989–2029`).

**Recommendation:** expose **Alerts and overlays — up to 3 layers** as three logical layers, each with paired landscape/portrait choices in one focused screen. Explain one responsive URL versus a separate portrait layout; do not silently reuse incompatible 16:9 browser viewports. Leave unused layers empty. Do not auto-enable audio on both canvas copies. Offer a direct route to the real audio controls after creation; do not label URL assignment as tested delivery.

Although copy says Lumia, the URL policy actually permits ordinary external HTTP/HTTPS browser providers (`pulse-source-canvas.hpp:38–57`). Labels can say “browser alert link” and use Lumia as an example, preserving the explicit native-overlay exclusion where required.

## Native card implementation constraints

- `builtin_graphic` already supports `text`, `textByLook`, `minutes`, per-Look and per-canvas caching (`builder:1941–2029`). Existing tests check distinct Countdown/Welcome text (`MotionAudit:1999+`). It generates local transparent HTML and a one-second JS timer, even for non-countdown cards.
- Current Lumia-only draft validation explicitly rejects that kind (`builder:1540–1547`), as do the source questions. A new mixed presentation mode must maintain browser-only, orientation and scope checks for alert inputs, and retain the old strict Lumia-only contract for existing requests.
- A generated static PNG represented by an `image_source` avoids extra CEF/timer work for static slates. The backend permits `image_source` for generic feeds, but a proper generated slate definition needs transactional file creation and cleanup, unique naming, matching canvas sizes and validation. `text_gdiplus` is not an accepted current new-source kind.
- Keep the original engine transaction, name-collision guards, source reuse, source-audio policies, saved-scene cloning and Stage transition/stinger lifecycle. Existing created shows should not be rebuilt or pruned by the new setup.

## Gaps in current tests and proposed acceptance cases

The pairwise distinctness and in-bounds geometry checks at `MotionAudit:1480–1504` iterate the legacy `catalogue()`, not `setupCatalogue()`. The flexible catalogue test at `:1948–1980` proves all entries can create with neutral source slots and checks selected transforms. It does not assert useful visual coverage, content preservation, meaningful preset differences, or a novice's independent task completion.

Add output-focused cases using inert labelled test images and test sources, with no hardware or provider calls:

| Task | Required output-level assertions |
|---|---|
| Game only | One content input reused across phases/canvases; no camera question or fake camera panel; all four HUD corners retained by fit; no unnecessary duplicate Looks. |
| Game + camera | Content-only, camera-only and combined operating views differ in source visibility/framing; all are reachable through saved actions; one capture UUID per chosen device. |
| Conversation | One camera input; sensible portrait crop/fit choice shown accurately; no celebration duplicate created by default; no game/source integration requirement. |
| Tutorial/screenshare | Entire 16:9 or ultrawide screen retained; camera does not obscure labelled important content by an unexplained fixed inset; fitted content extent represented in the preview. |
| Tabletop/making | Main activity camera plus optional presenter/close-up; detail view actually uses a distinct input or deliberate crop, not an alternate name for the same geometry. |
| Guests/call | Existing call-window/browser capture plus local camera; do not claim to create or connect a guest-call service; two-source composition and audio check are explicit. |
| Basic phases without integrations | Generated Starting/BRB/Ending images contain the configured text at both canvas resolutions; no unresolved placeholder or required external browser link; static cards use no CEF or polling timer. |
| Alerts 0/1/2/3 | Exactly the chosen logical layer count per format; deterministic Z order; intended Look scope; no more than one scene item per assigned slot; no extra new browser instances across Stages; existing audio unchanged, new audio policy explicit. |
| Back/revise/cancel | Changing capabilities removes inactive questions/views from the draft, preserves answers if re-enabled, and does not mutate existing sources or save partial output. |

Render fixtures for 16:9, 4:3, native portrait and ultrawide inputs, then inspect actual generated frame images in addition to transform assertions. An in-bounds rectangle alone is insufficient. Usability validation should ask a novice to produce one concrete show without coaching, record first-page comprehension and wrong turns, and distinguish independent success from the agent's guided walkthrough. Academic/task research is with the other research agents; the code findings here do not substitute for those sources.
