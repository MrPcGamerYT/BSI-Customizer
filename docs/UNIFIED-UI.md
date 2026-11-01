# 3.0.1: actual HTML presentation, original BSI engines

## Normal entry point

The Windows portable/installer workflow now builds with `BSI_CHROMIUM_UI=ON`.
Double-clicking that build opens `ChromiumMain.qml`: the existing native Discord
login gate followed by the **actual HTML/CSS/JavaScript workspace**. The supplied
`gemini-code-1793470243134.html` is the presentation reference, not a Python bridge
or a replacement EXE patcher. Its original concept credit is retained.

This supersedes the previous native-QML approximation. The HTML frontend has the
glass shell, local Manrope/Jakarta fonts, hover/transitions, profile menu, custom
HSV/HEX picker, styled asynchronous confirmation dialogs, page-entry animation,
and delayed minimize/close effects. Reduced-motion preference is respected.
Layout and controls may differ to accommodate BSI's original workflows safely.

There are no ordinary “full native editor” buttons inside the feature forms.
`--native-ui` and the renderer-crash recovery button remain explicit emergency
paths, not the normal feature implementation. The legacy `--web-ui` flag is no
longer required in Chromium-enabled builds. Native-only developer builds still
work when configured without WebEngine.

## Feature-to-engine map

| HTML page | Existing native operations retained |
|---|---|
| Overview | Install/instance detection, target selection and paths, backup restore |
| Theme | Auto/QML/Hex, palettes/presets/staging, toggles, gradient load/apply, preview/dry run |
| Resources | **Manual Scan Resources**, original search semantics, pagination, budgets, text save, replace and extraction |
| FPS | Existing DLL detection, analysis, patch/restore and detection preference |
| Integrity | Existing inspect, patch and restore |
| Boot | Original timing patch, cache purge and profile staging |
| Player UI | Original removals, layout tweaks, About text/link/visibility, sidebar row ordering/hiding/reset |
| Discord IDs | Find/status/replace, dry run/undo, Windows resource names |
| Interface | App theme/accent, target paths and installation selection |
| Build | Update preference/check/download/verified apply, trust and helper diagnostics |
| GIF RPC | Existing proxy inspection/install/restore, configuration/profile and IPC diagnostics |
| Workspace | Profiles, queues, hash-bound dry run/apply, journals, image preparation, MP4 inspection and Qt decoder preview |
| Debloat & Android | Original config cleanup/restore, live ADB package/storage cleanup/undo, Root.vhd/VHDX/archive tools |

The disk tools reuse `Backend::rootvhd*`, including read-only/dirty-journal
refusals, explicit write enabling, file/folder listing and search, extraction,
add/replace, folder creation, rename/copy/move/delete, undo and journal discard.
Listings are paginated in HTML. No image scan runs on page entry. Sidebar and
Debloat options are read explicitly from the native engine; not invented by JS.

The MP4 preview's controls are HTML. Only the video pixels are composited by
`VideoOutput` using the **same Qt Multimedia decoder as the original editor**.
No local-file browser permission, new HTTP service, replacement codec or generic
filesystem endpoint is introduced. The preview pauses/hides on navigation,
dialogs, busy engine state, and sign-out. Decoder playback does not certify the
emulator's playback; there is no automatic encoder/converter.

## Boundaries

- No patch engine, Backend operation, AuthManager policy, session store or
  updater signature verification is replaced.
- Every bridge command/file picker is re-authorized natively and refuses busy /
  re-entrant calls. Renderer state never grants production access.
- Confirmations are presentation only. Cancelling sends no write. Native
  validation, backups, capacity checks and recovery journals remain authoritative.
- Renderer crashes never automatically retry a write. Inspect the target and
  backup before retrying an interrupted operation.
- A disconnected browser preview cannot patch files or bypass Discord login.

## Packaging and verification

The standard Windows workflow deploys WebEngine's helper, matching ICU/PAK data,
locales, QML imports and runtime DLLs inside the portable payload/installer.
Matching Qt-generated Chromium third-party notices are bundled, not omitted to
shrink the download. A clean-PATH smoke test launches the packed renderer, not
just the inner SDK build. The dedicated Chromium workflow additionally checks
locked bridge access and renderer recovery. Neither feature-branch workflow
publishes a public release.

Automated browser tests use **test-only mock transport**, checking request
arguments and UI behavior; they are not authenticated C++ engine end-to-end
certification. Native engine fixtures and Windows deployment tests remain
separate. Real Discord OAuth and every emulator version still require target-PC
verification. See [CHROMIUM-UI.md](CHROMIUM-UI.md) for the bridge/security design.
