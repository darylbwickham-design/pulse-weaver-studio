# Pulse Weaver 1.14.0 unstable 1

This is an opt-in interface showcase built on the alpha 6 motion and updater fixes. It is a separate preview channel. New installs still default to the regular release. The installer backs up the existing app and profile before updating and preserves saved scenes, account registrations, ports and Lumia settings.

## What changed

- Settings now opens a Pulse Weaver studio setup workspace with a short path from a blank show to source assignment, destinations and rehearsal. It groups stages, output bitrates, sound, recording location, sources, automation port, appearance, updates and recovery.
- Kick offers only a landscape bitrate setting. YouTube offers landscape and portrait settings. Twitch Enhanced Broadcasting keeps its own negotiated track settings.
- The stage editor shows visual Fill, Corner, Left and Right placement controls and a searchable stage picker when the stage strip grows.
- The show builder suggests appropriate camera, game and screen sources, reuses existing camera crop/rotation from a selected scene item, and lets a newly created camera fill more than one role without creating it twice.
- A dedicated music audio source can be excluded from YouTube and Kick stage routing while Twitch and its VOD track remain as configured. The builder rejects the Game Capture picture source as that music source.
- Alpha 6 fixes are included: saved motions follow source UUIDs across renames and adapt newly saved framing to resolution changes; swap focus and undo/redo handle two canvases more reliably.
- The unstable installer bundles only the public Google Desktop client ID for YouTube's PKCE sign-in. It does not distribute a client secret, access token or refresh token. Existing local registrations and account tokens are retained during updates.

## Important preview limits

- This is an unstable build. Rehearse a new show on both canvases before broadcasting. The guided builder still needs working device selections and URLs from external chat/alert providers.
- The studio setup workspace exposes common settings directly. Advanced encoder, audio device and VOD track controls still open the OBS engine dialog from the same workspace.
- A source exclusion also excludes that source's picture. Use a dedicated music audio source for YouTube/Kick exclusions.
- The interface changes have been compiled and package checked in isolation; they have not been exercised on the user's installed profile or against a live service.
- The ID-only YouTube sign-in and refresh path follows Google's documented desktop-client flow, but needs live account verification after installation.

## Install, update and recover

From alpha 6, use **Studio → Updates → Include unstable showcase builds** to opt in and check for this build, or download its installer from this release. From the regular 1.13.0 release, opt in to alpha and update to alpha 6 first to gain the separate unstable option. Do not uninstall the existing app first. The installer creates a verified backup before replacement. **Studio → Updates → Restore a backup / leave preview channel** returns to a previous version. The existing Lumia plugin ID and action schema are unchanged; its bundled 1.4.0 package is optional for users already on that version.
