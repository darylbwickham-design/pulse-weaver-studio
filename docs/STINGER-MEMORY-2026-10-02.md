# Local stinger memory fix — 1.14.2 candidate

## Diagnosis

Read-only inspection of the running installation found a 1920×1080, 100-frame
transparent WebM stinger with preloading enabled. Its log showed five players
preloading that same clip: the main transition, vertical program, YouTube
horizontal, YouTube vertical and Kick horizontal.

Decoding the actual clip through the production media cache measured 494.4 MiB
of video frame payload per copy. Five private caches retain approximately
2,471.9 MiB; one shared cache retains 494.4 MiB, saving 1,977.5 MiB (1.93 GiB).
These figures cover decoded video frames, not total application working set.
The observed main process working set was approximately 3,417 MiB. Browser
overlays and other rendering resources also consume memory.

## Change

Only preloaded stinger sources opt into sharing. Players with the same local
file, contents and decoding options retain one immutable video/audio cache.
Each player keeps independent playback, pause, seek and timestamp state.
The last player releases the decoded data. Other media sources retain their
existing behavior; user preload settings are preserved.

Cache identity includes the path, file metadata, SHA-256 of compressed contents
and decoding options. Same-size replacements with unchanged timestamps cannot
reuse stale frames. Unreadable or non-local files use the existing private path.
Background preload decoding is serialized while a shared entry is published,
preventing simultaneous players from temporarily allocating duplicate movies.

## Verification

- The production obs-ffmpeg plugin compiled successfully in RelWithDebInfo.
- 50 checks passed with a synthetic transparent WebM containing audio.
- 49 checks passed with the user's actual stinger, which has no audio segments.
- Checks cover five concurrent players, independent playback/pause/seek,
  original-owner destruction, final-owner cleanup, private-cache isolation,
  different playback speed, reopening, and changed contents with identical
  file size and modification timestamp.
- OBS allocation counts returned to baseline after releasing all players.
- Candidate installer payload, layout and language checks passed.
- A synthetic 1.14.1 → 1.14.2 upgrade and full rollback preserved all four
  configuration files and all 2,232 original files byte-for-byte. No installed
  files or registry entries were changed by this test.
- The packaged obs-ffmpeg DLL matches the native build by SHA-256, and its
  FFmpeg DLL dependencies match the production payload.

The test exercises production media-cache code. It does not prove the final
working-set reduction or visual transition quality in the installed full app;
those require running the candidate with the same scene collection and outputs.

## Delivery

The local 1.14.2 candidate uses the 1.14 production runtime with the rebuilt
obs-ffmpeg plugin and the 1.14.1 installer. It is not a published release.
The existing running application and profile have not been replaced.
Close Pulse Weaver before running the candidate installer, then exercise the
same animated transitions and compare memory after it settles. The installer
backs up the previous installation and preserves its configuration.
