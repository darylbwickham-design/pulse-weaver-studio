# Pulse Weaver native Control API

While the native studio is running, it exposes a loopback-only API at `http://127.0.0.1:18755/api/v1`. The bearer token is shown in **Action → API / Plugins** and is generated in this portable copy's isolated settings.

Every request requires `Authorization: Bearer <token>`. The server listens only on loopback and does not enable browser CORS.

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/health` | Engine and output status; reports that WebSocket is not required |
| GET | `/scenes` | Real horizontal scenes and active scene |
| POST | `/scene?name=Gameplay` | Activate a scene |
| POST | `/scene/create?name=Gameplay` | Create a real scene |
| GET | `/sources` | Scene items, kinds, visibility, lock and transforms |
| POST | `/source/create?name=Card&kind=color_source_v3` | Add an OBS input to the active scene; JSON body may contain `settings` |
| POST | `/source/visibility?name=Camera` | Set `{"visible":true}` on an active-scene item |
| POST | `/source/transform?name=Camera` | Set any of `x`, `y`, `scaleX`, `scaleY`, `rotation` |
| GET | `/overlays` | Managed overlay catalogue and live URLs |
| POST | `/overlay/trigger?name=Follower%20Spotlight` | Trigger an overlay with optional event JSON |
| POST | `/overlay/add-to-scene?name=Follower%20Spotlight` | Add a managed browser source |
| GET | `/automations` | Persisted command-flow rows |
| POST | `/automation` | Add a validated automation row from a JSON body |
| GET | `/modules` | Registered native module actions |
| POST | `/event?platform=game&type=boss.defeated` | Publish normalized JSON event data |
| POST | `/stream/start`, `/stream/stop` | Control native streaming |
| POST | `/recording/start`, `/recording/stop` | Control native recording |
| POST | `/app/exit` | Request normal process-owning shutdown |

The official Lumia plugin uses the restricted `/api/v1/lumia` surface. These routes require the normal bearer token and `X-Pulse-Weaver-Client: lumia-plugin`:

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/lumia/state` | Stage names, active Stage, destination routes and live/recording state |
| GET | `/lumia/events` | Authenticated SSE snapshot then live operator events; 20-second heartbeat, no historical replay |
| POST | `/lumia/destination/start?platform=kick`, `/lumia/destination/stop?platform=kick` | Independently operate a configured destination without changing its saved output mode |
| POST | `/lumia/source?source=UUID&action=mute` | Existing-source mute/unmute/toggle_mute, volume (0–100), media play/pause/restart/stop_media/next_media/previous_media |
| POST | `/lumia/source?source=SCENE_UUID&item=ITEM_ID&action=hide` | Existing scene/group item show/hide/toggle_visibility |
| POST | `/lumia/stage?name=Starting` | Apply a Stage through Show Control |
| POST | `/lumia/stage/next`, `/lumia/stage/previous` | Cycle Stages |
| POST | `/lumia/go-live` | Run Pulse Weaver's validated Go Live flow with Lumia confirmation already supplied |
| POST | `/lumia/end-stream` | Stop every Pulse Weaver live output |
| POST | `/lumia/record/start`, `/lumia/record/stop` | Use Pulse Weaver's selected recording route |

From 1.11.42, state includes `operatorApi: 2`, source/item UUIDs and IDs, and per-output event states. The official Lumia plugin 1.1.0 uses these restricted controls and SSE events; it exposes no authoring or raw-request action. Requests labelled `X-Pulse-Weaver-Client: lumia-plugin` cannot access the general API routes. The shared bearer token is not a separate per-client security credential.

Example automation body:

```json
{
  "event": "twitch.channel.raid",
  "conditionField": "viewers",
  "conditionOperator": "Greater or equal",
  "conditionValue": "10",
  "delayMs": 250,
  "action": "Switch scene",
  "target": "Raid"
}
```

Routes are additive within `/api/v1`; integrations should not depend on the legacy WPF API or OBS private internals.
