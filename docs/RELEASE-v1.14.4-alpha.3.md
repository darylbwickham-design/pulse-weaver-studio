# Pulse Weaver 1.14.4-alpha.3 — Altron native teleport preview

Adds an optional native Altron Teleport camera filter and Altron Teleport Audio source. A live cutout gets the existing smooth white scribble departure/arrival effect, with separate animation state per filter. Choose your own arrival/departure audio files, volume and delay in the native audio source properties; stream audio is mixed directly, with optional monitoring through the app's shared monitoring device.

The bundled Altron bridge prefers native per-filter cues and keeps the Lua fallback. Its opt-in helper migration retains the previous Lua filter as a disabled backup and rolls back filter renames after errors. It never edits original cameras or Stage definitions. The Lumia avatar plugin and private overlay remain separately supplied; Node 22+ is still required for the show bridge. Setup creates helpers only; Stage creation remains manual.

Retains alpha.2's configuration-preserving installer. Validation covers real Pulse Weaver and OBS 32.2.2 GPU/audio output, source cleanup, independent instances, sound timing/gain/cancellation/missing-file fallback, bridge overlap guards, migration rollback, and installer payload/configuration preservation. These are isolated engine tests; this local preview has not been published to the updater.
