# Pulse Weaver 1.12.1

Fixes portrait output scenes shrinking or moving sources when Stage exclusions are enabled. Private scene copies now retain the original canvas dimensions and relative camera transforms. The preview and broadcast encoder use this same repaired output canvas.

Audio-only and stale exclusions no longer create unnecessary video scene copies. The Stage picker still lists global and inactive audio inputs with (audio) labels and saves their original names. For YouTube Dual, exclude Spotify on 16:9 and leave it included on 9:16. Twitch Dual continues to share its audio exclusion mix.

This is a repair of the 1.12.0 baseline; the next development focus remains the Camera tab. Public and private installers retain their existing installation folders and preserve user configuration. Lumia companion 1.1.3 remains compatible.

Validation: reproduced the coordinate-space failure against the 1.12.0 runtime; native build; GPU regression covering portrait dimensions, position, scale and actual local output frames; audio exclusions; archive parity; credential audit; installer checks. No live platform broadcast was started.
