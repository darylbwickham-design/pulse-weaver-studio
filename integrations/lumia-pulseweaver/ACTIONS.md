# Pulse Weaver actions and alerts

Add these **PulseWeaver Stream Studio** actions to a Lumia reaction, command or LumiCon control. Choices come from the show currently open in Pulse Weaver.

## Stages and Looks

- **Activate Stage:** choose an existing Stage and use its saved transition and routing.
- **Next Stage / Previous Stage:** step through the existing Stage order.
- **Run Stage Look / Motion Action:** choose a saved action in the **Motion action** field. A different Stage uses its configured transition; a Look within the same Stage uses its saved motion.
- **Remember Current Stage / Look:** pin a return point before several temporary changes. Wait for a running motion to finish first.
- **Undo Previous Stage / Look:** return to the remembered presentation when available; stop an unfinished motion first.

Stage and Look actions remember their preceding presentation by default. A successful Undo consumes that return point. It is one return point, not a history of edits. Remembered state clears when the plugin unloads or its connection settings change. Several temporary changes within one Stage require a saved Look the plugin has already observed completing to restore that Look exactly. Without a remembered destination, Undo uses Pulse Weaver's native motion-layout restore when available.

## Streaming and recording

- **Start Show / End Show:** start the configured Twitch, Kick and YouTube destinations, or stop all streaming destinations. Recording remains independent.
- **Start Platform / Stop Platform:** operate only the selected Twitch, Kick or YouTube destination. Starting uses its mode already selected in Pulse Weaver; configure a destination set to Off in the app first.
- **Start Recording / Stop Recording:** use the recording route already selected in Pulse Weaver, independently of streaming.

Start controls can begin a real broadcast. Configure accounts, output modes and recording in Pulse Weaver first. A successful output action confirms the requested live/stopped state. If a whole-show start partially fails, other destinations can remain live: check Pulse Weaver before retrying.

## Sources and media

- **Source Visibility:** choose a specific **Scene / Source** item, then Show, Hide or Toggle visibility.
- **Source Mute:** choose an **Audio source**, then Mute, Unmute or Toggle mute.
- **Source Volume:** set an **Audio source** from **0–100%**.
- **Media Playback:** choose a **Media source**, then Play / Resume, Pause, Restart, Stop, Next or Previous. Available operations depend on the source.
- **Reconnect:** reconnect to the local Pulse Weaver installation after it opens or connection settings are corrected.
- **Open Pulse Weaver Manual:** while the plugin is connected, choose the browser reading edition or the page-numbered PDF print edition. Both files remain in the installed plugin package for offline reading.

Visibility affects the selected scene item. Mute and volume affect that shared source wherever it is used. Platform audio exclusions and monitoring stay configured in Pulse Weaver. Internal routing copies are not offered as editable scene items.

## Alerts

**All 51 plugin alerts default to Off.** Enable only the alerts for which you configure reactions. Existing saved switches are retained on update.

**Keypad 01–Keypad 15** correspond to the same numbered button slots in Pulse Weaver. Each provides `buttonIndex` (1–15), `buttonName` and `message` (the button name). Renaming a button preserves its binding.

The other 36 alerts report Stage changes, source visibility/audio changes, media playback, transition begin/end, and streaming/recording state changes. Output alerts include `platform`, `output` and `state`; dual outputs may produce separate events. Not every field is populated for every alert. Reconnecting refreshes the current state without replaying historical alerts.

## Variables and results

Use Lumia's variable picker for **active_stage**, **previous_stage**, **previous_look**, **motion_status**, **motion_action**, **stream_status**, **recording_status**, **twitch_status**, **kick_status**, **youtube_status**, and **last_result**. An empty previous Look means no saved Look has been identified for that return point.

The retained namespace is **`pulseweavermotionpreview_`**, despite the new public name. Action results include `pulseweavermotionpreview_result` and, on success, `pulseweavermotionpreview_active_stage`. Failed actions report an error and stop the current Lumia action chain. A Look request being accepted means it was queued; it does not confirm animation completion.

## Downloads and help

- [Read the complete offline manual](manual/Pulse-Weaver-Studio-Nerds-Introduction.html) or [open its print edition](manual/Pulse-Weaver-Studio-Nerds-Introduction.pdf)
- [Download Pulse Weaver from the website](https://lumi-con.com/pulse-weaver/)
- [Source code and issues on GitHub](https://github.com/darylbwickham-design/pulse-weaver-studio)
- [Contact support on Discord](https://discord.gg/MQ6pAN9Q)
