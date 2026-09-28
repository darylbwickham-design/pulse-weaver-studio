# Pulse Weaver 1.14.0 unstable 4

This preview makes the 1.14 interface changes visible in daily use and adds direct controls for common setup tasks.

## New in unstable 4

- A fresh setup opens at **Settings → Start here** with **Build my show**. Existing stage catalogues still open at Show.
- The show wizard now asks for desktop and microphone devices. **Keep current device** preserves an existing scene collection. Newly created device sources are removed if the show build fails.
- Control has **Choose sources + layout…**. Pick the large and supporting sources by name, then preview Main + corner, Side by side or Full screen main on both canvases. Undo or save the look. Existing overlays and each source's crop stay intact.
- Settings now exposes audio devices, Twitch VOD track routing and source membership, recording folder and format, base/output video size and frame rate, destination bitrates, appearance and the local Lumia port. Changes explain when a restart or new output start is required.
- The setup guide follows the actual screen labels.

## Retained behaviour

The Twitch routing, YouTube quota and streaming chat improvements from regular 1.13.3 remain included. Kick has a landscape output only. Credentials, scenes, saved ports and Lumia 1.4.0 actions are preserved by the installer backup/update flow.

Advanced encoder settings, track names, monitoring and custom FFmpeg recording still open the underlying OBS settings dialog. This remains an experimental interface and the complete long-term redesign is unfinished.

## Verification

Native application and motion plugin rebuilt. Existing updater, routing and quota regression checks and installer self-checks run against this build. The packaged application receives an isolated startup check with an empty portable profile. No user's installed instance or live broadcast is used.
