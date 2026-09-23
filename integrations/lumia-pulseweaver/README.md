# Pulse Weaver for Lumia Stream — 1.2.0

Version 1.2.0 updates the existing Pulse Weaver Lumia plugin. It keeps the `pulseweavercontrol` ID so current Lumia reactions and LumiCon alert bindings remain attached to the same plugin. Import `PulseWeaver-Lumia-1.2.0.lumiaplugin` over the existing plugin after installing the Pulse Weaver build that provides native motion.

Version 1.2.0 adds the native motion catalogue, Run Motion, Stop + Restore and Restore Last controls for Lumia reactions and LumiCon buttons. These controls invoke the same guarded runner as Pulse Weaver's Motion page. It also retains the existing show, Stage, source, audio, media and recording controls.

## Operating controls

- Start/end the whole configured show, or start/stop Twitch, Kick and YouTube individually.
- Activate an existing Stage, next Stage or previous Stage.
- Run a named native motion action, stop and restore it, or restore the last completed layout.
- Show/hide/toggle an existing scene item.
- Mute/unmute/toggle and set volume (0–100%) on existing audio sources.
- Play, pause, restart, stop, next and previous on existing controllable media sources.
- Start/stop the recording mode already selected in Pulse Weaver.
- Reconnect after opening Pulse Weaver or changing its connection settings.

Platform start uses the saved horizontal/vertical/dual mode. A platform set to Off must be configured in Pulse Weaver first. Source mute and volume affect that source everywhere it is used; they do not edit platform audio exclusions. Visibility selects a specific scene/group item. Generated private routing copies are not exposed as editable scene items.

There are **no** scene/source creation or deletion actions, transforms, filters, file/URL replacement, configuration editing, or raw request actions. Lumia-labelled requests to the general editing API are rejected. The connection token is still the local API token, not a separately sandboxed credential; do not distribute it.

## Alerts and feedback

All 36 alerts default to **Off** in Lumia. Enable individual alerts when you configure their reactions. Existing saved alert switches are preserved by Lumia during an update; already-enabled alerts must be switched off once in Lumia. No additional plugin master switch is required.

36 alert definitions cover Stage changes, source shown/hidden/muted/unmuted/volume, media playback, transition begin/end, and platform/recording starting/live/stopping/stopped/failed (plus platform reconnecting). Output alerts include the specific output: a dual YouTube or recording route can produce separate output events. A platform's status confirms both YouTube outputs for Dual; partial failure is reported as failure rather than full success.

Existing status variables remain, with motion, Twitch, Kick and YouTube status variables. Action return variables keep Lumia's existing `pulseweavercontrol_` prefix. A start command only reports success after live state is observed; a timeout stops the Lumia action chain and tells you to check the app. A failed whole-show start may leave successful destinations running, and its error says so.

Events arrive over one authenticated local event stream. There is no idle status polling. Source lists update when their catalogue changes, variables/options update only when changed, and the connection sends a small heartbeat every 20 seconds while subscribed. Reconnection retries back off and stop after eight failed attempts; use Reconnect or re-enable the plugin afterwards. Reconnecting refreshes the current state without replaying historical alerts. Normal successful actions do not create toast spam or Event List entries.

## Connection

Port 18755 remains the default for the normal Pulse Weaver installation. To test the isolated Motion Preview build, change this existing plugin's port to 18765; token discovery then prefers `%LOCALAPPDATA%/Programs/Pulse Weaver Motion Preview`. Switching the port back to 18755 reconnects it to the normal installation. For a custom portable installation, set the full `config/obs-studio/plugin_config/pulse-weaver-core/pulse-weaver.ini` path or enter its connection token.

Do not use Lumia's generic OBS connection to bypass these operating controls. Build scenes, Stages and routing inside Pulse Weaver.

## Verification

The Node test suite exercises pushed events, dynamic lists, platform isolation, completion/failure handling and rejection of unsupported actions. The native smoke test runs against an isolated development runtime, operates existing test sources, verifies their events and checks forbidden routes. No live platform broadcasts are started by these tests.
