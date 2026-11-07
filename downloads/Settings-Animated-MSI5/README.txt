SETTINGS — ANIMATED SVG ICON EDITION
===================================
Black/white Settings shell; your supplied Icons.js is extended and wired in.
This is the emulator Settings UI, not the Customizer UI or Android game UI.

INSTALL — COPY BOTH FILES
------------------------
1. Fully exit the emulator. Back up the existing files below if present.
2. Copy BOTH files from this ZIP's Assets folder to:
   C:\Program Files\BlueStacks_msi5\Assets\

   Result:
     Assets\BSISettingsOutline.qml
     Assets\Icons.js

3. Keep your CURRENT embedded UISettingWindow.qml / UiSettingsWindow.qml
   resource unchanged. It already loads the external Settings shell and
   passes bsiHost/bsiTheme. No EXE resource/slot rewrite is needed here.
4. Restart and open Settings. Test hover, press, selection, native
   discard/cancel, native Save and close. Restore backups to undo.

If your installation is elsewhere, your native bridge's file:/// URL must
match the actual BSISettingsOutline.qml location. Icons.js must sit beside
that QML file, using exactly that case. Keep the .pragma library line.

Replacing Icons.js ALONE adds shapes but cannot add QML animations. Copy
both files for the new effects. They need no network, font or image download.

WHAT IS NEW
-----------
* All 29 icon keys/shapes from your pasted Icons.js are preserved.
* 14 additional shapes plus 14 aliases: 57 named entries total.
* Settings now uses SVG Images from Icons.js, not the old Canvas drawings.
* New speedometer, monitor, isometric graphics cube, game-window controls,
  controller, sliders, smartphone, palette, gear, info and other glyphs.
* Hover: icon lift/scale, small gear/palette tilt, label nudge and tint fade.
* Press: icon compression and background press feedback.
* Selection: spring indicator and a short white flash/fade.
* Close: small rotation/press feedback, still calling native Window.close().
* No endless spinning, shimmer loops, timers or backend polling.

REDUCED MOTION
--------------
Near the top of BSISettingsOutline.qml change:
  property bool bsiAnimations: true
To:
  property bool bsiAnimations: false
Hover/selection state still shows, but transitions become immediate.

NATIVE LOGIC / LIMITS
--------------------
Native model alone supplies tabs. No invented Themes page, fake Save button,
configuration commits or replacement native settings controls are added.
Selection follows the host's pCurrentTabIndex only, so the shell cannot
visually switch early when native code blocks a tab change for unsaved data.
Native page loaders, arguments, callbacks, Save/discard, authentication,
settings window sizing and native backend logic stay in the existing host.
No global UiTheme mutation or forced Amatic SC. Existing native dropdowns,
sliders and page fonts keep their current styles. OS corners/shadows unchanged.

This Settings package does not rewrite your topbar or inline sidebar. The
original supplied shape names remain available to other consumers of Icons.js.
Older external consumers using shapes[name] and ICON_COLOR keep that API.
Do not rename/remove other emulator Assets or delete the stock font files.

VERIFIED / NOT VERIFIED
-----------------------
* Source test: supplied shape preservation, 57 valid self-contained SVGs,
  safe solid-color replacement, URL helper and package/source equality.
* Actual Qt SVG decoder: all 57 entries produce nonempty images.
* Qt6.8.3 software fixture: all 11 visible SVG Images Ready; actual hover
  scale, native press feedback, reduced-motion snap, ten native-context
  page-loader doubles with args/init callbacks, guarded selection, close
  callback and 719/720/700/840/1200 px layouts passed without QML warnings.
* Previous reference/monochrome source and reference runtime regressions pass.
* No live Windows MSI installation was tested. SVG decoder/plugin support in
  the installed emulator remains its own runtime requirement.
* Preview.png and MotionPreview.gif are real Qt fixture renders and are
  labeled inside the picture. CPU/FPS/Save and tab entries in those pictures
  are test DOUBLES, not captured live MSI controls or installed page files.
* MotionPreview.gif loops for demonstration; the installed UI does not loop
  its selection animation. Effects are triggered only by actual state changes.
* Sandbox software tests use abort-on-call shims for unused GL/EGL/XKB/DBus
  APIs. None of these APIs were invoked during successful software tests.

ZIP CONTENTS
------------
Assets\BSISettingsOutline.qml  Full replacement external shell
Assets\Icons.js                Complete extended icon library
Preview.png                    Static Qt fixture preview
MotionPreview.gif              Qt fixture hover/press animation preview
README.txt                     These instructions
