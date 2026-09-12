# Native module SDK

Pulse Weaver extensions are ordinary OBS native modules loaded inside `PulseWeaverCore.exe`. They may publish normalized events and register automation actions without depending on OBS WebSocket or private frontend widgets.

Publish an event through the global proc handler:

```text
void pulseweaver_publish(in string platform, in string type, in string json)
```

Listen for the corresponding global signal:

```text
void pulseweaver_event(string platform, string type, string json)
```

Register an Action-workspace command:

```text
void pulseweaver_register_action(
  in string module_id,
  in string module_name,
  in string action_id,
  in string action_name,
  in string proc_name)
```

The registered `proc_name` is called with input strings `value` and `json`. It must return boolean `success` and may return string `message`. See `engine/obs-studio/plugins/pulse-weaver-sample-clock` for a compiled example. Add native modules to the OBS plugin build and distribute their DLL/resources beneath `obs-plugins/64bit` and `data/obs-plugins`.
