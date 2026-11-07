RIGHT EMULATOR SIDEBAR — BLACK / ICONS.JS / ANIMATED
==================================================
This is PlrSidebar.qml, NOT the Settings left navigation or Customizer UI.
Complete real native-derived sidebar source; no reduced test fixture installed.

INSTALL — READY FILES, NO CMD/PYTHON
-----------------------------------
1. Fully exit all emulator instances. Back up HD-Player.exe, the original
   PlrSidebar.qml resource and existing skin files before making changes.
2. Copy these TWO files from this ZIP's Assets folder into:
   C:\Program Files\BlueStacks_msi5\Assets\

     BSISidebarVisual.qml
     Icons.js

   Copy assets FIRST. Keep these two files together. Administrator approval
   may be required by Windows. Icons.js is byte-identical to the library in
   the Settings-Animated-MSI5 package; both skins can share that same file.
3. In BSI Customizer open the correct emulator EXE, manually Scan Resources,
   select PlrSidebar.qml and replace its FULL source with this ZIP's complete
   PlrSidebar.qml. Use only the normal capacity-checked resource writer.
4. Do NOT copy PlrSidebar.qml into Assets as a substitute for step 3; that
   does not change the native embedded sidebar. Do NOT replace
   UISettingWindow.qml / UiSettingsWindow.qml or BSISettingsOutline.qml.
5. Keep normal resource/cache safeguards enabled. Do not Format/Beautify the
   compact source, force-save, truncate/append an EXE, grow its slot manually,
   or blindly delete unrelated caches after a refusal.
6. Restart and test native fullscreen, mute/volume, keyboard controls,
   right-click + Ctrl/Alt behavior, APK install, Settings, overflow and bottom
   Back/Home/Recent navigation. Native disabled/hidden tools stay disabled.

The resource points to the exact C: path above. Another installation needs
its own matching local file URL. This native source is for the previously
supplied MSI Player layout, not an arbitrary different emulator version.
If the writer reports another capacity or refuses, stop and send that exact
message. Do not bypass it. Restore the original resource/assets to undo.

WHAT CHANGES
------------
* Black/grey rail, white/grey outline SVGs from your extended Icons.js.
* Hover lift/zoom, press compression, smooth border/hover feedback.
* Settings resolves to the new gear glyph with a small hover tilt.
* Active indicator grows/fades; disabled tools retain disabled styling.
* Native busy/trim rotation runs only while the native busy flag is set.
* The native button owns ALL input. Skin adds no MouseArea, HoverHandler,
  TapHandler, shortcut, click handler or emulated backend action.

Unlike the earlier self-contained compact sidebar, this version deliberately
loads a separate BSISidebarVisual.qml so shared Icons.js updates are visible
and richer animation does not have to fit inside the compressed EXE slot.
Both Assets files are REQUIRED. Merely replacing Icons.js cannot update an
older inline sidebar that still embeds its own icon table.

REDUCED MOTION
--------------
Inside Assets/BSISidebarVisual.qml set:
  property bool motionEnabled: false
Restart to apply to all instances. Native busy flags/logic remain unchanged;
this disables the skin's own transition/busy-glyph animation only.

VERIFICATION / LIMITS
---------------------
* Complete installed native resource: 74,663 UTF-8 bytes.
* Actual Qt qCompress level 9: 13,996 / 15,868 bytes; 1,872 spare.
* Complete-file qUncompress roundtrip matches the delivered source exactly.
* 142 native functions/handlers and full native model unchanged from the
  previously supplied black compact sidebar. ALL code outside its visual
  component is unchanged except the descriptive skin marker/blank line.
* No blanket restoration of hidden features, native login/auth/business
  changes, game viewport changes, topbar rewrite or global UiTheme mutation.
* Qt6.8.3 software fixture passed SVG rendering, hover scale, native press
  feedback, reduced-motion snap, live mute binding, right-click+Ctrl/Alt,
  press/release, disabled tools, bottom navigation, busy/trim animation,
  32px geometry and missing-asset stock-glyph fallback.
* Missing-asset fixture intentionally logs Loader errors. Original glyphs
  remain instead of being blanked. If assets are absent/broken, stock colors
  may return; missing files are not fixed by changing native actions.
* The fixture is NOT live Windows MSI/Qt5 integration. Preview.png is labeled
  inside the picture and shows layout/input doubles, not a live emulator.
* Software tests use abort-on-call shims for unused GL/EGL/XKB/DBus APIs;
  none were called during successful software tests.
* The actual installed resource writer remains the final capacity authority.

ZIP CONTENTS
------------
PlrSidebar.qml                  Full native-derived resource replacement
Assets\BSISidebarVisual.qml     Passive black animated visual component
Assets\Icons.js                 Complete shared extended SVG library
Preview.png                     Clearly labeled Qt fixture preview
README.txt                      These instructions
