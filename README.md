# BSI Customizer — BlueStacks 5 / MSI 5 Modding Toolkit

A native **C++ / Qt 6 (QML)** tool for modders. It patches `HD-Player.exe` and
`libOpenglRender.dll` directly — theme, icons, FPS cap, integrity check, boot
video and more. Full C++/Qt 6 rebuild of the original Python tool, including a
properly working **QML (zlib) theme patcher** with automatic relocation.

## Features

| Page | What it does |
|---|---|
| **Dashboard** | Detects BlueStacks 5 / MSI 5 installs (disk + registry), lists instances from `bluestacks.conf`, editable target paths |
| **Theme** | Recolors the whole BlueStacks UI by decompressing `UiThemeObject.qml` (zlib) inside the EXE, editing colors, recompressing. **Automatic relocation into a zero cave when the new theme is bigger than the old slot**, with qrc table re-pointing and verify-before-write. Legacy **hex palette** mode included for old builds |
| **SVG** | Extract every embedded SVG icon from the EXE, preview it, replace it in place (null-padded, file size unchanged) |
| **FPS** | FPS unlocker — detours `QQuickSlider::setTo` in `libOpenglRender.dll`, stores your cap (60–1440) in a code cave; also rewrites the 240/360/60 built-in constants |
| **Integrity** | Bypasses the disk-integrity self-check so patches survive restarts |
| **Boot Video** | Injects a `Loader` into the compressed boot QML that plays `Assets\Promo.mp4` |
| **Extras** | Version spoofing, ad/UI string removal (nulls the strings), instance detection, one-click backup restore |
| **Appearance** | App theme (dark/light/system) + accent colors |

**Safety:** every patch writes a `.bak` next to the original first; Restore
rolls back byte-identically. The app requests administrator rights via its
embedded manifest.

## The QML theme method — why it works now

The old tool's QML theme patcher only worked sometimes because a bigger
recompressed theme silently overflowed its slot and corrupted the qrc table.
This build ports the full "no room" fix:

1. Locate the `UiThemeObject.qml` record via the Qt resource (qrc) node — not
   blind byte scanning — so the *right* record is always patched.
2. If the new compressed QML fits the old slot (+ trailing zero slack), patch
   in place and zero-pad.
3. If it doesn't fit, find a **mapped zero cave** in a PE section, write the
   new record there, re-point the qrc node's data offset (RVA math), and
   invalidate the orphaned old record.
4. **Verify before writing** — the patched image is re-parsed and
   decompressed; the file is only written if the new theme round-trips.
5. Works on both zlib-compressed and plain UTF-16LE theme builds; UTF-16LE
   edits are applied in place with no size change.

## Build

```
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires Qt 6.5+ (Core, Gui, Quick, QuickControls2, Svg) and CMake 3.21+.
Any generator works (Ninja, Visual Studio, NMake).

On Windows `src/app.rc` embeds `src/app.manifest`, which makes the exe request
administrator rights, and the target is built as a GUI-subsystem app
(`WIN32_EXECUTABLE`) so no console window flashes behind the UI. The MSVC
toolchain is told not to generate a second manifest (`/MANIFEST:NO` +
`GenerateManifest=false`) — otherwise the linker ends up with two
`RT_MANIFEST` id 1 resources and dies with
`CVTRES CVT1100: duplicate resource` / `LINK LNK1123`.

## Self-test

```
BSI-Customizer --selftest
```

On Windows the exe is a GUI app, so when it is started from a terminal it
re-attaches to the parent console to print its report; when stdout is
redirected to a file or pipe (what CI does) it writes there instead.

Runs 10 sections on synthetic PE/zlib fixtures (theme in-place + relocation,
plain UTF-16LE, hex palette, FPS detour + restore, integrity NOP, SVG
replace, version spoof + removals, boot video, live QML binding chain) and
exits non-zero on any failure. The release pipeline runs it on every build.

## Releases

Push a tag like `v2.0.0` — GitHub Actions (`.github/workflows/release.yml`)
builds with MSVC + Qt 6.8.3, verifies the embedded `requireAdministrator`
manifest with `mt.exe`, runs the self-test, packages with `windeployqt` (and
checks the Qt runtime actually landed in the package) and publishes a zip on
the GitHub Release page. A manual run (`Run workflow`) uploads the same zip as
a job artifact instead of creating a release.

`.github/workflows/ci.yml` runs the same build + manifest check + self-test on
every push to `main` and on pull requests, so a broken build is caught before a
tag is ever pushed.
