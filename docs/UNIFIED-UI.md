# 3.0.1: actual HTML presentation, original BSI engines

## Normal entry point

The Windows portable/installer workflow now builds with `BSI_CHROMIUM_UI=ON`.
Double-clicking that build opens `ChromiumMain.qml`: the existing native Discord
login gate followed by the **actual HTML/CSS/JavaScript workspace**. The supplied
`gemini-code-1793470243134.html` is the presentation reference, not a Python bridge
or a replacement EXE patcher. The presentation now uses BSI Lab branding and named tool navigation.

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
| Binary Lab | Explicit read-only PE sections, DLL imports, named exports, Win32 resources, bounded ASCII/UTF-16LE strings, raw hex, new-file-only section/resource/overlay and JSON exports |
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
Matching Chromium vendored-source license/attribution notices are bundled, not omitted to
shrink the download. A clean-PATH smoke test launches the packed renderer, not
just the inner SDK build. The dedicated Chromium workflow additionally checks
locked bridge access and renderer recovery. Neither feature-branch workflow
publishes a public release.

Automated browser tests use **test-only mock transport**, checking request
arguments and UI behavior; they are not authenticated C++ engine end-to-end
certification. Native engine fixtures and Windows deployment tests remain
separate. Real Discord OAuth and every emulator version still require target-PC
verification. See [CHROMIUM-UI.md](CHROMIUM-UI.md) for the bridge/security design.

## Binary Lab and detailed maintenance

Binary Lab does not execute DLLs, patch arbitrary offsets or promise to unpack
all protectors. Inspection is explicit and capped at 256 MiB. String scans cover
the first 32 MiB, at most 2000 matches; UTF-16LE scanning covers ASCII-range
characters at either byte alignment. Named exports and Win32 resource leaves
have 4096-entry budgets; tables paginate 100 rows. Hex reads return 4096 bytes.
Every follow-up request rechecks SHA-256 against the inspection. Exports require
a new output filename (native `NewOnly`), never replace the inspected source,
and extract raw bytes, not reconstructed ICO/BMP containers. Overlay bytes can
include Authenticode certificates or packer data; signature trust is not inferred.
Qt/qrc entries remain in the original Resource explorer, including its text
editing, capacity checks, replacement and bulk extraction.

Maintenance exposes original configuration groups, per-instance tuning, ADB
access, dedicated GPU preference, all installed packages with reviewed labels,
search, selection, storage warnings and restore. Selection is staging only.
Downloads backup does not back up Android/data or obb. No new arbitrary ADB
shell endpoint, placebo optimizer, automatic scan or blanket safe-removal claim
is introduced.

## Emulator outline icons

Resources now includes **Emulator icon studio** with 22 original BSI Outline
SVGs for the *player's* topbar/sidebar, not this app's navigation. It previews
trusted SVG templates, accepts a constrained color/stroke, and stages into a
selected text-editable SVG only. Numeric root dimensions are retained where
possible. Choosing, previewing and staging never scans or saves. Saving still
uses the existing `saveResource` native guard/writer, capacity checks, backup and
stale-file protections. No new filesystem or bridge endpoint is introduced.
Original native raster previews are used; untrusted source SVG is never inserted
into the DOM. Applying an icon does not remap its button or change bar spacing.
See `assets/emulator-icons/README.md` for mapping, variants and recovery notes.

## Resource previews, profiles and external ADB

Resources now displays native scan-cached SVG/raster thumbnails, with paginated
100-row rendering. Opening an SVG shows a Qt-rasterized saved preview **and** its
source editor; PNG/JPEG/WebP and other supported Qt image formats use the same
preview pane (static frame for animated formats). Fit/100%/200% refer to preview
pixels, with checker/dark/light backgrounds. Unsupported formats have metadata,
a bounded 256-byte decoded sample, and extraction; QML/scripts/fonts are never
executed by the preview. Existing native scan/decode budgets remain in force.
There is no scan on navigation/filtering. Draft source is not rasterized live.

Debloat / Android adds manual host/ADB diagnostics, fixed Windows Settings links,
and draft-only profile shortcuts. The new 60 FPS balanced / 30 FPS efficiency
profiles require the existing selected instance's three supported frame settings,
conflict with the 240 FPS mode and keep the original native config journal. Caps
are load/smoothness trade-offs, not guaranteed speed gains. Re-applying a different
profile preserves the original undo baseline. No CPU/RAM cleaner or security /
service disabling is implemented. Windows settings are opened, not auto-changed.

App-local ADB discovery supports both loose sidecars and `adb/`, including the
outer portable launcher's folder. Inherited launcher paths must match the actual
parent process; cwd/PATH are not searched. Real ADB binaries are external. See
[INSTALLER.md](INSTALLER.md) for exact installer commands and the optional ADB
allowlist. New native tests exercise discovery and frame plans; real emulator /
supplier-binary acceptance must still be performed on Windows.
