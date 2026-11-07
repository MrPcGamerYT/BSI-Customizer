PLAYER CHROME — MONOCHROME / MSI PLAYER
======================================
Black native titlebar + white outline buttons + matching black sidebar.
This is an emulator skin package, NOT a replacement Customizer or EXE.

START HERE — no CMD, Python, npm or server needed
------------------------------------------------
1. Extract this ZIP to a normal folder. Keep an untouched EXE backup.
2. In BSI Customizer, open your correct MSI Player EXE. Manually click
   Scan Resources. Export the ORIGINAL PlrTopbar.qml and plr.qml as UTF-8
   text files, or copy their full original source from the resource editor.
3. Double-click PlayerChrome.html; open it in Chrome or Edge.
4. Choose those TWO originals in the page (or expand Paste full source).
   Menu + collapse restoration is enabled by default; leave it enabled.
5. If your resource writer reports each entry's maximum compressed byte
   capacity, enter those limits in the matching fields. Otherwise leave
   them blank: the helper will explicitly mark their fit UNVERIFIED.
6. Click Create patched QML. Read the report and check the acknowledgment.
   Download BOTH complete outputs, or use Copy all for each output.
7. Back in the Customizer, replace the FULL corresponding resources:
     PlrTopbar.qml  <- the complete output generated in your browser
     plr.qml        <- the complete output generated in your browser
     PlrSidebar.qml <- the ready complete file supplied in this ZIP
   Names are case-sensitive. Do NOT paste HTML into a QML entry.
8. Use only the normal capacity-checked save workflow; keep backups.
   Close all emulator processes before replacing a running installation.
   Launch and verify menu, collapse/expand, minimize, maximize/restore,
   close confirmation, dragging, fullscreen and the game viewport.

IMPORTANT: topbar/main are generated on YOUR PC from YOUR originals.
This ZIP does NOT contain already-patched full native versions of those
TWO files. Their earlier pasted source was not retained on disk. The helper
avoids substituting a guessed or reduced fixture for your native source.
Your source stays local. The HTML has no network dependencies and does not
read or write an EXE. You still apply the output through the Customizer.

CAPACITY / COMPATIBILITY
------------------------
* Included complete black sidebar: 80,039 UTF-8 bytes;
  actual Qt qCompress level 9: 15,408 / 15,868 bytes (460 bytes spare).
  Qt qUncompress roundtrip matches the complete delivered file exactly.
* This sidebar is derived from the supplied MSI Player native layout.
  It is not a universal replacement for unrelated emulator versions.
* Topbar/main native slot capacities remain UNKNOWN. No claim they fit.
  The browser displays pako zlib9 + four-byte qCompress-envelope ESTIMATES.
  These are NOT a guarantee of the installed writer's Qt compression.
* The resource writer is the final authority. Never force, truncate, grow
  the slot, disable checks or overwrite an EXE after a capacity refusal.
* Do not Format/Beautify the compact QML: it can exceed the available slot.
* Already-patched or unrecognized topbar/window layouts are refused.
  Always start from an untouched original backup, not generated output.

WHAT CHANGES / WHAT STAYS
------------------------
Black titlebar and frame colors, passive outline icons, matching black rail.
Only the requested menu + collapse hidden flags are restored. Other clean
hidden features remain as supplied. Existing input handlers, menu opening,
close confirmation, adaptive visibility, backend calls, drag/double-click,
boot timer, login/auth logic and game viewport are retained. The main-window
transform changes four scoped colors only; it does not alter its geometry.
No global UiTheme mutation, fake game content or OS corner/shadow changes.

VERIFICATION / LIMITS
---------------------
* Node tests: scoped edits and preservation of original native bodies.
* Actual Chromium file://: full source input/output, QML download bytes,
  clipboard action, unknown-limit warning, supplied-limit refusal,
  stale-output invalidation and no network requests.
* Qt6 software tests with layout/backend doubles: Canvas rendering,
  original controls, close confirmation, collapse, maximize/restore,
  disabled input, double-click and resize. Sidebar tests also cover
  dynamic mute, native mouse/modifier input and fallback rendering.
* Black sidebar: 142 native functions/handlers and native model unchanged
  from the previously supplied compact sidebar.
* These are local regression/fixture tests, NOT live Windows MSI testing.
  Preview.png explicitly shows a Qt fixture, not a captured running MSI
  installation. Its logo/game/sidebar stand-ins are not installed assets.

FILES IN THIS ZIP
-----------------
PlayerChrome.html   Offline source-preserving generator for topbar/main
PlrSidebar.qml      Complete real native-derived black compact sidebar
Preview.png         Clearly labeled Qt fixture illustration
README.txt          These instructions
pako-LICENSE.txt    License for the embedded pako compression library
