# PulseWeaver Stream Studio: from connection to your first button

This plugin connects **Lumia Stream and LumiCon** to **Pulse Weaver**, the streaming application. Lumia can change your presentation and operate its controls; Pulse Weaver's on-screen keypad can trigger your Lumia reactions.

This guide covers plugin **1.5.2** and **Pulse Weaver 2.0 for Windows**. Lumia Stream **9.3.4** was used for testing; the plugin manifest declares Lumia 9.x compatibility. Run both applications on the same computer.

For the full book, including the OBS comparison, Stage/Look/scene examples, release history, glossary and index, open **Studio → Help → Open Pulse Weaver Manual** in Pulse Weaver Studio, or use the manual button in **About Pulse Weaver**. The printable manual belongs to the studio installation and is not included in this Lumia plugin.

## 1. Have a show ready

Download [Pulse Weaver for Windows from the website](https://lumi-con.com/pulse-weaver/) and open or build your show. You need at least one Stage for the first example. Two Stages make it easier to see the change. The project is also [available on GitHub](https://github.com/darylbwickham-design/pulse-weaver-studio). For setup questions or support, [contact us on Discord](https://discord.gg/MQ6pAN9Q).

A **Stage** is a presentation you can activate, such as Starting or Gameplay, with its configured landscape/portrait scenes and routing. A **Look** is a saved presentation or motion action, such as a different camera arrangement. A **source** is an existing camera, microphone, browser, media clip or other input. Choose your own names; these examples are not templates installed by this plugin.

Create your Stages and Looks, connect streaming accounts, and configure transitions and audio routing in Pulse Weaver. The plugin operates what you have already configured.

## 2. Install and connect

Install **PulseWeaver Stream Studio** from Lumia's plugin marketplace when it is available. For a manual installation, use Lumia's plugin manager to import **PulseWeaver-Lumia-1.5.2.lumiaplugin**. Enable the plugin.

If updating **Pulse Weaver Motion Preview**, import the new package as an update. Its permanent ID is still **`pulseweavermotionpreview`**, so existing action/alert bindings and variable names remain attached. Keep your saved connection settings. The renamed plugin connects to the regular Pulse Weaver installation by default.

Open Pulse Weaver and check the plugin's connection settings:

| Setting in Lumia | What to enter |
| --- | --- |
| **Pulse Weaver port** | Match Pulse Weaver's local API port. A new standard installation uses **18755**. Keep a custom port if you already use one. |
| **Connection token** | Leave blank for automatic detection from the standard installation. Only enter a token manually when needed. |
| **Custom Pulse Weaver configuration path** | Leave blank for a standard installation. For another location, enter the full path to its `pulse-weaver.ini` file. |

Find the app's port under **Settings → Automation and app → Local automation API**. The plugin does not discover a custom port automatically. Changing the API port in Pulse Weaver takes effect after restarting the app; use its existing port while streaming.

For a normal installation, the automatically read configuration file is:

```text
%LOCALAPPDATA%\Programs\Pulse Weaver\config\obs-studio\plugin_config\pulse-weaver-core\pulse-weaver.ini
```

For a portable or custom installation, enter the corresponding file under that installation's `config` folder and match its port separately. The configuration-path field expects the **file**, not its containing folder. Normally the token can still be read from that file, so the token field stays blank.

Save changed plugin settings. It reconnects automatically; **Reconnect** can request an immediate attempt. Pulse Weaver must be open. No OBS WebSocket connection or OBS password is needed. Keep connection tokens private.

## 3. Try a Stage change

1. In Lumia, open a reaction or command you want to use for the test.
2. Add **PulseWeaver Stream Studio → Activate Stage**.
3. In **Stage**, choose a Stage from your open show. Leave **Remember previous stage / look** at **Yes**.
4. Run the reaction. Pulse Weaver should activate that Stage using its configured transition.

This checks the connection without starting a broadcast. If Stage choices are empty, check the connection and that you have created Stages in the open show. An ordinary scene does not automatically become a Stage.

## 4. Put the action on Pulse Weaver's keypad

1. In Pulse Weaver's **Show** page, click the keypad icon beside **Chat**. It replaces the chat view with your button panel.
2. Select **Add button**. Give it a short name, such as **Gameplay**, and choose an included icon or **Use my image…**. Select **Save**.
3. Note the alert shown in the editor, such as **Keypad 01**. The numbered slot connects the two apps.
4. In Lumia's **PulseWeaver Stream Studio** alerts, find **Keypad 01**. Add **Activate Stage** and choose your Gameplay Stage, or choose another reaction you want the button to run.
5. **Enable Keypad 01 in Lumia.** All keypad alerts default to **Off**. Configuring a reaction alone does not enable its alert.
6. Return to Pulse Weaver and press the button. If the panel says **Done editing**, click that first to leave edit mode.

The Stage should change. The button can also run Lumia lighting, sound or other configured reactions. Its name and icon are labels; they do not choose a Stage automatically.

You can add up to **15 buttons in three columns**, with five rows when all slots are used. Buttons remain square; a short panel can scroll. Click the keypad icon again to return to chat.

Use **Edit buttons** to change buttons without triggering them. **Done editing** returns to normal operation. Renaming or replacing an icon keeps the same numbered alert. To remove a button, select **Clear button**, then **Save**. Reusing the freed slot reuses its alert too: change its old Lumia reactions before assigning a different job.

Custom icons can be PNG, JPG/JPEG, WebP or BMP. Use an image under 16 MB, no more than 8192 pixels on either side, and at most 32 million pixels in total; Pulse Weaver stores a small local copy. **Sent to Lumia** confirms hand-off, not that every reaction or transition has finished. Presses made while disconnected are not replayed later.

## 5. Run a temporary Look and return

For one temporary change, create a Lumia reaction containing:

1. **Run Stage Look / Motion Action** → choose your saved action in **Motion action**. Keep **Remember previous stage / look** at **Yes**.
2. A Lumia delay long enough for that action and the time you want to hold it. Use your action's timings; there is no universal delay.
3. **Undo Previous Stage / Look**.

For several temporary Stage/Look changes with one return point, put **Remember Current Stage / Look** at the beginning and **Undo Previous Stage / Look** at the end. Wait for a running motion to finish before remembering a return point.

Undo is one return point, not a full edit history. One temporary Look within the same Stage can restore its native baseline. Several changes within the same Stage require a saved starting Look that the plugin has already observed completing to restore that Look exactly. Otherwise it reports that the earlier layout is unavailable. A successful Undo consumes the return point; plugin unload or connection-setting changes clear it. Existing **Restore Last Motion** bindings use the renamed Undo action.

## 6. Add output or audio controls

Choose output modes and connect accounts in Pulse Weaver before adding streaming controls. **Start Show** can start a real broadcast to all configured destinations. **Start Platform** uses the selected Twitch, Kick or YouTube mode already configured in the app; configure a destination set to **Off** there first. **End Show** stops streaming destinations while recording remains independent.

**Start Recording / Stop Recording** use the recording route selected in Pulse Weaver. Configure an enabled recording mode before starting it.

**Source Mute** and **Source Volume** affect a shared source everywhere it is used. **Source Visibility** affects the particular scene item selected. Configure monitoring, copyright-audio exclusions and platform mixes in Pulse Weaver; these actions do not change that routing.

An output action reports success after observing the requested live/stopped state. If a multistream start only partially succeeds, successful destinations may remain live. Check Pulse Weaver's output status before retrying. A Look request being accepted means it was queued, not that its motion has finished.

## Troubleshooting

| What you see | What to check |
| --- | --- |
| **Disconnected** | Pulse Weaver is open, the plugin is enabled, and the ports match. For a nonstandard installation, supply its configuration file. Save corrections and use **Reconnect**. |
| **Connection failed: HTTP 401** | The app rejected the token. Check the configuration path. Clear an old manually entered token to use automatic detection. |
| **Empty Stage or Look choices** | Create Stages and saved actions in the open show, then reconnect. The plugin does not turn raw scenes into Stages or Looks. |
| **A keypad button does nothing** | Leave edit mode with **Done editing**. Enable the exact numbered alert in Lumia and give it reactions. Check **last_result** in the plugin's Lumia variables. |
| **The wrong Stage runs** | Edit the numbered alert's reactions in Lumia. Renaming the Pulse Weaver button does not change those reactions. |
| **Undo cannot recover the exact Look** | Start from a saved Look, let it complete while the plugin is connected, and remember that presentation before the temporary sequence. Undo does not record arbitrary edit history. |
| **Connection disappears when Pulse Weaver closes** | This is expected. Reopening the app reconnects automatically; old events and button presses are not replayed. |

Automatic Stage/source/output alerts also default to **Off**. Enable those you want and add reactions. Avoid making an unfiltered Stage Changed reaction change Stage again.

Use Lumia's variable picker to inspect **active_stage**, platform statuses and **last_result**. The legacy prefix **`pulseweavermotionpreview_`** intentionally remains compatible with existing commands. Plugin alerts do not request entries in Lumia's general Event List; check the actual reaction and result variable when testing.

## Further help

- [Action and alert reference](https://github.com/darylbwickham-design/pulse-weaver-studio/blob/main/integrations/lumia-pulseweaver/ACTIONS.md)
- [Plugin changelog](https://github.com/darylbwickham-design/pulse-weaver-studio/blob/main/integrations/lumia-pulseweaver/CHANGELOG.md)
- [Pulse Weaver downloads](https://lumi-con.com/pulse-weaver/)
- [Source and issue tracker](https://github.com/darylbwickham-design/pulse-weaver-studio)
- [Discord support and contact](https://discord.gg/MQ6pAN9Q)
