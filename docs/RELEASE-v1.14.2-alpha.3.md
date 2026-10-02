# Pulse Weaver 1.14.2 experimental alpha 3

Corrects Twitch program previews displaying OFF while the output bar displayed
READY. Twitch readiness now comes from the configured Twitch streaming service
and stream key, using the same condition as starting a Twitch output.

The program previews and output statistics now agree: LIVE for an active output,
READY for a connected idle destination, and NOT CONNECTED for a selected
destination that lacks its connection. Chat connectivity does not determine
streaming readiness. No additional network requests are introduced.

Includes the alpha 2 reference show builder and Lumia plugin 1.4.1 bundled under
`integrations/lumia`, also available as a separate release asset.

Enable **Include experimental alpha builds** under **Studio → Updates**, stop
outputs and check for updates. Setup backs up the existing installation and
profile before upgrading.

Verification: the native frontend compiled successfully. Installer contents and
uploaded asset checksums were inspected. This status change has not been checked
in a live broadcast.
