# Pulse Weaver 1.14.4-alpha.6 — Show Control refinements

Show Control now uses the compact controls refined in Lite. Twitch, YouTube and Kick have joined format toggles instead of dropdowns. The redundant Outputs heading and permanent account-status row are removed; the link icon opens Connections, exposes account status on hover and shows an attention icon when action is needed. Existing live-output format restrictions still apply.

The landscape preview title bar holds a Live previews switch and a 15 / 30 / 60 FPS slider. These controls change preview drawing only; stream and recording frame rates are unchanged. Regular Alpha starts with live previews enabled and 60 FPS refresh. Turning live previews off pauses the Show previews while outputs are active and leaves the broadcast running. Controls occupy the existing title bar, and the preview/chat split is saved when resized.

Recording uses the same joined control in its existing bottom-bar slot, capped at 240 × 32 px. **Default**, Off, 16:9, 9:16 and Both are available. Default retains the regular recording-settings path; the other formats retain their Stage-routed recording paths. Format changes remain locked while recording. The replay buffer, chat providers, Altron tools, stingers and other regular Alpha features are retained.

Includes Alpha 5's shared Lumia guide and Alpha 2's configuration-preserving installer repair. Stop Pulse Weaver before running **PulseWeaver-Setup-1.14.4-alpha.6.exe**. Setup creates a verified backup and retains the existing configuration, API port and Lumia connection. Alpha updates remain opt-in through **Studio → Updates → Include experimental alpha builds**. The regular release remains 1.14.3.

Validation details accompany the release in `VALIDATION.txt`. UI and installer checks do not certify performance with every game, encoder or live destination.
