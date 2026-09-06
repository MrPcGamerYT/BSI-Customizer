# BSI Customizer

Qt Quick desktop customizer for BlueStacks / MSI App Player on Windows.

## Single portable EXE

Release output is **`BSI-Customizer-<version>-win64.exe`**, not an installer.
The app, Qt/QML plugins, SVG support, and VC runtime DLLs are included inside it.
A native launcher unpacks into a private temporary directory, runs the app, and
cleans up on normal exit. No runtime download or installation is performed.
See [the full portable workflow/build guide](docs/PORTABLE.md) for Actions,
local builds, clean-environment tests, temporary-file behavior, and license/DLL
extraction. GitHub Releases provide the EXE directly; Actions wraps its single
EXE artifact in a transport ZIP.

## Theme patching

1. Select the **HD-Player.exe from the installation you actually launch**.
2. Close **all** running HD-Player instances. Running processes keep their old theme in memory and Windows can lock their files.
3. Load the QML theme, choose a preset or edit colors, then click **Patch Theme**.
4. Start BlueStacks again. You can close it, change the palette, and relaunch it again; another theme changer is not a prerequisite.

The QML patcher validates Qt resource trees (14-byte v1 and 22-byte v2/v3 nodes), preserves neighboring resource headers, and uses memory-relative offsets when relocating a resource across PE sections. It refreshes the resource timestamp so a previous run's disk cache is rejected, and invalidates the theme's embedded compiled QML where identified. It does not globally disable Qt caching or delete application data.

If an older patch already wrote the desired colors but BlueStacks still shows an old palette, click **Patch Theme** again (even without new edits) to refresh the cache. Ambiguous resource layouts and edits that cannot fit are reported instead of silently claiming success. Use the **Hex (old)** method for legacy builds without a QML theme.

The original `.bak` is preserved. Theme edits are made against the current EXE, not a stale backup. Writes are checked and atomically replaced where supported; close BlueStacks and run the tool as administrator if Windows refuses access.

## Gradient themes

BlueStacks paints the sidebar and top bar from `plr.qml`'s `PlrSidebar` /
`PlrTopbar` instances (`color: oem === "msi5" ? primary70 : primary80`), not
from the type-level Rectangle roots. **QML (new)** is offered only on Qt 6
players. Older Qt 5 / MSI 5 builds hide that method: use **Hex (old)** and
**Gradient theme**, which writes a two-stop linear gradient onto those real
chrome instances (solid hex recolors remain available). Qt 5.15 can load the
gradient source, but compiled `qv4cdata` units with an empty source path are
invalidated first; the on-disk `<install>/cache/qmlcache` is also cleared so
Qt 6 reloads the edited QML.

After loading the QML theme on a Qt 6 player, open **Theme → Gradients**. The
editor discovers verified compatible targets: player-window chrome instances
first, then Rectangle-based sidebar/top-bar backgrounds, the supported
button-background branches, existing gradient bindings, and native properties
typed `Gradient` (including button borders). Choose two `#RRGGBB` or
`#AARRGGBB` stops and a horizontal/vertical direction; the editor shows a live
preview before applying. Surfaces and theme-level gradient definitions are
listed first, and the filter box narrows the list by target name, QML file or
kind. Clear/apply staged solid-color edits first, then close BlueStacks before
writing. Pick **Sidebar (player window)** / **Top bar (player window)** to
change what BlueStacks actually shows.

A property typed `color` cannot hold a Gradient object. These edits therefore
modify the compatible rendering/Gradient binding while preserving the original
solid color. **Solid fallback** sets the selected gradient to `null`; it does not
restore an old multi-stop gradient definition. Text/icon colors and unsupported
controls remain solid. Button-background gradients intentionally use the same
stops in all supported rendering states; native border slots can be edited
individually. This is not an arbitrary CSS gradient string in a hex-color field.

Writes use validated Qt resource mappings, mapped growth when necessary,
targeted cache refresh and read-back verification, with the first `.bak`
preserved. Unknown/ambiguous layouts and incompatible QtQuick imports are not
modified. The initial editor supports two-stop linear gradients using QtQuick
2.12+ or Qt 6 APIs, not radial/diagonal gradients or every custom control type.
The regression suite renders synthetic horizontal/vertical/solid cases in Qt
and checks actual pixels; it never executes QML extracted from the user's EXE.

## Boot video and integrity

Boot patching uses the selected installation's `Assets/Promo.mp4`, not a drive-root URL. Registered QML resources are discovered by structure and controller content rather than only a fixed filename; raw and zlib source records are supported. When multiple controller variants coexist, a unique reference from the host's boot Component is used where available. MSI/BlueStacks `PlrBootProgressBar.qml` resources are supported; QtMultimedia imports can be discovered in another validated QML file in the same resource pool, so a stock `BootVideo.qml` is not required. It updates an existing `Video`/`MediaPlayer` resource when available; an inline QtMultimedia fallback is used only when an existing resource already imports that module. A QML `Loader` is never pointed directly at an MP4. Minified/multiline boot objects are handled by bounded object/property parsing. Validated compressed resources can grow into mapped read-only PE storage, and their resource timestamps/compiled caches are refreshed. Unknown players and unverified resource mappings remain non-writing errors.

Integrity patching follows `E8 ???????? 84 C0 74 ??` in `.text`, with bounded RIP-relative references to the verify/fail messages when disambiguation is needed. Natural NOP sequences are not counted as clean candidates. Already-applied patches are verified by a message reference or matching backup evidence. Only the two JZ bytes are NOPed; dry-run is read-only, the first backup is preserved, and writes/restores are verified rather than truncating/deleting the EXE first. Close BlueStacks yourself before patching; the tool does not forcibly terminate your sessions. Full-backup restoration also rolls back theme/boot edits made after that backup.

Theme/boot resource validation also reads supported `qRegisterResourceData` argument sequences and embedded binary-QRC headers. This allows verified noncanonical node ordering/unused nodes without treating arbitrary offsets as resource pointers. Unrelated opaque resource codecs no longer invalidate an otherwise bounded resource tree. The edited resource's bytes, aliases and cache records are still verified. `--theme-probe` includes resource mapping evidence; a screenshot alone cannot identify every unsupported executable layout.

## FPS slider

Supported targets (64-bit x86 Windows DLLs):

- **Qt5QuickTemplates2.dll**
- **Qt6QuickTemplates2.dll**

These contain `QQuickSlider::setTo` / `QQuickRangeSlider::setTo`. **libOpenglRender.dll is not the FPS target.**

**FPS auto-detection is enabled by default.** The app finds BlueStacks/MSI installations under the real Program Files locations and Windows registry paths. When several installations exist, choose one with **Use** on the Dashboard; the app never patches a randomly chosen installation. It reads the selected HD-Player.exe's actual normal/delay imports (and bounded local UI dependencies when necessary) to detect Qt 5 or Qt 6, then selects that installation's matching slider DLL. Actions recheck the target after in-place updates, and stale saved DLL paths are refreshed. If no Qt import evidence is available, a single local DLL version can be selected; two versions or conflicting evidence require a manual choice. No fallback to a different installation or wrong Qt major is allowed.

Use **Auto-detect** on the FPS page to refresh the selection. Disable the automatic-selection checkbox only when deliberately selecting a DLL manually. File-dialog paths are decoded through QUrl, so spaces, Unicode and UNC paths are preserved.

Choose a high-FPS maximum from 60–1440, close BlueStacks, and patch. Only the 240 FPS high-frame-rate maximum is replaced. Normal mode stays at its default **60 FPS**, with no normal-mode setting or rewrite. Other slider values are unchanged. This changes the settings slider's upper limit, not a guarantee of in-game FPS.

A per-DLL `.bak` supports restoring the original. Unsupported setter code and backups from a different DLL version are refused. Older high/normal-mode detours (including the `read it.txt` reference) and previous max-only patches can be upgraded using their matching clean backup.

The patcher decodes complete supported x64 entry instructions instead of requiring one exact five-byte prologue. It supports common MSVC/MinGW stack-save/frame variants, relocates RIP-relative operands, and preserves CET landing instructions. Compiler entry jumps are followed only when their destination is a verified PE runtime-function entry; unknown third-party detours are not overwritten.

If Analyze reports a **recognized partial FPS patch**, keep the matching clean `.bak` and click **Patch FPS**. Both setters are rebuilt together; no unknown setter is silently skipped. If a build is still unsupported, the selectable State message includes the exact setter, RVA and entry bytes. Copy that message and provide the affected DLL and `.bak` for diagnosis—do not delete the backup to bypass the safety check.

## Build and verify

Requires CMake 3.21+, C++17, and Qt 6.5+ with Core, Gui, Qml, Quick, QuickControls2, and Svg. **Qt 6 builds the customizer UI; it does not limit FPS patching to Qt 6 installations. Qt 5 and Qt 6 target DLLs use the same patching engine.**

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Run the built executable with `--selftest`. On Windows with the Visual Studio generator:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
Start-Process build/Release/BSI-Customizer.exe -ArgumentList '--selftest' -Wait
```

The self-test covers resource detection/relocation, actual Qt QML loading in separate processes with disk caching enabled, repeated patch–launch cycles, embedded cache invalidation, palette parsing, and both FPS DLLs with MSVC/MinGW export names. On Windows x64 it executes controlled setter fixtures to verify that only 240 FPS is replaced while 60 FPS and other values (including NaN) pass unchanged. It also tests multiple compiler prologue styles, instruction boundaries, observable RIP-relative relocation, verified entry thunks, partial-patch recovery, legacy upgrades, repatching and exact restoration. Windows CI requires successful patch/repatch/restore checks on real **Qt 5.15.2** and **Qt 6.8.3** DLL copies, as well as the full Qt5/Qt6 × MSVC/MinGW fixture matrix. Neither modified DLL is loaded, and both originals remain untouched. No installed BlueStacks instance is exercised. GitHub CI runs the Windows build and self-test on pull requests.

The Qt 5 test reference is extracted from the official PySide2 5.15.2.1 Windows x64 wheel with pinned SHA-256 checks for both the archive and DLL. It stays in the ignored build directory and is never installed, linked, executed, committed, or bundled with the app. `BSI_TEST_QT5_REFERENCE` defaults ON in Windows GitHub CI and OFF for ordinary/offline builds. To run the same real-Qt5 check locally, configure with `-DBSI_TEST_QT5_REFERENCE=ON`. Alternatively, an ordinary build's `--selftest` accepts `BSI_FPS_QT5_DLL` pointing to a clean `Qt5QuickTemplates2.dll`; only a temporary copy is patched.

For an unsupported real EXE, collect a read-only diagnostic report:

```powershell
.\BSI-Customizer.exe --theme-probe "C:\Program Files\BlueStacks_nxt\HD-Player.exe"
```

## Third-party notices

The restricted x64 prologue decoder uses [HDE64 from MinHook v1.3.4](src/third_party/hde64/README.md), under its BSD license. The notice below is included in the README shipped with binary releases. CMake builds and installations also include `THIRD-PARTY-NOTICES.txt`.

<details>
<summary>HDE64 / MinHook BSD license notices</summary>

```text
﻿MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

</details>

A diagnostic EXE can be supplied with `-DBSI_EXE_TEST_FILE=<absolute-path>`. In GitHub CI, a user-supplied `HD-Player.exe` at the repository root is also recognized as optional diagnostic input. Self-tests only read it and patch disposable copies; they do not execute its native code or QML. The probe records its SHA-256 so results from different installations/builds are not confused. Diagnostic input is not a runtime dependency of the portable EXE.

Compatibility is version-adaptive, not a guarantee for every past/future build: FPS detours currently support Windows x64 Qt 5/Qt 6 setters, and boot rewriting requires a recognizable boot-controller/media API with a validated resource mapping. Unknown architectures, ambiguous runtime selection and unrecognized resource layouts are refused instead of reporting a false success. Integrity behavior is not changed by these detection improvements.
