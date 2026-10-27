# Pulse Weaver 2.0.0-unstable.7

This unstable revision makes camera movement and portrait editing corrections
part of Pulse Weaver's normal behaviour.

## Animated Looks

- New and existing layout Looks use smooth reflow unless their saved data
  explicitly selects the legacy compatibility style.
- Cameras move and resize independently, with gentle acceleration and varied
  entrances. Shared views keep their identity as the composition changes.
- Coordinated handoffs keep a main view covering its region while the next
  view takes over. Alerts and chat retain their framing.
- Transitions between bounds modes retain the saved visual endpoints without
  changing sizing interpretation on the final frame.
- New layout movement defaults to 1,050 ms. Explicit saved timings remain
  authoritative. Camera close-ups retain their existing behaviour.
- Hidden camera layers remain hidden at the end of a move; this revision does
  not keep cameras visible offscreen or change audio routing to animate them.

## Camera workspace

- Scene availability follows actual scene/group references rather than names.
  Portrait-only helpers are available in Portrait, while shared helpers are
  available in both tabs.
- Output canvas ownership remains unchanged. Selecting a helper in Portrait
  changes the editor selection, not the program channel.
- Custom-sized scenes use their complete native dimensions in the preview,
  editing tools and size label. Changing scene size refreshes preview scaling.
- Existing source identities, filters, transforms, Stage routes, monitoring and
  stinger definitions are preserved.

## Distribution and validation

Both the regular unstable installer and the isolated 2 Preview installer are
produced from the same source revision and verified native runtime. They retain
their separate product identities and updater families. Stable releases are
unchanged. Installers and public source archives do not include user shows,
camera settings, credentials, logs or recovery backups.

Release validation covers native motion geometry and rendered intermediate
frames, coverage during camera handoffs, preview/runtime consistency, legacy
compatibility and camera close-ups, portrait helper availability, custom scene
dimensions, existing-show retention with inert inputs, source/runtime
provenance, and installer update/rollback checks.
