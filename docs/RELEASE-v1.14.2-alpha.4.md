# Pulse Weaver 1.14.2 experimental alpha 4 — themed show builder

This alpha introduces the themed **Build my show** wizard for user feedback.
Open **Show Control → Control → Build my show…** to create a new editable show.

## What to try

- Pick Starting, Viewer focus, Gameplay focus, Intermission, Craft focus and/or
  Wrap-up. Each selected theme becomes one Stage, with three suggested Looks.
- Select the Looks you want, then assign existing sources or create new sources
  for each Stage. Reuse assignments across themes when appropriate.
- Add up to three full-canvas overlay layers per landscape and portrait canvas.
  Use Lumia Stream's **Chatty / Chatty vert** in layer 3, above alerts and stickers,
  visible in every Look. Chat remains an overlay in all suggested compositions.
- Review both canvases, adjust panel placement, fit/crop, crop position and
  movement duration, then rehearse movement before creating the show.
- Try the three Looks within a Stage: shared cameras and content move between
  small insets, larger columns and presenter-led compositions. Portrait layouts
  place the presenter above the content with different divider heights.

Starting uses a dimmed, grey camera with a clipped corner on the left. Its
camera treatment and countdown are shared across the three Looks. The camera's
own filters are preserved. Look movement keeps content behind an expanding left
camera column, preventing a blank gap; Control previews use the same movement
code as live Looks. Wizard scrolling and saving a Look into its intended Stage
were also corrected.

The reference-show copy workflow is still available. New show creation validates
source identities and naming collisions and rolls back failed creation. Sources
and nested group contents remain shared; changing their properties affects other
uses. Moving a generated scene item changes that composition.

## Update and feedback

Enable **Studio → Updates → Include experimental alpha builds**, stop outputs and
check for updates. Setup backs up the existing installation and profile before
upgrading. The installer bundles the current **Lumia plugin 1.4.1** under
`integrations/lumia`, also supplied as a separate release asset.

Please [report feedback](https://github.com/darylbwickham-design/pulse-weaver-studio/issues)
with the Stage/theme, Look, landscape or portrait canvas, source types, what you
expected and what happened. Screenshots of the wizard and generated composition
are especially useful. This is an experimental alpha for refining the builder.

Verification: native build succeeded; **1,454 offline motion preservation checks**
passed, including overlay visibility/order, exact saved transforms, movement
coverage, backwards scrubbing and preview isolation. A separate local test show
created six Stages with 18 Looks. Real camera UI checks covered the generated
compositions and movement. No personal scenes, sources, provider URLs or settings
are included in the installer.

Release checks also passed for update-channel selection, 69 backup/recovery
regressions, all 16 Lumia plugin tests, installer payload/migration/language/layout
checks, and an alpha 3 → alpha 4 → full restore using a synthetic profile. The
upgrade proof preserved all four configuration files and 2,234 original files
byte-for-byte. The packaged builder matches the tested module, and the bundled
Lumia package matches the separate release asset.
