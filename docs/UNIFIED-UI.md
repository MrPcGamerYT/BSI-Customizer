# 3.0.1: one app, original features, new presentation

## Normal entry point

Double-click the tested **portable EXE** or install the tested **setup EXE**.
`Main.qml` is the normal entry point, including in Chromium-capable builds.
No command-line flag, WebEngine runtime folder or “open full native editor”
switch is required. The original Discord login, role validation, settings and
Backend remain authoritative.

The supplied HTML is the visual reference, not a replacement patching engine.
The production shell uses its slim icon rail, compact navigation/target picker,
mint/glass palette, bundled Manrope/Jakarta fonts and 2×2 dashboard. All editor
pages below are the original QML components, in their original stable slots.
Editor state is retained in the same StackLayout rather than recreated on each
navigation. Resources scans remain manual.

## Feature parity map

| Slot | Existing editor reused unchanged | Features retained |
|---|---|---|
| 0 | DashboardPage (presentation refreshed) | Detect/use installations, target paths, instance detection |
| 1 | ThemePage | Auto/QML/Hex, presets, individual edits, toggles, gradients, reload/stale detection |
| 2 | ResourcesPage | Manual scan, search/kind filters, original preview/text editor, budgets, replacement/extraction |
| 3 | FpsPage | Existing DLL detection, analysis, FPS patch and restore |
| 4 | IntegrityPage | Existing inspect, patch and restore |
| 5 | BootVideoPage | Existing boot timing patch and validation |
| 6 | UiPage | Removals, layout, **sidebar ordering**, About text and link controls |
| 7 | RpcidPage | Discord IDs, resource names, dry runs, patch/undo |
| 8 | AppearancePage | Dark/light/system theme and accent |
| 9 | AboutPage | Existing version, update/download/install and diagnostics UI |
| 10 | GifRpcPage | Original GIF RPC configuration, inspect/apply/restore |
| 11 | WorkspacePage | Original profiles, queues, dry run/apply/history, image/media utilities |
| 12 | DebloatPage / RootVhdBrowser | Original Debloat, Android/ADB cleanup and complete disk browser operations |

Cache purge remains in the top-bar menu, with its original confirmation dialog.
Sign-out is in that menu too. The original worker/busy modal, activity log,
backup rules, capacity checks and native resize/window controls are retained.

## What was deliberately not rewritten

No binary patcher, Backend operation, AuthManager policy or update verification
was replaced for this UI consolidation. In particular the HTML forms do not
become another source of production form state or feature coverage. The
experimental Chromium adapter remains available only via explicit `--web-ui`
in builds that compile it; it is not the normal full-feature download.

## Verification

- `tests/workspace_qml_lint_test.py`: real Qt 6.8 metadata over every page/control.
- `--selftest`: loads the real production Main.qml, checks all 13 original
  editor objects and stable navigation slots, rejects invalid routes, verifies
  manual-scan behavior and theme propagation; existing engine fixtures still run.
- CI: native Windows build/self-tests and Linux Resources/search/runtime tests.
- Portable/installer workflow: packages this same Main.qml and verifies the
  deployed runtime/installer using the existing acceptance tests.
- Chromium workflow remains a separate regression check for the optional adapter.

These automated checks do not prove every emulator version or a real Discord
OAuth session. If a target-specific crash persists, record the exact feature,
build/target version and local diagnostics log; do not retry an interrupted
write without inspecting the target and backup first.
