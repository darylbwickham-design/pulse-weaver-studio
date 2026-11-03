# PulseWeaver Stream Studio

The Lumia Stream companion plugin for **Pulse Weaver** streaming software. Use Lumia reactions, commands and LumiCon controls to operate your existing show. Pulse Weaver's on-screen keypad can also trigger Lumia alerts.

**Plugin version: 1.5.1.** Use Pulse Weaver **2.0 for Windows** for the complete feature set. Tested with **Lumia Stream 9.3.4**; the manifest declares compatibility with Lumia 9.x. Both applications must run on the same computer.

## Start here

1. Install [Pulse Weaver 2.0 for Windows](https://lumi-con.com/pulse-weaver/) and create or open your show.
2. Install **PulseWeaver Stream Studio** from Lumia's plugin marketplace when available, or import `PulseWeaver-Lumia-1.5.1.lumiaplugin` in Lumia's plugin manager.
3. Enable the plugin and open Pulse Weaver. Leave **Connection token** and **Custom Pulse Weaver configuration path** blank for a standard installation. **Pulse Weaver port** must match the app's local API port; a new standard installation uses **18755**.
4. Add an **Activate Stage** action to a Lumia reaction and select an existing Stage. Run it to check the connection without starting a broadcast.

Follow the [complete tutorial](TUTORIAL.md) for keypad buttons, temporary Looks, output controls and troubleshooting. The shorter [connection help](SETUP.md) and [action reference](ACTIONS.md) also appear inside Lumia.

## What it controls

- Existing Stages and saved Looks or motion actions, including remembering and undoing a temporary change.
- The configured show, individual Twitch/Kick/YouTube destinations, and recording.
- Existing scene-item visibility, source mute/volume, and supported media playback.
- Fifteen numbered keypad alerts and automatic Stage, source, media, transition and output alerts. **All alerts default to Off**; enable the ones you configure.

Configure scenes, Looks, transitions, destinations and audio routing in Pulse Weaver. This companion does not create scenes or sources, edit transforms or filters, replace media files, or configure accounts. It does not require an OBS WebSocket connection.

## Updating from Motion Preview

The public name is **PulseWeaver Stream Studio**, but its ID remains **`pulseweavermotionpreview`**. Existing action/alert IDs, field keys and the variable prefix are unchanged. Lumia retains existing saved alert switches during an update. Keep your saved port and connection settings.

The standard package connects to the regular Pulse Weaver installation. The developer-only isolated Motion Preview package uses the same ID and is an alternative connection profile, not a second plugin to install alongside it.

## Connection and support

Communication uses an authenticated connection to `127.0.0.1` on your computer. The plugin reads the standard installation's local connection token automatically and receives pushed events rather than polling while idle. Closing Pulse Weaver disconnects it; reopening the app reconnects without replaying earlier alerts. Keep connection tokens private.

For help with setup or other support questions, [contact us on Discord](https://discord.gg/MQ6pAN9Q). Use the [website for downloads](https://lumi-con.com/pulse-weaver/) and [GitHub for source code and issue tracking](https://github.com/darylbwickham-design/pulse-weaver-studio). See the [changelog](CHANGELOG.md). The plugin is licensed under [MIT](LICENSE).
