# BSI Violet — native emulator RIGHT sidebar

For the **emulator toolbar**, not the Customizer navigation or Settings sidebar.
Dark violet gradient, original outline icons, rounded hover/press feedback and
active markers. Existing **32px hit targets**, parent placement, native shortcuts,
mouse handlers, feature flags, overflow/popup geometry and bottom navigation stay
in the original resource. The separate top window bar is **not** modified.

## What you install

- `Assets/BSISidebarVisual.qml` — passive Qt Quick visual component.
- `Assets/Icons.js` — embedded original SVG drawings; no internet, fonts or npm.
- `build_external.py` — safe source-to-source patcher, Python 3.9+, no dependencies.

The patcher makes a **complete modified copy of YOUR sidebar QML**, keeping its
native model, functions, pressed/released callbacks, accepted mouse buttons and
popup components. It never opens an EXE and never enables disabled features.
It recognizes the supplied `Rectangle` / `UiColumnLayout` / `iBtn` structure and
known bottom/overflow buttons. Unsupported variants are refused, not guessed.

## MSI 5 installation

1. Exit all emulator instances and back up `HD-Player.exe` and the original
   extracted sidebar QML. Export the **sidebar** resource with the existing
   Customizer resource editor. Do not use `UiSettingsWindow.qml` here.
2. Note the sidebar's actual available compressed-byte limit shown by the
   resource writer. **The old Settings limit of 2,701 bytes is NOT a sidebar limit.**
3. Unzip this package. Open a terminal in it, then run:

   ```powershell
   py build_external.py "C:\path\OriginalSidebar.qml" "C:\path\SidebarOutput" --capacity ACTUAL_BYTE_LIMIT
   ```

   Replace `ACTUAL_BYTE_LIMIT` with the real integer. The output directory must
   not already exist. Output: `Sidebar-Outline.qml`, `Assets/`, and `report.json`.
   **Do not import the test fixture as your actual sidebar.** It is not included
   in the install ZIP and does not contain the emulator backend.
4. Copy **both** files from output `Assets` into:

   ```text
   C:\Program Files\BlueStacks_msi5\Assets\BSISidebarVisual.qml
   C:\Program Files\BlueStacks_msi5\Assets\Icons.js
   ```

   Administrator permission may be required. Copy the assets BEFORE saving QML.
   Keep them together; the visual component imports `Icons.js` relatively.
5. Replace only that original **sidebar QML resource** with the complete generated
   `Sidebar-Outline.qml`, using the existing capacity-checked save path. Keep
   relocation/growth safeguards enabled. If the writer refuses, stop: it is
   authoritative. Do not append/truncate the EXE or force-save.
6. If your Customizer supports it, invalidate only the matching compiled-QML cache
   for this resource. Do not delete unrelated resources or all caches blindly.
   Restart and check fullscreen, volume/mute, controls including right-click and
   Ctrl/Alt, enabled/disabled tools, APK install, overflow and bottom navigation.

For another installation location, pass an exact URL when generating:

```powershell
py build_external.py OriginalSidebar.qml SidebarOutput --capacity ACTUAL_BYTE_LIMIT --asset-url "file:///D:/Player/Assets/BSISidebarVisual.qml"
```

The file URL must be local and absolute. For spaces use `%20`. No network asset
URLs are accepted. Both skin files must be in that same Assets directory.

## Safety and limitations

- Original icons are hidden **only after** the replacement SVG reports Ready.
  Missing/broken sidecar or unrecognized native tools retain their original icons;
  the main column background may fall back to the original root color.
- The skin has no MouseArea, HoverHandler or TapHandler. Mouse input stays on the
  native buttons. It observes their existing hover/press/enabled state only.
- Native memory-trim animation flags also animate the new glyph. Native busy
  badges, shade/timer elements and tooltips are not removed or replaced.
- Source functions, callbacks and model flags are kept verbatim; application
  behavior still depends on the real emulator's original components and backend.
- `report.json` measures qCompress-compatible zlib level 9 of the complete output.
  This is not a promise of fit for another source, Qt build or resource slot.
  Output is refused before files are created if it exceeds the supplied limit.
- Runtime tests passed in a Qt **6.8** software-rendered fixture using native-type
  doubles: SVGs, dynamic mute, native press/release, right-click+Ctrl/Alt, disabled
  tools, navigation, animation, 32px layout, and missing-sidecar fallback.
  **Live Windows MSI / Qt 5.15 integration has not been verified.**
- The preview is labeled as a fixture. Its topbar is illustrative, not patched.
- Rollback: restore original sidebar resource/EXE and its matching compiled-QML
  state first; only then remove these Assets files. Player updates can overwrite
  the edited resource or remove added assets.

## Repository tests

```bash
python3 tests/sidebar_external_test.py
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software python3 tests/sidebar_external_runtime_test.py
```

The second test requires PySide6 plus working platform libraries; production does
not use Python. Icon drawings are original BSI Outline assets under the MIT
license included in this package.
