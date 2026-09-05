# BSI Customizer

Qt Quick desktop customizer for BlueStacks / MSI App Player on Windows.

## Theme patching

1. Select the **HD-Player.exe from the installation you actually launch**.
2. Close **all** running HD-Player instances. Running processes keep their old theme in memory and Windows can lock their files.
3. Load the QML theme, choose a preset or edit colors, then click **Patch Theme**.
4. Start BlueStacks again. You can close it, change the palette, and relaunch it again; another theme changer is not a prerequisite.

The QML patcher validates Qt resource trees (14-byte v1 and 22-byte v2/v3 nodes), preserves neighboring resource headers, and uses memory-relative offsets when relocating a resource across PE sections. It refreshes the resource timestamp so a previous run's disk cache is rejected, and invalidates the theme's embedded compiled QML where identified. It does not globally disable Qt caching or delete application data.

If an older patch already wrote the desired colors but BlueStacks still shows an old palette, click **Patch Theme** again (even without new edits) to refresh the cache. Ambiguous resource layouts and edits that cannot fit are reported instead of silently claiming success. Use the **Hex (old)** method for legacy builds without a QML theme.

The original `.bak` is preserved. Theme edits are made against the current EXE, not a stale backup. Writes are checked and atomically replaced where supported; close BlueStacks and run the tool as administrator if Windows refuses access.

## FPS slider

Supported targets (64-bit x86 Windows DLLs):

- **Qt5QuickTemplates2.dll**
- **Qt6QuickTemplates2.dll**

These contain `QQuickSlider::setTo` / `QQuickRangeSlider::setTo`. **libOpenglRender.dll is not the FPS target.** Installation detection chooses the matching Qt major when possible; an old saved OpenGL path is migrated. You can also select the correct DLL manually.

Choose a high-FPS maximum from 60–1440, close BlueStacks, and patch. Only the 240 FPS high-frame-rate maximum is replaced. Normal mode stays at its default **60 FPS**, with no normal-mode setting or rewrite. Other slider values are unchanged. This changes the settings slider's upper limit, not a guarantee of in-game FPS.

A per-DLL `.bak` supports restoring the original. Unsupported setter code and backups from a different DLL version are refused. Older high/normal-mode detours (including the `read it.txt` reference) can be upgraded to high-FPS-only using their matching clean backup.

## Build and verify

Requires CMake 3.21+, C++17, and Qt 6.5+ with Core, Gui, Qml, Quick, QuickControls2, and Svg.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Run the built executable with `--selftest`. On Windows with the Visual Studio generator:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
Start-Process build/Release/BSI-Customizer.exe -ArgumentList '--selftest' -Wait
```

The self-test covers resource detection/relocation, actual Qt QML loading in separate processes with disk caching enabled, repeated patch–launch cycles, embedded cache invalidation, palette parsing, and both FPS DLLs with MSVC/MinGW export names. On Windows x64 it executes controlled setter fixtures to verify that only 240 FPS is replaced while 60 FPS and other values (including NaN) pass unchanged. It also tests upgrading legacy high/normal patches, repatching and exact restoration. These are synthetic fixtures, not an installed BlueStacks instance. GitHub CI runs the Windows build and self-test on pull requests.

For an unsupported real EXE, collect a read-only diagnostic report:

```powershell
.\BSI-Customizer.exe --theme-probe "C:\Program Files\BlueStacks_nxt\HD-Player.exe"
```
