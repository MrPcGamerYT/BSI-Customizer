# BSI Studio workspace (3.0.1, feature branch)

This is a presentation/responsiveness upgrade, not a replacement patch engine.
The existing Discord gate, 13 original sections, Binary Lab, update verifier,
backup rules, manual resource scans and native media decoder remain in place.

## Window and responsiveness

- The production Chromium shell uses a normal OS-framed, resizable window.
  Windows borders, snapping, system menu, taskbar and minimize/maximize work.
  Initial dimensions respect the available desktop; this is not kiosk/fullscreen.
- Unsaved resource text is confirmed before closing via the native titlebar.
  Closing is refused while a native worker is active; minimizing is still allowed.
  Sections can still be browsed during work; new engine commands stay guarded.
- CSS reflows at real window size. There is no fixed 1480×920 canvas transform
  shrinking text, controls or pointer targets at laptop resolutions.
- Resource inspection, source compression measurement and selected export use
  the native worker facade. One inspection reads one hash-validated EXE snapshot
  for bytes, budget, source and raster instead of repeated GUI-thread reads.
- SVGs sent to Chromium are now genuinely **PNG rasters**. The old image helper
  returned SVG URLs, which the browser correctly rejected. QML recovery keeps
  its original vector preview API; cached thumbnails are safe PNGs for both UIs.
- Navigation/hero markup is reused when unchanged, native log bursts render at
  most once per animation frame, and image/source payloads are not serialized
  into a second hidden JSON results box. There are no huge live blurred layers.
- **Low effects** disables presentation transitions/animations, including window
  effects. Only this preference is stored locally; no auth/session/target data.
- **Ctrl+K** finds sections/tools without executing them. Normal entry, dialog,
  picker and window animations remain in full-effects mode; reduced-motion is
  respected. Pane scroll positions survive command results.
- The native sign-in card scrolls on short displays, including manual callbacks
  and saved-session/error states. AuthManager logic is unchanged.

## Resource workbench

1. Select a real target in Settings, then **Scan Resources** explicitly.
2. Browse metadata-only folder scopes and quoted/type/size search, naturally sort
   names or sort by size/type, and switch between a list and cached-thumbnail gallery.
3. Inspect saved raster/source, storage mapping, dimensions, compression and the
   first 256 decoded bytes. Unsupported data is never executed as code or loaded
   as a font. Oversized entries get metadata-only inspection rather than an empty
   editable draft; use Extract. Existing native decoding limits still apply.
4. Bookmark paths for this target/session, reopen up to eight recent assets,
   copy a path, search exact source text, indent with Tab or reset the current
   draft after confirmation. **Ctrl+S** uses the same explicit native save confirmation.
5. Keep edits as drafts until Save. The picture shows the **saved** native raster,
   not a live rendering of arbitrary source. Native compression/mapping validates
   the actual write; source search does not decompress or scan the EXE.

Only 100 result entries and 200 immediate folder entries render at a time. Deep
folder trees can be browsed level by level, or searched from All resources.
Filtering, sorting, bookmarks, gallery and navigation never start a native scan.
Native writes may rebuild their existing post-write snapshot, as before.

### Selected export

Check entries and choose **Export selection…**, then a local destination folder.
After confirmation the engine creates a new `BSI-resources-<UUID>` child folder.
Files are flat, named `<native-index>-<sanitized-name>` to prevent collisions,
Windows reserved-name issues, traversal and overwriting existing files. The EXE
is unchanged. The source hash must still match the explicit scan.

Limits: **500 unique entries, 16 MiB per entry, 128 MiB total decoded data**.
Actual output bytes are checked too. Partial failures identify the output folder;
files already exported are not silently deleted. Original Extract and the legacy
bulk tools remain available. Bookmarks/selection are not shared between targets
or persisted as executable offsets.

## Validation

- `node tests/resource_workbench_test.cjs`: metadata model and 20,000-row workload.
- `node tests/chromium_contract_test.mjs`: engine/asset/auth/manual-scan contracts.
- `tests/chromium_ui_test.cjs`: all routes plus workbench interactions and safe
  text/image handling using an isolated browser transport double (not a real patch).
- Native self-tests cover real SVG-to-PNG output, worker inspection/measurement,
  selected export, unchanged source bytes and stale-scan refusal. Deployment tests
  assert the normal OS frame for the actual Chromium window.
- Windows workflows compile/deploy the real branch and test portable/setup
  lifecycle. Real display scaling, dragging/snapping, manual OAuth and large real
  emulator installations still require an interactive Windows acceptance pass.

See [INSTALLER.md](INSTALLER.md) for installer/optional external ADB packaging.
