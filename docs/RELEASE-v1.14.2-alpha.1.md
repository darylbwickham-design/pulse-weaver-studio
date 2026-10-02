# Pulse Weaver 1.14.2 experimental alpha 1

This preview reduces memory retained by preloaded animated stinger transitions.
The same stinger was decoded separately for each output player. In the user's
scene collection, five players loaded one 100-frame WebM. Its decoded video
frames occupied approximately 494 MiB per player. The new cache shares those
frames, removing approximately 1.9 GiB of duplicate decoded frame allocations
for this scene collection. Overall application RAM usage also includes browser
overlays, video rendering and other sources, so the full app needs a follow-up
measurement after installation.

Playback, pause, seek and transition state remain independent for each output.
Only preloaded stingers opt into sharing. Other media sources and user preload
preferences retain their existing behavior. The 1.14.1 installer and its Terms
agreement and Privacy Policy links are included.

To install through Pulse Weaver, enable **Include experimental alpha builds**
under **Studio → Updates**, then check for updates after stopping outputs. Setup
backs up the existing application and profile before upgrading. **Restore a
backup / leave preview channel** can return to the previous version.

Verification: the production native plugin compiled; 50 video/audio cache
checks passed with a synthetic transparent clip and 49 with the user's actual
stinger. Checks covered five concurrent players, independent playback, cleanup,
and cache invalidation when clip contents change. The packaged installer passed
payload, layout, language and 1.14.1 upgrade/rollback checks. The current
running installation was not changed during development. A full visual
transition check and process RAM comparison remain to be done on the alpha.

Lumia plugin 1.4.1 is unchanged and does not need reinstalling for this fix.
