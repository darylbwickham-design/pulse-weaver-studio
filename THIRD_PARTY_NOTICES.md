# Third-party notices

This application bundles a modified OBS Studio, not merely an external OBS controller. OBS Studio is licensed under GPL version 2 or later. Its corresponding source, including Pulse Weaver modifications and vendored browser, websocket and DirectShow source, is under `engine/obs-studio`. Preserve its COPYING, AUTHORS and component license files when redistributing.

Runtime dependencies include Qt, Chromium Embedded Framework, FFmpeg and other OBS dependencies. They retain their respective licenses. Versioned dependency downloads and hashes are listed in `engine/obs-studio/CMakePresets.json`; dependency source and build recipes are available at https://github.com/obsproject/obs-deps and Chromium Embedded Framework source at https://bitbucket.org/chromiumembedded/cef . Preserve bundled license notices. See component licenses in the source tree for exact terms.

The optional Lumia integration is supplied as source in `integrations/lumia-pulseweaver`; it is separate from the studio installer.
