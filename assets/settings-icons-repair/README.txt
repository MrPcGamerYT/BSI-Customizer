VOID / ISOLATED SETTINGS ICON REPAIR
Developed By NOVA XIS
https://www.youtube.com/@nova_xis20

THIS IS NOT ANOTHER LIMITED GLOBAL Icons.js REPLACEMENT.

Close the emulator and back up the affected Assets files first.

1. Shared/global Icons.js
   Keep the full Icons.js you supplied in chat for the sidebar/topbar.
   If it was overwritten by the previous limited pack, Shared-Recovery/Icons.js
   reconstructs your supplied 43 primary SVG shapes plus 14 original aliases,
   preserving their geometry, shapes object, svg(name,color), url(name,color),
   white default color, accepted RGB formats and fallback behavior.
   Copy that file to your existing shared Icons.js location ONLY if recovery is
   needed. It is not an inventory of unknown icons outside the file you supplied.

2. Settings files
   Copy these THREE entries together beside your existing SettingsShell.qml:
       SettingsShell.qml
       BSISettingsIcons.js
       BSISettingsIcons/   (entire folder, including png/)
   No top-level Icons.js is included, so these Settings files do not overwrite
   your shared icon library. Do not flatten the folder hierarchy.

   SettingsShell.qml is the animated shell you supplied, with icon-only changes:
   - import "BSISettingsIcons.js" as Icons
   - Devices and Phone now use separate symbols
   - PNG-first Image source, optional SVG path with one-way PNG fallback
   - missing asset paths are logged if neither path works

3. Keep your existing native host integration
   This shell still expects bsiHost and pageContainer reparenting exactly as in
   your posted QML. It is not a replacement UiSettingsWindow or native backend.
   The supplied host's fChangeSelectedSetting, close, Save and guards remain
   responsible for actual Settings behavior. No native EXE resource is patched
   by this archive, and copying files alone cannot create a missing host hook.

The 15 chosen Settings symbols are the previous silver-white designs you asked
for, NOT the different Settings glyphs in the shared file. They are isolated
so names such as graphics/gamepad/keyboard no longer collide with chrome art.
Default bsiPreferPngIcons=true uses transparent 48px PNGs at your 18px/24px icon
sizes. This avoids depending on a Qt SVG decoder in the emulator. Your existing
scale/rotation/translate/opacity animations apply to the image unchanged.
PNG assets are fixed #f4f4f7. If you need arbitrary tint, set bsiPreferPngIcons
false to use the SVG url(kind,stroke) path; SVG errors fall back once to PNG.
No red icon variants, timers or infinite icon animations are added.

Reload/restart the emulator after deployment. If icons are still missing,
provide the exact Assets paths, the installed SettingsShell.qml and any
'Settings icon asset missing:' message; do not replace more shared files.
The real Qt fixture test checks loading/callback preservation, not an OEM
emulator session. Boot and all unrelated assets remain untouched.
