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

Two workflows share the same sources:

- **`.github/workflows/ci.yml`** builds the application on Windows, verifies the
  embedded administrator manifest, runs the engine self-test and the packaging
  script's own offline self-test. It is the fast check for pull requests and
  manual runs.
- **`.github/workflows/release.yml`** additionally bundles and tests the
  one-file EXE and can publish it.

- Pushes to `main`, pull requests and manual runs build the app, bundle/test the
  one-file EXE, run the full engine tests (including Qt 5/Qt 6 checks), and upload
  exactly one EXE.
- Matching `v<version>` tag pushes and manual runs with **publish_release** enabled
  publish that tested EXE to GitHub Releases.
- Ordinary push/PR builds and the default manual run never create a release/tag.

Keep `scripts/`, `packaging/portable/`, `cmake/` (`Qt5FpsReference.cmake`,
`CollectQtNotices.cmake` and its test), the application sources, tests and
license files: these are build dependencies, not loose files users need
alongside the final EXE.

Use **Actions → Single EXE Release → Run workflow**, select the source branch,
leave **publish_release** unchecked for a test build, and select the runner:

- **windows-latest** uses GitHub's hosted Windows runner. On private repositories,
  this is subject to the account's included minutes, spending limits and payment
  status. A workflow change cannot bypass a billing restriction.
- **self-hosted** uses a Windows x64 PC that you register under **Repository
  Settings → Actions → Runners → New self-hosted runner**. Follow GitHub's
  registration commands on that PC; keep the runner online while building.
  This avoids hosted-runner charges, but uses your PC's CPU, disk and internet.
  Install Visual Studio 2022 C++ build tools (including the Windows SDK), CMake,
  PowerShell 7, Git, Python 3 and 7-Zip first. Inno Setup 6 is recommended;
  otherwise the workflow needs Chocolatey and permission to install it.
  The workflow downloads Qt itself. Run only trusted code on this PC; automatic
  PR builds cannot select this runner. Nothing here registers a PC automatically.

The updated workflow must first exist on the repository's default branch for
new manual-run inputs to appear in GitHub's UI. Until then, PR builds exercise
this branch's workflow automatically on the hosted runner. Do not publish a
release or change repository visibility just to obtain a test EXE.

After a successful build:

- The Actions artifact download is wrapped in a ZIP by GitHub's artifact service;
  it contains **one EXE**, not a folder of dependencies. Find it under
  **Artifacts** at the bottom of the run's summary. Test artifacts expire after
  **three days**; download promptly. Artifact uploads still depend on the account's
  Actions storage allowance, including when using a self-hosted runner. If an
  upload fails on your self-hosted PC, the EXE remains under `build/artifacts/`
  inside that runner's checkout.
- Test builds also upload a separate **setup** artifact containing the tested
  `BSI-Customizer-<version>-setup.exe` installer (three-day retention). Choose
  this artifact when testing installation, shortcuts or installed-app updates.
- Both test ZIPs use standard Deflate compression and names containing the run
  ID **and attempt**, so a rebuild does not reuse a previous download. Before
  the job can pass, it **downloads both published ZIPs back from GitHub**, checks
  the ZIP structure/CRC and uploaded archive SHA-256, requires exactly the tested
  EXE with its recorded size/SHA-256, then extracts again with Windows
  `Expand-Archive` and compares the extracted EXE hash. This tests the delivered
  archives, not just the pre-upload build. ZIP and inner-EXE hashes/sizes are
  recorded separately in the run summary and check annotations. No extra files
  or nested ZIPs are added to either artifact.
- A published GitHub Release exposes the **`.exe` directly** as its download.

The download verifier is a CI/developer tool, not a recipient dependency. Its
19 offline tests cover truncation, CRC/SHA mismatch, HTML/error-page downloads,
wrong/extra/linked members, unsupported ZIP encodings, size limits and transport
credential separation. It never executes a member. A GitHub token is sent only
to the GitHub API, never to the signed storage download URL. A locally downloaded
file that is only a small HTML/JSON error page is not the advertised ZIP; check
repository access/sign-in and compare its size/hash rather than running it.

Set the `project(... VERSION ...)` value in `CMakeLists.txt` before pushing a
matching tag, e.g. `v2.0.1` for project version `2.0.1`. Mismatched versions are
refused. A manual publication will not reuse a tag belonging to another commit;
bump the version instead of replacing an old release with different source.
SHA-256 is recorded in the workflow summary and verified before publication.

## Local build

Requires Windows x64, MSVC C++ build tools, CMake 3.21+, Qt 6 development tools
and PowerShell 7 — no interpreter, runtime or extra tool beyond those.
`makecab.exe` and the cabinet reader are built into Windows. The workflow uses
Qt 6.8.3 with Multimedia, WebEngine, WebChannel, Positioning and SerialPort
(the deployed NMEA Positioning plugin depends on it). For packaging,
Git must also be on PATH to collect matching Chromium license/attribution
blobs (a small partial fetch, not a checkout of Chromium source). Native-only developer builds can explicitly
set `BSI_CHROMIUM_UI=OFF`.

```powershell
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release -DBSI_BUILD_PORTABLE=ON -DBSI_CHROMIUM_UI=ON
cmake --build build --config Release --parallel
```

### Experimental native shader payloads

The revised Single EXE test workflow builds/tests separate **Qt 5.15.2 and Qt 6.2.4**
public-API build baselines for regular x64 release MSVC shader modules, and embeds them inside the inner application
and requires clean-PATH bundle checks in the packed/extracted EXEs. They are
**not installed into a player automatically**. See the
[native Shaders user verification guide](SHADERS-USER-TEST.md); real game/GPU
validation is still pending. New V3 modules can qualify on equal/newer same-major
runtimes only after full provider/symbol checks; old V2 remains exact-version only.
See [Qt evidence and limits](EMULATOR-QT-COMPATIBILITY.md). Incompatible/unknown
installations refuse without replacing runtimes.

To include the same experimental payloads in a local native build:

```powershell
./scripts/Build-ShaderBundle.ps1 -Qt5Prefix "C:/Qt/5.15.2/msvc2019_64" `
  -Qt6Prefix "C:/Qt/6.2.4/msvc2019_64" -BuildRoot build-shader-payloads `
  -OutputDirectory build-shader-bundle
cmake -S . -B build -A x64 -DBSI_BUILD_PORTABLE=ON -DBSI_CHROMIUM_UI=OFF `
  -DBSI_REQUIRE_SHADER_BUNDLE=ON -DBSI_SHADER_BUNDLE_DIR="$PWD/build-shader-bundle"
cmake --build build --config Release --parallel
```

Use a new/empty output directory. The shader script defaults to **Visual Studio
2022 / v143** explicitly: Qt 6.2.4 headers use `stdext` iterators removed in the
VS 2026 STL. The release/bridge jobs use `windows-2022`, not the moving latest
image. Do not patch SDK headers or relabel a newer Qt DLL to conceal this build
incompatibility. A self-hosted builder needs this toolchain installed; optional
`-Generator` / `-Toolset` parameters are for explicitly provisioned compatible
builders, not a runtime compatibility override. The script also pins each
iteration's CMake prefix and Qt 6 host/CoreTools paths: setting `Qt6_DIR` alone
allowed the application's 6.8.3 tools into a 6.2.4 configure. A packed
`BSI_SHADERS_EXPECT_QT_VERSION` compile-time assertion independently refuses
wrong-version headers instead of trusting only the selected qmake.

Ordinary developer/CI builds can omit this
optional bundle: safe removal/recovery remains available, but install is disabled
with an explicit report. No Qt 5 library is linked into the Qt 6 Customizer. The
player receives only the matching regular module, qmldir, ownership record and
complete third-party notices, never the app's Qt/VC runtime. Build tools/SDKs are
not recipient dependencies. The official experimental test workflow requires a
valid bundle instead of silently producing an installer that cannot install it.

The `BSI-Portable` target:

- stages `windeployqt` output and app-local CRT DLLs (never `vc_redist.exe`);
- fixes `qt.conf` to use the packaged QML/plugin paths;
- checks every staged PE import for missing Qt/CRT/non-system dependencies;
- gathers Qt source license/attribution text with `cmake/CollectQtNotices.cmake`
  (a CMake script, verified offline by `cmake/tests/CollectQtNoticesTest.cmake`),
  plus matching Chromium source license/attribution notices and the app/HDE64 licenses;
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
