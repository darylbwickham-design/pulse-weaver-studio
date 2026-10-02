# Pulse Weaver 1.14.2 experimental alpha 2

The show builder can now use existing Stage compositions as a reference.
Choose a name and reference Stages to create a separate editable show retaining
all saved looks, layers, camera crops, independent portrait and landscape
framing, movement durations, output assignments, sound exclusions and Stage
transitions. Source settings and nested group contents remain shared.

The builder rejects source/Stage naming collisions, validates reference layer
identities and rolls back failed creations. Windows Stage catalogue saving and
the movement preview's starting-look selection were corrected during real UI
checks. Edit, New look and Duplicate look controls remain available in Control.

Includes alpha 1's shared decoded stinger frames and the 1.14.1 installer.
Lumia plugin **1.4.1**, the latest current plugin, is included in the installer
under `integrations/lumia` and is also available as a separate release asset.
Import the `.lumiaplugin` file into Lumia Stream if needed. The channel packaging
script now bundles its current Lumia plugin into every future channel installer
and rejects mismatched plugin manifest/package versions.

Enable **Include experimental alpha builds** under **Studio → Updates**, stop
outputs and check for updates. Setup backs up the existing installation and
profile before upgrading.

Verification: native build succeeded; 118 offline motion preservation checks
passed. A separate local reference installation produced five Stages and 13
looks, with all 336 saved transform targets and durations matching their
references. Real UI checks covered Starting, Hangout focus swaps, Celebration,
Ending portrait framing, restart persistence, movement preview and applying a
saved look to both local program canvases. No personal scenes or profiles are
included in the installer.
