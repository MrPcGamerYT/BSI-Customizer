# Make a Windows installer (3.0.1)

The project already uses **Inno Setup 6**. Setup installs the tested single-file
portable EXE, creates shortcuts and an uninstaller, and keeps the same AppId for
upgrades. It does not require Qt on the end user's PC.

## 1. Get the right portable EXE

Use the **new build of your feature branch**, not the old main release. Changes
to resource previews, frame-rate profiles and ADB discovery require rebuilding
this source; putting new JS beside an older embedded EXE will not update it.

Simplest: get the portable artifact from this branch's **Single EXE Release**
Actions run (a branch run builds test artifacts, **not a public release**). You
need repository access to download private-repository artifacts. Extract it and
keep `BSI-Customizer-3.0.1-win64.exe`. Do not package an old setup EXE as the app.

Alternatively build on Windows x64 using the prerequisites and commands in
[PORTABLE.md](PORTABLE.md#local-build). Use MSVC Build Tools, PowerShell 7, CMake,
Qt 6.8.3 MSVC x64 with WebEngine/WebChannel, Multimedia, Positioning, SerialPort,
and Git. In a VS developer PowerShell, with your installed Qt prefix:

```powershell
cmake -S . -B build -A x64 "-DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64" -DBSI_BUILD_PORTABLE=ON -DBSI_CHROMIUM_UI=ON -DBSI_REQUIRE_EMBEDDED_AUTH=ON
cmake --build build --config Release --parallel
```

Production builds must supply your existing embedded Discord public-client / role
policy as documented in [AUTH.md](AUTH.md), e.g. through the owner's local
`ZLP_AUTH_JSON` environment. Keep secrets/configuration out of commits and chat.
Do not disable the auth requirement to produce a distributable build.
Qt/WebEngine takes significant build and staging space. **Installer-only packaging
below does not need the Qt SDK** if you already have the correct portable EXE.

Expected portable input: `build/artifacts/BSI-Customizer-3.0.1-win64.exe`.
`build/Release/BSI-Customizer.exe` is only the inner Qt application; do not
ship it alone.

## 2. Optional: provide ADB

For **your own running copy**, put matching trusted Windows files here:

```text
My Customizer/
  BSI-Customizer-3.0.1-win64.exe
  adb.exe
  AdbWinApi.dll
  AdbWinUsbApi.dll
```

An `adb/` subfolder containing those files also works. Restart the customizer,
sign in, then open **Debloat & Android → System & diagnostics → Read system & ADB
status** to see the resolved path. This check does not start ADB. Start the chosen
emulator instance and explicitly scan it to use ADB operations.

Lookup order: outer portable/install directory (`adb/adb.exe`, then `adb.exe`,
then `HD-Adb.exe` / `HD-Adb`), inner runtime directory, then selected player
folder. The current working directory and PATH are never searched. Existing
private-server / loopback-instance behavior is unchanged; other ADB servers are
not killed. The launcher supplies its real module path rather than argv[0], and the inner app
validates it against the actual parent process (not just inherited environment), so
this also works when started from a shortcut or a different working directory.
The app and its ADB child run elevated: use only a trusted, owner-controlled
folder and verified supplier binaries. A different user's writable shared folder
is not appropriate. No ADB download is built into the app.

For an **installer with ADB**, copy a matching supplier set into `tools/adb/`:

```text
tools/adb/adb.exe
tools/adb/AdbWinApi.dll
tools/adb/AdbWinUsbApi.dll
tools/adb/NOTICE.txt
```

The new helper detects this folder automatically. Or pass `-AdbDirectory` to a
folder outside the repo. All four files are required when bundling. The installer
copies only those four files into `{app}/adb/`, not the whole SDK or private ADB
keys. Check the supplier's licence / redistribution terms; supplying a notice does
not itself grant redistribution permission. These binaries are not included here.
The existing CI installer stays ADB-free unless explicitly given an ADB directory.

## 3. Compile setup

Install **Inno Setup 6** on Windows. In PowerShell 7 from this repository root:

```powershell
pwsh -File .\scripts\Build-Installer.ps1 `
  -PortableExe .\build\artifacts\BSI-Customizer-3.0.1-win64.exe `
  -Version 3.0.1
```

With ADB outside the repo or a custom output folder:

```powershell
pwsh -File .\scripts\Build-Installer.ps1 `
  -PortableExe 'C:\My Builds\BSI-Customizer-3.0.1-win64.exe' `
  -Version 3.0.1 `
  -AdbDirectory 'C:\Trusted Tools\platform-tools' `
  -OutputDirectory .\build\installer
```

Default result: **`build/installer/BSI-Customizer-3.0.1-setup.exe`**. The helper
checks the portable launcher using `--deployment-test`, invokes the existing
`.iss`, fails on compiler errors and prints SHA-256. Run it only on a trusted EXE;
that validation executes the supplied program. For a nonstandard Inno location,
pass `-IsccPath 'C:\...\ISCC.exe'`.

The equivalent Inno command (without the helper's validation) is:

```powershell
$exe = (Resolve-Path .\build\artifacts\BSI-Customizer-3.0.1-win64.exe).Path
$out = [IO.Path]::GetFullPath('.\build\installer')
New-Item -ItemType Directory -Force $out | Out-Null
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" "/DAppVersion=3.0.1" "/DSourceExe=$exe" "/DOutputDir=$out" '.\packaging\installer\BSI-Customizer.iss'
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }
```

Add `"/DAdbDirectory=C:\Trusted Tools\platform-tools"` to that direct command to
include the four external ADB files. The existing setup source is
`packaging/installer/BSI-Customizer.iss`; do not replace its stable AppId for every
build. Neither command signs the EXE or publishes a release. Authenticode signing
requires your own certificate and is separate from a successful build.

## 4. Test before sharing

Use a disposable Windows VM: install, launch/sign in, check ADB discovery from a
shortcut, manually scan Resources, inspect an SVG and PNG, stage a frame profile,
apply it with the player closed, restore, upgrade the same installation, and
uninstall. `scripts/Test-Installer.ps1` and the release workflow contain existing
install/upgrade/uninstall acceptance tests, restricted to explicitly marked
disposable hosted CI machines; do not run them against your own registration.
Optional ADB packaging and real device connections additionally need a trusted
ADB set and running emulator; source/browser tests cannot prove those.
