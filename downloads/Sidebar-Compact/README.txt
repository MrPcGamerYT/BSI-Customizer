PLRSIDEBAR VIOLET INLINE — COMPACT FIX FOR THE 15,868-BYTE SLOT

Use THIS folder's PlrSidebar.qml, not the earlier oversized Sidebar-Direct file.
No Python command, external skin file, Icons.js or Assets setup is needed.

Measured COMPLETE UTF-8 resource with actual Qt qCompress level 9:
  Payload: 15,437 bytes
  Reported resource capacity: 15,868 bytes
  Headroom: 431 bytes
  qUncompress round-trip: exact source match
The emulator's own capacity-checked save is still authoritative; never force it.

WHAT CHANGED FROM THE PREVIOUS INLINE FILE
Unused drawing data (including topbar glyphs not used by this sidebar) and the
visual-only alias lookup were reduced. Indentation outside quoted/regex/comment
content was compacted. Newlines are retained so JavaScript automatic semicolon
insertion is unchanged. Do NOT auto-format this compact file before saving;
formatting can increase its compressed size beyond the resource slot.

Native functions, callbacks, model, feature flags, popups, accepted mouse buttons,
shade/busy indicators and the 32px cell geometry are retained. Unknown/disabled
tools are not reenabled, and unrecognized tools keep their stock glyphs.
Top window controls are unchanged.

APPLY
1. Close all emulator instances. Back up the correct HD-Player.exe.
2. Open this PlrSidebar.qml as TEXT; select all and copy.
3. In Customizer, select the actual PlrSidebar.qml resource in that HD-Player.exe.
   Replace ALL its QML with this complete file. Do not append to the old code.
4. Save normally, without clicking Format/Beautify. Stop if the writer refuses.
5. Re-read the resource: search for BSI SIDEBAR INLINE V1 COMPACT.
6. Restart and test tools, controls right-click/modifiers and bottom navigation.
If the marker is present but the old UI persists, inspect the running EXE path
and only the matching compiled-QML cache. Do not blindly delete all caches.

TEST RESULTS
- All 142 native function/handler bodies are lexically identical after ignoring
  outside-literal indentation. Quoted data, regexes, comments and line breaks in
  those bodies, plus the native model/flags, were checked separately.
- Complete QML parsed by Qt's qmlformat. Actual Qt 6.8.3 fixture passed SVG/mute,
  right-click+Ctrl/Alt, native press/release, disabled state, navigation, animation,
  geometry and component-owned-input checks, with external Assets deleted.
- Not a live Windows MSI / Qt 5.15 integration verification.
