# 1.14 unstable workflow coverage

| Task | Current UI | Authoritative state | Remaining work |
|---|---|---|---|
| First show with new or existing video sources | Settings → Start here → Build my show | OBS sources/scenes, stage catalogue, motion store | Device previews and a reversible whole-show edit flow |
| Desktop and microphone setup | Wizard → Set up sound; Settings → Sound and recording | OBS output audio sources in scene collection | More than one input/output channel in wizard |
| Alert, landscape chat, portrait chat, starting/BRB/ending graphics | Wizard → Add graphics and overlays | OBS browser/media sources and scene items | In-wizard preview of external browser URLs |
| Landscape and portrait framing | Wizard framing choices; Control previews and placement | OBS scene item transform, motion look | More portrait layout templates |
| Main/inset source choice and swap | Control → Choose sources + layout… / Swap focus | Motion draft and saved look | Source substitution when one canvas lacks it |
| Game capture window | Show → Game window and gear | Game Capture source settings and saved source UUID | Dedicated empty-state illustration |
| Accounts and stream metadata | Action/Connections | Existing OAuth and provider settings | Consistent Twitch/Kick title/category form |
| Go live, recording, destination routing | Show and Manage stages | Existing OBS outputs and stage assignments | Consolidate routing editor into Show |
| YouTube quota and chat | Show chat | Existing native quota policy and gRPC helper | Live account validation after quota resets |
| Source exclusions and Twitch VOD | Manage stages; Settings → Twitch VOD track and Audio source routing | Stage assignments, audio source mixer masks, active profile | Combine provider exclusions and track assignments in one view |
| Recording folder and format | Settings → Sound and recording | Active OBS profile | Custom FFmpeg output still uses native advanced dialog |
| Base/output size and FPS | Settings → Sources and canvas | Active OBS profile | More presets and layout migration when resolution changes |
| Track names, local monitoring and six-track source routing | Settings → Sound and recording | Active OBS profile and scene collection | Live UI validation and clearer provider labels |
| Advanced encoders | Advanced OBS options | Native OBS settings | Bring the safe presets into Pulse Weaver with provider-aware validation |
| Lumia port, appearance, updates and recovery | Settings → Automation and app; Studio → Updates | Existing INI/theme/updater and backup system | Bring backup list into Settings page |

The 1.14 interface is still an unstable showcase. The remaining-work column is part of the release gate for a later regular version; the current build must not be described as a complete settings migration.
