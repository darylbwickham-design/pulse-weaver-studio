# Pulse Weaver 2.0.1

This Windows update fixes the cramped **Manage Stages** layout introduced in the 2.0 workspace.

## Manage Stages

- Scene selectors use the full width of each platform cell. Landscape and portrait labels and source-exclusion controls sit above the scene names.
- Wider opened selectors display scene names clearly, with full-name tooltips on the selected scene.
- Transition type and duration are stacked in one box for **16:9**, with a matching box for **9:16**. Separate duration columns no longer consume scene space.
- Fade exposes its editable duration. Stingers explain that their timing comes from the stinger; Cut is shown as an instant change.
- Consistent column widths, spacing and compact controls replace the stretched transition selectors. Smaller windows scroll horizontally instead of compressing all scene choices.
- New and duplicated table rows finish creating their controls before table-change notifications can save them.

## Lumia companion help

Includes **PulseWeaver Stream Studio 1.5.1**, a documentation and help-text update. It adds a complete tutorial with website downloads, GitHub and Discord support links, and clarifies connection settings, keypad alerts and temporary Looks. The plugin runtime, permanent `pulseweavermotionpreview` ID, existing bindings and alert defaults are unchanged from 1.5.0.

## Downloads and updating

- [Windows x64 installer](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v2.0.1/PulseWeaver-Setup-2.0.1.exe)
- [Lumia companion plugin 1.5.1](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/download/v2.0.1/PulseWeaver-Lumia-1.5.1.lumiaplugin)
- [Website](https://lumi-con.com/pulse-weaver/) · [GitHub release](https://github.com/darylbwickham-design/pulse-weaver-studio/releases/tag/v2.0.1) · [Discord support](https://discord.gg/MQ6pAN9Q)

Use **Studio → Updates**, or close Pulse Weaver and run the installer over the existing installation. Setup backs up the application and settings. Lite, the isolated 2 Preview and the Mac preview remain separate editions.

## Verification

The native frontend was rebuilt and the actual Manage Stages dialog was checked in an isolated instance with inert sources. Scene choices and Fade, Cut and Stinger controls were inspected. A horizontal Fade-duration change was checked against the saved Stage data: portrait duration, other transition selections, source exclusions and the other normalized Stage settings were retained. No live broadcast or user capture devices were used for that check.
