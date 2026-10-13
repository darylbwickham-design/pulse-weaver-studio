# Pulse Weaver 1.14.4-alpha.4 — built-in Altron Command Centre

Altron now starts inside Pulse Weaver rather than a separate Node bridge. Studio → Tools → Altron Command Centre creates or reuses the dedicated show, adds its new Stage without replacing existing entries, and offers Enter/In/Out/Reset tests and configurable arrival/departure sounds. Existing Lumia commands and Lumi-Con actions use the same localhost protocol. The original camera and its transforms are retained, with background-removed camera and alerts reused as references. Old dedicated Lua filters remain disabled backups.

The native controller owns teleport timing, destination switching and actual camera-transform observation. Lumia retains private overlay artwork, backgrounds, head placement, TTS/headphone monitoring and per-location placement controls. A previous standalone bridge must be closed once if it occupies the local control port; the app retries binding automatically.

The alpha.2 configuration-preserving installer is retained. Validation includes in-process native control with real libobs/audio, scoped helper and Stage setup, overlap guards, camera observation, actual app startup/dialog/shutdown and GPU/audio output. This is a local preview and has not been published to the updater.
