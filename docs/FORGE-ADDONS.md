# Forge-inspired shell and Add-ons

The normal Chromium frontend and the `--native-ui` fallback share the original
C++ authentication, patching and recovery engines. The shell uses a stable,
labelled navigation rail, graphite surfaces, gradient background and backend logs.
The incomplete imported four-page QML routing is replaced with one working
instance of each editor. Customizer Settings remains a single draggable modal;
Backups and Add-ons are distinct destinations. No automatic resource scan was
introduced.

## Six scoped player collections

| ID | Palette | Button radius / height / horizontal padding |
| --- | --- | --- |
| carbon | OLED neutral | 4 / 32 / 12 |
| amethyst | Amethyst | 12 / 36 / 18 |
| arctic | Dark blue | 8 / 34 / 16 |
| ember | Red dark | 2 / 32 / 12 |
| matrix | Green matrix | 0 / 30 / 10 |
| nord | Nord | 10 / 38 / 20 |

All dimensions are pixels and only cataloged literal tokens found in the actual
player are edited. The six browser previews have distinct finite entrance motion,
respect reduced-motion preferences and do not animate continuously while idle.
**Player motion is not installed.** Previews are illustrative, not emulator runtime
screenshots. Raster artwork, icon packs, optimizer, native dialogs and boot remain
unchanged. These collections are **not six complete variants of the separate
full VOID emulator pack**. Actual Forge component/font files were not available;
the shell is reference-inspired, not a pixel-exact Forge import.

## Apply and recovery

1. Select the correct HD-Player.exe in Settings.
2. Select a collection and click **Stage this pack**. Both a supported QML palette
   and safe button-design tokens are required. Unknown/ambiguous targets refuse.
3. An existing Workspace batch blocks staging; no queue is silently cleared.
4. Run **Verify Dry Run**. Palette and button geometry are prepared together on an
   isolated image, including resource conflict checks and verified readback.
5. Close all player instances. Review and confirm Apply. The engine rechecks source
   and planned output hashes and retains the existing transactional recovery journal.
6. Open **Backups**, refresh journals and confirm Restore. Only an exact post-patch
   target can be restored. Updates or unrelated writes invalidate recovery.

The native Add-ons page routes to the existing Patch Workspace for dry run/apply;
the normal HTML frontend exposes those actions in the collection detail too.
Preview selection is not deployment: it never replaces an already staged pack.

## Validation

- `node tests/addons_test.mjs`: catalog parity, no entry-time operations, existing
  batch refusal, stale target/revision, cancellation, failed dry-run invalidation,
  exact apply/restore arguments, no implicit clear and single modal Settings.
- `node tests/chromium_ui_test.cjs`: all 15 routes with an explicit mock desktop
  transport, six previews and staged apply/recovery, existing editor regressions,
  sign-out lock, manual scans and reduced motion. Not a native patch certification.
- Existing five Node contract/search/geometry/UI suites remain applicable.
- `python tests/workspace_qml_lint_test.py`: Qt 6.8.3 metadata/type checking.
- `tests/workspace_core_test.cpp`: six real synthetic-resource pack preparations,
  palette + radius readback, same-payload conflicts and unknown-pack refusal;
  compiled by the existing Windows regression target. Must pass before release.

No actual MSI runtime or Windows EXE build is certified by browser-only tests.
