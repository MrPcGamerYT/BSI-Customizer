# Chromium workspace — BSI Customizer 3.0.1

The user-supplied `gemini-code-1793470243134.html` is the visual reference.
The BSI Lab presentation keeps the HTML interactions. This integration uses
bundled local fonts, CSS/JS and real BSI engine calls, not its Python bridge,
placeholder "EXE found" state, or a replacement login provider.

## Architecture and current status

```
Unchanged native LoginGate.qml + AuthManager (Discord PKCE/session/access rules)
                                  |
                         authorized native shell
                                  |
Local Qt WebEngine / Chromium HTML + JS <-- Qt WebChannel --> WebBridge
                                                              |
                                        existing Backend / native engines
```

The normal Windows portable/installer workflow now builds this frontend with
`BSI_CHROMIUM_UI=ON`. It opens by default, after the unchanged Discord login.
This corrects the earlier native-QML approximation; see [UNIFIED-UI.md](UNIFIED-UI.md).
`--native-ui` remains an explicit recovery option and native-only developer builds
remain supported. Feature forms no longer hand normal work to another editor.

Implemented HTML flows cover 13 areas using the existing engines:

- Overview, detected installations/instances and native activity.
- Theme Auto/QML/Hex loading, individual staging/deselection, presets, legacy
  preview, dry run, toggle SVG recoloring and per-surface gradient controls.
- Explicit **Scan Resources**, the original search semantics (quoted phrases,
  min/max sizes, kind/font/modified filters), pagination, text/raster inspection,
  cached encoded-size measurement, native save/replace/extract and bulk extract.
- FPS detect/analyze/patch/restore and auto-detection preferences; native integrity
  check/patch/restore; boot patch/cache purge and staged boot profile settings.
- Player removals, supported layout tweaks, sidebar ordering/hiding/reset, About text/link/visibility editing,
  About report and profile-section staging.
- Offline Discord application ID find/status/replace/dry run/undo and Windows
  Explorer/Task Manager version-resource name editing.
- Animated presence settings, explicit proxy inspection/install/restore, profile
  staging and IPC diagnostics (without pretending config means connection).
- Portable profile import/export, EXE inspection/diagnostics, resource replacement
  queueing and original/replacement inspection, theme queueing, native Dry Run,
  hash-bound batch Apply, journal history/restore, image preparation and MP4
  metadata inspection.
- Dark/Light/System theme/accent, target paths, installation selection, backup
  restore, auto-update preference, download/verified apply and helper diagnostics.

- Debloat config options/apply/restore, live Android/ADB scan/package and storage
  cleanup/undo, image/archive inspection, Root.vhd/VHDX browsing, extraction,
  write enabling, add/replace/mkdir/rename/move/copy/delete and journal undo.

The MP4 preview uses HTML controls and the original Qt Multimedia VideoOutput
for its pixels, composited in the HTML preview rectangle. File URLs are not
served to Chromium. It pauses/hides when leaving the page, opening dialogs,
running native work or signing out. Metadata inspection and decoder playback
are not claims about emulator playback.

The merge does not alter the patch engines, AuthManager or SessionStore.
Browser tests use a mock transport; native build/engine fixtures and deployed
Windows renderer tests are separate checks, not real Discord OAuth coverage.
See [tool-to-engine mapping](CHROMIUM-MERGE-MAP.md).

## Invariants

- The renderer never receives `AuthManager`, auth configuration, PKCE material,
  session-signing keys or updater credentials. No generic reflection, shell,
  Python interpreter, localhost HTTP server or arbitrary backend evaluation API.
- Every bridge command and file dialog is authorized again in C++. Backend's
  existing checks remain in place. A saved session alone never unlocks the UI.
- Resource navigation and snapshots do **not** scan. Only the explicit native
  `scanResources` command, called by **Scan Resources**, initiates a scan.
- Busy/re-entrant bridge calls are refused. Backend validation, backups, encoded
  capacity checks and file conflict/process checks remain authoritative.
- Engine refusal maps are errors, not successful UI toasts. Edited source is
  rendered as text, never inserted as executable HTML. Preview images are raster
  data URLs. No external fonts/CDN or player-supplied script is executed.
- Chromium's request interceptor permits only bundled UI/WebChannel resources
  and image data. The native OAuth browser handoff/updater network logic is not
  routed through or changed by this policy.
- The default web profile owns its request interceptor. Native lifetime and
  operation guards must be preserved when adding adapters.

## Build and deployment

Use the project's supported Qt 6 toolchain, plus **WebEngineQuick and WebChannel**
(Qt installer modules `qtwebengine qtwebchannel`, and their dependencies).
The Windows workflows install Qt 6.8.3 and `qtpositioning qtserialport` as well; the NMEA Positioning plugin needs SerialPort.

```powershell
cmake -S . -B build-web -A x64 -DBSI_CHROMIUM_UI=ON
cmake --build build-web --config Release --parallel
.\build-web\Release\BSI-Customizer.exe
```

A Chromium desktop app is **not just its inner EXE**: deploy the matching
`QtWebEngineProcess`, Chromium `.pak`/ICU data, locales, plugins and QML imports.
The standard packager scans both native and Chromium QML directories, validates
helper/resources/locales, and launches the packed renderer with the SDK removed
from PATH. The dedicated Chromium workflow also tests its full deployment.
Do not reuse a native-only payload or strip helpers to reduce size. Do not turn
off sandboxing to hide deployment/startup failures. The portable outer EXE must
unpack the matching runtime. The packager collects license/attribution data from the exact Chromium commit
pinned by the matching Qt release, in addition to Qt's SPDX/attribution
collection. Only metadata/notice blobs are fetched by Git's partial-clone
protocol; sources are not checked out or executed. No unavailable Qt WebEngine
documentation module or multi-gigabyte Chromium source download is needed.

## Verification

The shell source lives under `src/chromium/ChromiumMain.qml`, with a
resource alias preserving its runtime URL under `src/qml`. Native-only developer builds scan `src/qml`; Chromium packaging also scans
`src/chromium`, deploying both frontends. The QML metadata test checks both paths.

- `node tests/chromium_ui_test.cjs`: Playwright browser tests with an explicit
  test-only transport double. Covers all 13 routes, offline lock, scan behavior,
  pagination/search, safe text rendering, command arguments, refused operations,
  About visibility inversion, RPC/GIF settings, boot profile staging, gradients,
  profile import, hash-bound batch apply, failed-dry-run revocation and sign-out. Requires `playwright`; optional `BSI_PLAYWRIGHT` specifies its
  module path. This does **not** certify C++ integration or patching a real EXE.
- `node tests/chromium_contract_test.mjs`: checks bridge-command guard-test coverage,
  search equivalence to the existing QML implementation and embedded UI assets.
- `python tests/workspace_qml_lint_test.py`: checks all QML, including the new
  shell, against real Qt metadata using the existing PySide test toolchain.
- A Chromium-enabled native build adds `--web-bridge-test`: every bridge command,
  snapshot and file selection must remain locked without verified auth; isolated
  temporary settings, no test login bypass.
- `--chromium-runtime-test`: loads real packaged HTML/CSS/JS/WebChannel assets in
  Chromium with **no AuthManager, Backend or privileged endpoint constructed**.
  Confirms renderer operation and offline-only controls. It cannot patch files.
- `.github/workflows/chromium.yml` separately builds/tests Chromium on Windows;
  it does not publish a release or embed credentials. Authenticated bridge/editor
  E2E tests and actual patcher regressions remain required before promotion.

Local browser tests and Qt metadata lint can pass without a C++ SDK. Do not report
that as a Windows native build, successful sign-in or real executable patch test.

## Future backends

`transport.js` is a versioned adapter boundary: `snapshot`, `invoke`, signals and
file selection. C++ `WebBridge` owns the explicit command allowlist and delegates
to current engines. Further backends can implement this protocol, but **must not**
move access decisions into JavaScript or silently downgrade native validation.
No additional language/backend has been implemented in this initial migration.

For visual review only, serve `src/web` locally and select **View design preview**.
The persistent banner and disabled native buttons make clear that a normal
browser has neither a login session nor access to the native backend.

## Discord sign-in in downloadable test bundles

The Chromium Actions workflow uses the same `ZLP_AUTH_JSON` repository secret
(`BSIC_AUTH_JSON` alias) as the native release. It configures
`BSI_REQUIRE_EMBEDDED_AUTH=ON`, embeds the public-client/server-role policy,
and verifies the loaded embedded configuration before uploading a test bundle.
A missing configuration now fails the build instead of producing a download
that shows “Discord sign-in is not configured yet”. Fork PR builds without
access to this secret cannot produce this sign-in-ready artifact.

Do not put an OAuth client secret, bot token, or user session into this blob.
Desktop Discord sign-in uses public-client PKCE. The guild/role authorization
checks remain enabled, and local sidecar/environment policy overrides are
rejected for these bundles just as they are for native releases.

Already downloaded bundles do not change when the workflow is fixed: download
and use the newly verified standard portable/installer artifact. The HTML frontend now opens by default.
This verification checks configuration and locked bridge behavior offline;
it does not claim to have completed a real Discord OAuth sign-in.

## Reference UI refresh

The workspace follows the supplied `gemini-code-1793470243134.html`: mint glass
background and orbs, line-icon rail, compact navigation, target switcher,
two-by-two dashboard, status sidebar, and recent activity. Manrope and Plus
Jakarta Sans are bundled locally with their OFL licenses; no Google Fonts/CDN
requests are needed. The original prototype's Python bridge, simulated state,
and remote dependencies are not used.

The prototype's backup tile is replaced by the real Patch workspace rather
than inventing an unsupported backup-only operation. Instance detection moved
to Settings to keep the dashboard uncluttered. All existing editor commands,
manual resource scans, authorization and native validation remain in place.

### Crash triage and recovery

Renderer termination now offers reload and native-tool recovery instead of
leaving a blank workspace. Sign-out defers WebEngineView destruction until the
WebChannel invocation unwinds. Bridge operations convert C++ exceptions to
visible failures; inline resource previews above 8 MiB are refused rather than
copied into the renderer. These changes do not promise recovery from an access
violation or that an interrupted patch succeeded: inspect the file/backup
before retrying any write.

Windows records unhandled exception code/address in
`%LOCALAPPDATA%/MrPcGamerYT/BSI Customizer/diagnostics/last-crash.log` when writable.
It captures no memory dump, OAuth state or file contents and is never uploaded.
If a crash persists, retain that log and the exact action/target/build version.
Use `--native-ui` to compare the native shell. Real emulator-specific crashes
still require reproduction on the affected machine.
