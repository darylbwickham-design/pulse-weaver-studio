# Pulse Weaver 1.12.3 — Live 16:9 transforms

Camera scene selection remains preview-only, so selecting another scene does not
put it on air. Source transforms within the active 16:9 scene now propagate live
to destination-specific Stage copies, including copies used for source exclusions.
Position, scale, bounds, rotation and crop changes—and Move plugin animations—no
longer wait for a Stage change. Vertical behaviour is unchanged.

Public Preview installations use PulseWeaver-Public-Dist-1.12.3-Setup.exe.
Private beta installations use PulseWeaver-Setup-1.12.3-BETA.exe. Both installers
contain no developer registrations or personal account tokens. Lumia companion
1.1.3 remains compatible. Mac remains on its separate mac-v preview release line.

Validation: native Windows frontend build; live 16:9 and portrait transform
synchronisation; rendered portrait output; Stage exclusion isolation; installer
payload/configuration preservation; channel identity; and credential/archive audit.
No live broadcast was started during release verification.
