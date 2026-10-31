# Connect PulseWeaver Stream Studio

This plugin connects **Lumia Stream and LumiCon to Pulse Weaver streaming software**. It controls your existing show and receives events from Pulse Weaver. Build your scenes, Stages, Looks, audio routing and output destinations in Pulse Weaver first.

## Install and connect

1. Install Pulse Weaver and open it on this computer.
2. Import the `.lumiaplugin` package into Lumia Stream, then enable **PulseWeaver Stream Studio**.
3. The standard Windows installation connects automatically on port **18755**. Keep an existing custom port; it must match Pulse Weaver's local API settings.
4. If you use a portable installation, provide its `pulse-weaver.ini` configuration path in this plugin's settings. A connection token can also be supplied manually. Keep that token private.

The plugin connects only to this computer. It does not require an OBS WebSocket connection. Closing Pulse Weaver disconnects the plugin; reopening it reconnects without replaying earlier alerts.

## Set up the on-screen keypad

The keypad requires the Pulse Weaver build that includes **Chat → keypad** and plugin **1.5.0 or newer**.

1. In Pulse Weaver, click the keypad icon beside **Chat**.
2. Click **Add button**, give it a name, and choose an included icon or your own image.
3. Note the button's Lumia alert name, such as **Keypad 01**.
4. In Lumia, open **PulseWeaver Stream Studio** alerts and find that exact numbered alert. Add your scene change, Stage/Look action, lighting or other reactions.
5. Click the Pulse Weaver button to run the alert. Use **Edit buttons** to change it without firing it.

There are at most 15 buttons in three columns. Renaming a button or changing its icon keeps its numbered alert binding. Clearing a button frees that number for reuse, so update any old Lumia reactions before reassigning the slot.

The new keypad alerts default to on because they run only when you explicitly press a button. Existing alert settings and existing action IDs are preserved. If nothing runs, check that the numbered Lumia alert is enabled and has reactions configured. Pulse Weaver's “Sent to Lumia” message confirms event hand-off; it does not confirm that a scene transition has finished.

## Troubleshooting

- **Disconnected:** start Pulse Weaver, enable this plugin, and check the port/configuration path. Upgrade to plugin 1.5.0 to enable the keypad.
- **Button does nothing:** leave Edit buttons mode, check the matching Keypad alert and its reactions, and inspect the plugin's `last_result` variable.
- **Wrong scene:** update that numbered alert's reactions in Lumia. Pulse Weaver does not infer a scene from the button's name.
- **Returning after an update:** the plugin keeps its manifest ID `pulseweavercontrol`, so existing Lumia/LumiCon bindings remain attached.
