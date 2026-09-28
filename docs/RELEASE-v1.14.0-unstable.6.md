# Pulse Weaver 1.14.0 unstable 6

This build supersedes unstable 5 for the experimental Studio interface.

## Fix in unstable 6

- The audio source routing matrix now displays the operator's underlying six-track assignments when a stage has temporarily allocated tracks for Twitch, YouTube, Kick or recording exclusions. The temporary stage assignments are cleared before saving audio devices or track membership, so they are not written into the scene collection by those settings actions.
- Saving these settings is synchronous. Stage-specific exclusions are rebuilt when the stage is selected again or an output starts.

## Included from unstable 4 and 5

Fresh setups open the guided show builder. Control offers explicit main/supporting source and two-canvas layout choices. Settings exposes audio devices, track names, Twitch VOD routing, local monitoring, recording folder and format, video size and FPS, destination bitrates, appearance and Lumia port. The existing streaming, chat, credentials, scenes, port and Lumia integration remain in place.

This is still an unstable preview. Advanced encoders and custom FFmpeg recording use OBS advanced options. Provider source exclusions remain in Manage stages. Live account, migrated-profile and visual click-through checks have not been performed on the packaged installer.

## Verification

The native frontend and motion plugin compiled, and installer payload, layout, language and profile-adoption self-checks passed. Live release selection was checked so regular installs remain on 1.13.3 and unstable opt-ins receive this build. No installed Pulse Weaver instance or live output was changed.
