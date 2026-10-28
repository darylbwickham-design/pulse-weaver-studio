# Pulse Weaver 2.0.0-unstable.10 — Transparent desktop HUD

Camera now has **Landscape · 16:9**, **Portrait · 9:16**, and **HUD** workspaces.
Choose a display in HUD; its current physical pixel dimensions determine the
canvas size. Add and arrange sources with the native Camera editor.
New HUD browser sources start with that display's dimensions and a blank page
ready for an effects URL. Existing and shared sources retain their settings.

The monitor icon beside the Lumia and connection controls shows or hides the
transparent desktop output. It stays grey while off or starting and lights up
after the first frame is presented. The output is borderless, stays above normal
desktop windows and passes mouse input through to the application underneath.
HUD output starts off on application launch; the selected display is remembered.

There is one HUD composition, independent from stages and Looks, with an on/off
monitor control. Changing stages, Looks or Camera tabs leaves its output alone.
HUD editing shows its layers without a scene picker or transition controls, and
the HUD is excluded from broadcast scene and Look catalogues. Disconnecting the
display, changing collections or resetting the graphics device stops the HUD.
Changing display resolution resizes its canvas; changing the target display
recreates the output on the explicitly chosen screen.

Rendering stays on the GPU, without an additional video-output mix, encoder or per-frame
CPU image transfer. Multiple output mixes do not multiply HUD presentation
frequency. Effects update with the studio video clock. A checkerboard appears
only in the HUD editor; it is never included in the desktop output.

This Windows feature requires the Direct3D 11 backend. The desktop output uses
SDR premultiplied-alpha composition. Desktop and borderless-windowed use are
the initial target; exclusive fullscreen and protected content require their
own compatibility testing. Display Capture may include the desktop HUD.

The receive-only Discord integration, All chats (no Discord) filter and Discord
blue styling from unstable.8/.9 remain included. Stable and Mac releases are
unchanged.
