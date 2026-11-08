# Player Appearance Studio

Player Appearance Studio is the first numeric QML design-token package from the
HD-Player research roadmap. It adds safe, inspected controls for the player's own
appearance tokens without turning BSI into an arbitrary QML/hex/code editor.

## Where it lives

- Normal Chromium workspace: **Player interface → Appearance studio**.
- Emergency native fallback: **UI → Appearance studio**.
- Both shells call the same authenticated native Workspace actions:
  `appearance-inspect`, `queue-appearance`, `dry-run`, `apply`, and
  `profile-section` with section `appearance`.

The HTML page count remains 13; this is a tab inside the existing Player
interface page, not a new top-level destination.

## What it edits

The first catalog is intentionally small and literal-only:

- `UiThemeObject.qml`
  - `fonts.<role>.pixelSize` inside `Qt.font({ ... })`
  - `buttons.sizes.<size>.height`, `radius`, `horizontalPadding`, `spacing`,
    `iconWidth`, `iconHeight`
  - recognized slider, toggle, checkbox, radio, combobox and text-field size
    tokens such as handle/track dimensions, indicator dimensions, spacing and
    maximum dropdown height
- `plr.qml`
  - root `ApplicationWindow` properties `pSidebarWidth` and `pBorderWidth`

Every control is an integer with a per-token range. The scanner refuses
expressions, comments, string decoys, unknown resources, duplicate/ambiguous
logical paths and non-UTF-8 sources. Keys are logical
`<resourcePath>#root.<objectPath>.<property>` identifiers, never raw file offsets.

## Transaction and safety model

1. **Inspect** reads the selected EXE and reports supported tokens, resource
   paths, ambiguous paths and matching compiled-QML counts. It writes nothing.
2. **Stage** validates the edited values against the current image and queues a
   native `appearance` Workspace operation.
3. **Dry Run** prepares an isolated image, checks storage capacity/relocation,
   invalidates matching compiled QML units, rewrites only the selected literals,
   and reads the resource back through the Qt resource tree.
4. **Apply** rechecks the target fingerprint and reviewed output SHA-256, saves a
   recovery snapshot, commits the single EXE replacement and keeps the existing
   Workspace journal/restore flow.

The EXE is not grown. A payload that no longer fits its slot must fit verified
free space already mapped by the PE; otherwise the operation is refused.
Appearance edits conflict with palette or raw-resource operations touching the
same payload, and duplicate appearance operations are refused.

### Compiled-QML shadow precision (fail closed)

A source edit is useless if the player still runs a compiled copy of the page,
so the writer invalidates the matching QV4 unit. That is only safe when the unit
can be attributed **precisely**: disabling a compiled unit whose `.qml` source
the player does not carry would stop that component from loading.

Measured on the reference players, every Qt 5 QV4 unit has an empty
`sourceFileIndex`, and neither `UiThemeObject` nor `plr.qml` appears in any string
table — filename-based invalidation matches nothing there. This feature therefore
identifies units by one unique declared property each (`pFontFamily` for the
theme, `pSidebarVisibleBeforeFullscreen` for the player window). Broadly shared
names such as `iAppwin`, `iTopbar`, `titleLargest` or `buttonMedium` match 2–17
units and are deliberately not used.

Before writing, the engine compares the resource writer's broader identifier
match against that precise match. If the writer would disable **more** units than
this feature can positively attribute, the edit is refused with the reason. On
the four reference samples this means:

| Player | Theme tokens (fonts/buttons/controls) | Chrome tokens (`plr.qml`) |
|---|---|---|
| BlueStacks 5 latest (Qt 6) | offered | offered |
| MSI latest (Qt 6) | offered | offered |
| BlueStacks 5.10 (Qt 5) | offered (1 unit invalidated) | refused — 8 broad vs 1 precise |
| MSI 5.9 (Qt 5) | offered (1 unit invalidated) | refused — 8 broad vs 1 precise |

For legacy chrome changes, keep using the existing layout/sidebar tools, which
have their own recovery path.

## What it does not do yet

- Install font packs or icon-state packs.
- Edit arbitrary QML, JavaScript, gradients, colors or compiled bytecode.
- Edit player config keys, per-instance CPU/RAM/FPS settings, DLLs or RCC targets.
- Prove runtime appearance on every BlueStacks/MSI build. Static resource
  verification is not a substitute for launching the target player.

Those remain separate roadmap packages. The existing Theme, Resources,
Settings-panel, About, boot, FPS and debloat tools are unchanged.

## Verification

- `tests/player_appearance_scan_test.cpp` is a Qt-free C++17 sanitizer regression
  for the catalog, literal-only rewriting, CRLF preservation, ranges and
  ambiguity refusal.
- `tests/player_appearance_ui_test.mjs` checks the Chromium
  inspect/stage/dry-run/apply/profile flow against a mock native transport.
- `tests/workspace_core_test.cpp` includes appearance profile validation,
  isolated dry-run, read-back, same-payload conflict checks, and a synthetic Qt 5
  QV4 unit proving the compiled-shadow precision guard both refuses an
  unattributable unit and invalidates a precisely identified one.
- CI runs the Qt-free scanner test, the Node UI tests, QML lint/runtime tests and
  the Windows build/self-test matrix. A full Qt/CMake build and real player
  launch must still be verified on a Windows target before claiming visual
  compatibility for a specific emulator build.
