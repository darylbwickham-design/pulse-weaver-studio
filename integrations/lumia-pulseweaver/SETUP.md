# Connect to Pulse Weaver

Use **Pulse Weaver 2.0 for Windows** for all features, with Lumia Stream on the same computer. Tested with Lumia Stream **9.3.4**. Build your show in Pulse Weaver first; this plugin operates its existing Stages, Looks, sources and destinations.

The complete offline manual covers Pulse Weaver and OBS, Stages, Looks, scenes, streaming workflows, release history, a glossary and an index. It is bundled as a [browser reading edition](manual/Pulse-Weaver-Studio-Nerds-Introduction.html) and a [page-numbered PDF print edition](manual/Pulse-Weaver-Studio-Nerds-Introduction.pdf). With the plugin connected, run **Open Pulse Weaver Manual** in a Lumia reaction or command and choose an edition. The files remain in the installed plugin package for offline reading.

- [Download Pulse Weaver from the website](https://lumi-con.com/pulse-weaver/)
- [Pulse Weaver on GitHub](https://github.com/darylbwickham-design/pulse-weaver-studio)
- [Contact support on Discord](https://discord.gg/MQ6pAN9Q)

## Connect

1. Open Pulse Weaver and enable this plugin in Lumia.
2. Match **Pulse Weaver port** to the app's **Settings → Automation and app → Local automation API** port. A new standard installation uses **18755**. Keep an existing custom port when updating.
3. For a standard installation, leave **Connection token** and **Custom Pulse Weaver configuration path** blank. The token is read automatically. Save changed plugin settings; the plugin reconnects.
4. Once connected, add **Activate Stage** to a Lumia reaction, choose an existing Stage, and run it. Pulse Weaver should switch to that Stage. This check does not start streaming.

For a portable or nonstandard installation, enter the full path to its `config/obs-studio/plugin_config/pulse-weaver-core/pulse-weaver.ini` in **Custom Pulse Weaver configuration path**, and match its port separately. Only enter a token manually if needed; keep it private. No OBS WebSocket connection is required.

## Set up a keypad button

1. In Pulse Weaver, click the keypad icon beside **Chat**, then **Add button**.
2. Give the button a name, choose an included icon or **Use my image…**, and **Save**. Note the numbered alert shown in the editor, such as **Keypad 01**.
3. In Lumia's **PulseWeaver Stream Studio** alerts, open that exact numbered alert. Add reactions, such as **Activate Stage** with your chosen Stage, then **enable the alert**. Keypad alerts default to **Off**.
4. Click the Pulse Weaver button. If you are editing buttons, select **Done editing** first. Click the keypad icon again to return to chat.

There are up to **15 buttons in three columns**. Names and icons do not choose a Stage automatically: Lumia's reactions determine what happens. Renaming a button keeps its alert number. Clearing a button and saving makes that number reusable; update its old Lumia reactions before reusing it.

“Sent to Lumia” confirms hand-off, not that the reactions have completed. Earlier button presses are not replayed after reconnecting.

## If something does not work

- **Disconnected:** check that Pulse Weaver is open, the plugin is enabled, and the port matches. Automatic detection finds the standard installation; use the configuration-path field for another location. Run **Reconnect** after correcting settings.
- **Empty Stage or Look choices:** create them in the open Pulse Weaver show, then reconnect. A raw scene is not automatically a Stage or a saved Look.
- **Button does nothing:** select **Done editing**, check its numbered alert is enabled and has reactions, and read **last_result** in the plugin's Lumia variables.
- **Connection failed: HTTP 401:** the token was not accepted. Check the configuration path; clear a stale manually entered token to use automatic detection.
- **Updating from Motion Preview:** the name changes, but the ID stays `pulseweavermotionpreview`, preserving existing bindings and variable names.
