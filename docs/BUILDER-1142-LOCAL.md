# Local 1.14.2 builder candidate

The isolated portable installation is `Pulse Weaver 1.14 Clean Test` under the
user's local Programs directory. Open it with the desktop shortcut
**Pulse Weaver 1.14.2 Builder Test**. Its API port is 18775.

## First pass

In Show Control → Control → Build my show, use **Use existing Stage
compositions**. The **Builder Reference** collection contains the main
installation's five PW Stages and 13 saved looks. Name the generated show
**Builder Test**. Creating it retains each reference Stage's layers, separate
landscape and portrait transforms, visibility, crops, saved movement durations,
output assignments, sound exclusions and transition settings.

Generated scenes and looks have new identities. Source objects are reused within
the test collection: changing a source's settings affects both reference and
generated uses. Changing a top-level item's position changes that composition.
Nested group contents are shared; independent group editing is not covered by
this pass. No account credentials were copied from the main installation.

Select a look to edit in Control, save edits, then use **Run saved look live** to
rehearse locally. **Preview from** and **Play look movement** rehearse movement
on the design canvases. Check Hangout focus swaps, Starting's camera crop,
Intermission/Ending portrait framing and Celebration artwork first.

## Verification

- Plugin rebuilt successfully on Windows.
- 118 offline motion preservation checks passed, including paired transforms,
  source identity validation, collision rejection, reference preservation and
  failed creation rollback.
- Real wizard testing caught an open catalogue read handle preventing Windows
  replacement during save. The handle is now closed before the wizard opens.
- The test's camera feeds were visible after closing the regular installation.
- Created Builder Test through the real wizard: five Stages and 13 saved looks.
  All 336 reference targets matched generated transforms; durations matched.
- Checked Starting, both Hangout focus layouts, Celebration artwork and Ending
  portrait framing in the running app. The generated show survived restart.
- Fixed starting-look selection to use the saved look's Stage when the hidden
  Stage editor still has an old selection. Rehearsed the Hangout swap and
  confirmed Run saved look live reaches both local program canvases.
- Public releases and remote branches were not updated.

The previous test installation and the exact main reference snapshot are under
`artifacts/builder-1142/backup-20261002-175228`. Provisioning verified that the
main reference files were unchanged. The main runtime was not replaced.

The copied Spotify browser widget currently reports HTTP 400, and the second
facecam occasionally reports MJPEG decode errors. These should be considered
separately from saved composition or movement fidelity.
