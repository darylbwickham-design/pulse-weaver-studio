# Pulse Weaver 1.14.1

This patch refines Windows setup. It includes the same streaming and chat features as 1.14.0.

- Install, update and repair require an unchecked agreement box to be selected: agreement to the Terms of Service and acknowledgement of the Privacy Policy.
- Both policies have clickable links that open the public pages in the default browser.
- Setup uses quieter colours, consistent buttons and spacing, clear headings and readable language selection.
- Content scrolls on smaller windows while progress and action buttons stay visible.
- Backup, restore, interrupted-operation recovery and uninstall remain available independently of the agreement box.
- Regular, alpha and unstable installers share this interface and requirement.

Consent is a local installation step. It does not grant permission to collect data or enable automatic updates, and it is not sent to a server.

Validation includes agreement gating and busy-state recovery, default/minimum/expanded layouts, language selection, payload integrity, synthetic credential/profile migration, and upgrade/rollback from 1.14.0.
