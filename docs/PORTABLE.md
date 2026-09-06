# One-file Windows release

The distributable is **`BSI-Customizer-<version>-win64.exe`**. It is a portable
application, **not an installer** and not a ZIP containing loose runtime DLLs.

## What happens when it runs

1. Windows shows the administrator prompt required for patching BlueStacks.
2. A small native launcher checks its embedded runtime archive, then unpacks it
   into a unique private temporary directory. Elevated runs use an
   Administrators/System-only directory under Windows Temp, not a writable
   per-user extraction cache.
3. The actual customizer starts with its bundled Qt DLLs, QML modules, plugins,
   SVG support and app-local Microsoft VC runtime. No Qt, Python, .NET, or VC
   runtime installer is needed on the user's computer (Windows 10/11 x64).
4. The launcher forwards arguments, preserves the working directory and exit
   code, waits for the app to close, and removes the runtime on normal exit.

The app still saves its normal preferences and the backups/exports requested by
the user. A crash, forced termination or power loss can leave a private temporary
runtime directory behind; this is not an installed copy or background service.
The EXE is unsigned unless the distributor signs it separately; Windows
SmartScreen may show a warning. This workflow does not disable security checks.

## GitHub Actions

There is one workflow: **`.github/workflows/release.yml`**. The redundant
`ci.yml` has been removed; do not keep an older copy alongside it.

- Pushes to `main`, pull requests and manual runs build the app, bundle/test the
  one-file EXE, run the full engine tests (including Qt 5/Qt 6 checks), and upload
  exactly one EXE.
- Matching `v<version>` tag pushes and manual runs with **publish_release** enabled
  publish that tested EXE to GitHub Releases.
- Ordinary push/PR builds and the default manual run never create a release/tag.

Keep `scripts/`, `packaging/portable/`, `cmake/Qt5FpsReference.cmake`, the application
sources, tests and license files: these are build dependencies, not loose files
users need alongside the final EXE.

Use **Actions → Single EXE Release → Run workflow**. After a successful build:

- The Actions artifact download is wrapped in a ZIP by GitHub's artifact service;
  it contains **one EXE**, not a folder of dependencies.
- A published GitHub Release exposes the **`.exe` directly** as its download.

Set the `project(... VERSION ...)` value in `CMakeLists.txt` before pushing a
matching tag, e.g. `v2.0.1` for project version `2.0.1`. Mismatched versions are
refused. A manual publication will not reuse a tag belonging to another commit;
bump the version instead of replacing an old release with different source.
SHA-256 is recorded in the workflow summary and verified before publication.

## Local build

Requires Windows x64, MSVC C++ build tools, CMake 3.21+, Qt 6 development tools,
Python 3.8+, and PowerShell 7. `makecab.exe` and the cabinet reader are built into
Windows. The workflow uses Qt 6.8.3.

```powershell
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release -DBSI_BUILD_PORTABLE=ON
cmake --build build --config Release --parallel
```

The `BSI-Portable` target:

- stages `windeployqt` output and app-local CRT DLLs (never `vc_redist.exe`);
- fixes `qt.conf` to use the packaged QML/plugin paths;
- checks every staged PE import for missing Qt/CRT/non-system dependencies;
- gathers Qt source license/attribution text, plus the app and HDE64 licenses;
- creates one LZX cabinet and embeds it as an EXE resource in a `/MT` native
  launcher that itself depends only on Windows system DLLs;
- verifies the administrator manifest and runs the portable smoke tests.

Output: `build/artifacts/BSI-Customizer-<version>-win64.exe`.
`build/Release/BSI-Customizer.exe` is the inner application, **not** the one-file
release. Do not distribute the inner EXE by itself.

Ordinary local builds keep packaging optional. Windows GitHub CI enables it by
default. Set `-DBSI_BUILD_PORTABLE=OFF` for a development-only build. Packaging
requires network access to acquire Qt licensing/source-attribution data on the
first run; archives are cached only in the ignored build directory. No downloads
happen when the delivered EXE is launched.

## What is tested

The portable EXE is copied **alone** into a new directory with spaces in its name
and launched with a clean `PATH`, no Qt SDK/plugin/import environment variables,
and both Windows/offscreen platform backends. The test opens the real QML UI,
loads the SVG plugin, checks that Qt/QML paths stay in the extracted bundle,
verifies quote/empty/trailing-backslash argument forwarding and normal cleanup,
and checks that no loose files are left alongside the EXE.

An explicit extraction test verifies the inspectable runtime and its licenses,
refuses overwriting an existing directory, and runs the extracted app directly.
A negative control removes `Qt6Quick.dll` from the disposable test copy: launch
must fail rather than finding a dependency in the installed SDK. Full patcher
self-tests run separately against the development build, where `rcc` and the
real Qt 5/Qt 6 reference DLLs are available. SDK tools/test fixtures are not
application runtime dependencies and are not bundled.

## Inspecting licenses or replacing Qt

```powershell
.\BSI-Customizer-2.0.0-win64.exe --portable-help
.\BSI-Customizer-2.0.0-win64.exe --extract-to "C:\Tools\BSI-unpacked"
```

The extraction destination must be a **new directory** with an existing parent.
No existing directory is merged into or overwritten. This mode does not launch
the app or delete the extracted files. Read `PORTABLE-README.txt` and `licenses/`,
replace Qt DLLs with an ABI-compatible modified build if desired, and run the
inner `BSI-Customizer.exe` directly. Qt remains dynamically linked and replaceable.
The launcher checks the embedded archive for corruption; it does not prohibit
running the separately extracted application with replacement libraries.
