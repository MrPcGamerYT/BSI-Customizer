SETTINGS MONOCHROME — MSI APP PLAYER
Reference-inspired black/charcoal window with white outline navigation,
rounded white-border selected tab, compact rows, restrained hover and plain header.
This is SETTINGS, not the emulator's main right-hand toolbar.

IF THE EARLIER SETTINGS SMALL BRIDGE IS ALREADY INSTALLED
1. Close every emulator instance and back up the previous external skin file.
2. Replace ONLY this external file with Assets/BSISettingsOutline.qml from this ZIP:
   C:\Program Files\BlueStacks_msi5\Assets\BSISettingsOutline.qml
3. Restart Player and open Settings. No EXE/resource edit is needed for this update.
   Keep the same filename. Do not rename it to MonochromePanel.qml on the PC.

FIRST INSTALL (OR IF YOU NEVER INSTALLED THE SETTINGS BRIDGE)
1. Back up the correct HD-Player.exe. Close all instances.
2. Copy the Assets file to the EXACT path above BEFORE installing the bridge.
3. bridge.qml.txt replaces ONLY the original UiShadowContainer block inside
   UiSettingsWindow.qml. Retain outer imports, Window, native functions, dialogs,
   onClosing and onVisibleChanged.
4. UiSettingsWindow.qml is also included as a complete ready-to-copy EXAMPLE based
   on your previously supplied Settings source; it is unchanged from the earlier
   small-bridge package. Do not apply it to an unrelated version without checking.
5. Use the normal capacity-checked save. This full example measures 2,362 bytes
   with actual Qt qCompress9 against the earlier 2,701-byte slot. The real writer
   remains authoritative; never force growth or truncate the EXE.
6. Only the matching compiled-QML resource/cache may need invalidation for that
   embedded bridge change. Do not blindly delete unrelated caches.

DIFFERENT PLAYER INSTALL LOCATION
The embedded bridge currently loads:
file:///C:/Program%20Files/BlueStacks_msi5/Assets/BSISettingsOutline.qml
If your actual Player is elsewhere, use its real path in the bridge and copy the
asset there. Respect capacity again if changing the embedded bridge URL.

WHAT IS RETAINED
- Tab labels/order/pages come from the native pSettingsItems model. No fake Themes
  tab or page is inserted. A Themes entry is shown only if the native model has it.
- Native page URLs, arguments, init callbacks, save/restart behavior and discard
  prompts remain in the original embedded context. Navigation calls the native
  fChangeSelectedSetting; Close calls the original Window.close/onClosing flow.
- No external Python, SVG plugin, JS asset, web/network connection or new backend
  is needed by the visual skin. Icons are local Qt Quick Canvas vectors.
- This skin styles the window and navigation. Actual CPU/RAM/FPS controls and
  Save changes belong to your native pages and retain their installed UiTheme.
  Exact per-control black/grey styling requires the matching native page/theme
  sources; this package does not globally recolor the emulator or fake controls.

VALIDATION AND PREVIEW
Qt 6.8.3 software fixture passed creation, 10 original-context page-loader
arguments/init callbacks, mouse navigation, denied navigation/discard guard,
close routing, resizing and compact layout without QML warnings.
The preview is a labeled test fixture with SAMPLE performance controls, NOT a
live emulator screenshot and NOT replacement settings pages.
Live Windows MSI / Qt 5.15 behavior has not been verified here.

ROLLBACK
For an asset-only update, restore the previous BSISettingsOutline.qml file.
To uninstall the bridge, restore the original embedded resource/EXE and matching
compiled-QML state BEFORE removing the external asset. Missing asset fallback
shows an error and allows native Alt+F4 closing.
