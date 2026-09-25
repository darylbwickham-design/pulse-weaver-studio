# Native Motion checkpoint — 25 September 2026

The isolated `native-motion-engine` preview is running on `PW Gameplay · Gameplay`. The installed Pulse Weaver release and its regular profile were not changed. No stream or recording was started.

The show has five Stages and 13 saved looks. Show Control has a large landscape and portrait editor, Stage and Look selectors, and a Layers rail. Corner dragging supports proportional resize, Shift stretch, and Alt crop. Stage changes use the existing Stinger on both canvases. The separate Lumia Motion Preview 1.3.1 plugin retains manifest ID `pulseweavermotionpreview` and port 18765.

Portrait Gameplay fits `second facecam` across the full 1080 px width at the top (608 px high) and centres Game Capture in the lower 1312 px. Landscape Gameplay puts the facecam in the top right and the physical printer camera in the bottom left over Game Capture. Both canvases were checked visually with Minecraft running.

The previous Gameplay action was rejected because it referenced the obsolete `whodatpal` layer. Obsolete emote, Whodatpal/WDP, Palworld audio and other unused layers were removed from the new Stages and their saved actions. All 13 Stage looks were then accepted through the same restricted Motion Preview endpoint used by Lumia. Chatty, vert chatty, captions, cameras, Game Capture, Lumia audio/overlays and Spotify game-capture audio remain. Original imported scenes and source definitions remain preserved for Restore Original Scenes.

Every Stage excludes `spotifysound` and `spotify` from YouTube and Kick on both canvases; Twitch exclusions stay empty. `spotifysound` uses mixer mask 249, leaving VOD track 2 off in the active profile. Celebration Thank you was visually checked on both canvases. The desktop control tool cannot hold modifier keys while dragging, so Alt/Shift drag gestures still need a hands-on check.

The active profile and local recovery snapshots are under `artifacts/completed-show` and `artifacts/profile-backups`. See `artifacts/completed-show/SHOW-GUIDE.md` for stage descriptions, cue IDs and recovery controls.
