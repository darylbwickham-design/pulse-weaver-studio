# Pulse Weaver 1.12.0

The Stage exclusion picker now lists audio inputs from other scenes and global devices, marked (audio), alongside sources in the selected programme scene. Sources appear once, including inactive audio inputs that a future Stage may activate. Exclusions save the original OBS source name and use existing output audio routing. Private internal sources remain hidden.

For YouTube Dual, exclude Spotify on the 16:9 row and leave it included on 9:16. Kick and routed recordings also use Stage audio exclusions. Twitch Dual currently shares one audio exclusion mix across its two formats.

Version 1.12.0 marks the baseline for the next development focus: improving the Camera tab. Camera improvements follow in subsequent changes.

The public preview retains its separate installation folder and tester-owned platform app registrations. Lumia companion 1.1.3 remains compatible.

Validation: native frontend build, StageExclusions libobs regression, release archive parity, credential audit and installer payload verification.
