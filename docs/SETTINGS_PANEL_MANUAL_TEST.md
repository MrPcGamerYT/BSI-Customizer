# Manual Settings-panel check

Use the Windows manual launcher to inspect the real Customizer Settings-panel flow against a copy of the selected HD-Player executable. The tester never gives the GUI the original executable path: both the command-line backend test and the interactive GUI use disposable copies.

## Run it

Double-click `scripts/Run-SettingsPanel-Manual.cmd`, or run the PowerShell tester from the repository root:

```powershell
.\scripts\Test-SettingsPanel-Manual.ps1
```

The script prompts for `BSI-Customizer.exe` and `HD-Player.exe` if it cannot find them. You can also pass both paths directly:

```powershell
.\scripts\Test-SettingsPanel-Manual.ps1 `
  -CustomizerPath 'C:\path\to\BSI-Customizer.exe' `
  -PlayerPath 'C:\Program Files\BlueStacks_nxt\HD-Player.exe'
```

To run only the safe-copy backend check and skip opening the GUI, add `-NoLaunch`.

## What it checks

1. Reads the selected player's Settings-panel state and reports its detection result, registered QML resource count/sample paths, native tab count, QML dependency count, compiled units, and detailed reason when detection cannot resolve the Settings model.
2. Exercises `Backend::applySettingsPanelRebuild(true)` on a disposable copy and hashes the selected original before and after.
3. Unless `-NoLaunch` is used, opens the real Customizer directly to **Interface → Settings panel rebuild**, with that screen wired to a second disposable HD-Player copy. The manual session has isolated preferences and permits only Settings-panel inspection, local profile/art edits, and apply/restore on that disposable copy. Uploaded files are read and copied into the temporary profile; the selected originals and other emulator files are not written.
4. Leaves the Apply/Test button available even when detection says unsupported, so the backend can show its real diagnostic instead of silently disabling the test. The visible detection status and QML inventory are not hidden.

## What the rebuilt window should look like

The rebuilt shell (`BSI v17`) keeps the player's own tab model and pages in a flat, rounded window. The latest annotated screenshot asks to remove excess side space without making the dialog square. The chosen layout is **750 × 470 visible pixels**, or **780 × 500 logical pixels** including the unchanged native 15 px transparent shadow on each side, centered on the player. This is a layout target, not an exact pixel measurement of the annotations. The native page remains 568 px wide, with equal **91 px gutters** at the cap. Smaller viewports retain the existing 96% width / 94% height clamps; physical pixels scale with the player's DPI.

The user explicitly chose **horizontal top tabs**, not the reference's sidebar. The selected tab has a full-opacity outline and a soft rounded background, with its label **and actual icon** following the player's selected-tab color (`subWindowHeaderClick`). Hover uses a quieter background without changing selection. Unselected custom icons retain their original colors; selection tints their alpha silhouette without modifying the stored SVG/PNG. The rest of the native palette, fonts, controls and page column (up to 568 px), scrolling, footer and callbacks are retained. There is no additional page-content card. Earlier rebuilds remain recognizable for update/restore.

In the patched window, check that:

- the visible frame is **750 × 470** at 1× scale, is at least 1.5 times as wide as tall, and remains centered at both desktop/reference player viewports (1440 × 900 and 1301 × 665), with the original viewport clamps at smaller sizes;
- clicking a tab, including directly on its icon, changes the native selection and lights up its background, centered label and actual icon together; hover is weaker, never switches pages, and clears on exit;
- switching away from a custom-colored icon restores its unselected artwork; changing the player theme also changes the selected tab/label/icon without replacing the window palette;
- the **Settings** title is centered in the title bar with the close (**X**) button at the top right;
- the category tabs sit as a single centered, horizontally scrollable row under the title, with **12 px of clear space below the strip before the page begins**; the title-to-tab spacing and native footer position are unchanged;
- **every label is centered inside its own tab option**, including short labels such as About and long labels such as Game settings; the label and icon share one horizontal center;
- every neighboring option has the same **4 px hit-target gap**, with **11 px of label padding on each side** (26 px between neighboring label bounds); notification dots stay in that padding instead of shifting labels;
- hiding the first/last tab or several consecutive tabs leaves exactly the same gaps between the remaining tabs; replacing/defaulting icons and changing language must not shift labels off center;
- each supplied pack icon appears beneath its tab label automatically, without uploading artwork first;
- the **About** page logo stays centered in a **160 × 60 px maximum area**, preserving its aspect ratio (a square icon paints at 60 × 60), rather than expanding to its source resolution; test square, wide and tall custom artwork and verify the developer name remains visible;
- the native page has equal empty gutters on the left and right, and its controls and per-page footer (for example "Some changes will apply on next launch" with its save button) are fully visible;
- clicking a category still switches the page, dragging the title bar still moves the window, and the close button still closes it.

On a narrow player window the centered page column shrinks with the window (48 px total gutter inside the shadow frame); overflowing tabs remain reachable by scrolling. Long native forms scroll independently while **Save changes** stays visible in the footer. Labels use the player's themed font for both painting and measurement, including glyph side bearings, not an unrelated application font.

The automated `settings_panel_post_patch_gui_test.py` runs the **same assertions in Qt 6 and Qt 5**, measuring the actual generated Text and hit targets for all twelve tabs: multiple fonts (including italic), translations, badges, default/custom/missing icons, consecutive hidden tabs, reference/desktop/narrow players and both sides of the size-cap breakpoints. It executes the production sizing helper on a real transient window and checks the exact 750 × 470 visible frame, native 15 px inset, unchanged 568 px page, symmetric 91 px gutters at the cap, 12 px page gap, footer and scrolling. It scrolls to and physically clicks every tab (alternating label/icon-area clicks), then tests hover, live theme changes and the **painted icon/background pixels**. Assertions check that the selected PNG is truly recolored and its original custom colors return when deselected. This is an offscreen Qt Quick test, not an HD-Player/VM session.

`settings_panel_window_budget_test.py` reconstructs the **entire** legacy Settings resource, including its original callback prefix and the sizing helper. Counting shell-template bytes is not a reliable compression budget. Apply removes redundant horizontal whitespace beside safe punctuation/operators as well as leading indentation, retaining literals, notices, every line ending and JS statement boundary; ambiguous regex/template fragments are left unchanged. Token/Qt regressions protect operator separation, callbacks and quoted line continuations. The complete fixture uses **2436 / 2468 bytes**, retaining 32 bytes of headroom (the test requires at least 32). Hidden tabs still use the numeric icon-map empty sentinel; the unused separate `bHidden` channel was retired to make room for the new styling. The Windows engine suite additionally patches disposable copies of the real player references.

## Experimental native Shaders — ready for user verification, not certified

The native Customizer now has **PATCHER → Player interface → Shaders** with
read-only compatibility/hash inspection and explicit install/update/reapply,
shader-only removal and recovery dialogs. The Single EXE test workflow embeds
regular x64 release MSVC modules for **Qt 5.15.2 and 6.8.3**; another exact player
Qt version safely refuses. Apply the native Settings top-tabs redesign first,
then follow the [real-player test and recovery guide](SHADERS-USER-TEST.md).
Nothing installs on entry, and a regular Qt module is not a `version.dll` proxy.

The matching original SVG is a customizable default. The **13-tab Qt contract
fixture** loads the actual plugin, mouse-drags the horizontal native strip to
Shaders, tests native tab/close No/Discard prompts, controls, persistence and
missing-module fallback. The full Customizer Main.qml also has physical-pointer
installer tests with explicit mock services. Neither is a running emulator.

Compact v2 and bounded verified lossless compression retain the read-only,
in-memory root/window capacity, repeat and exact decoded-resource removal cycles
on four untouched player references. Some legacy slots have only 8 bytes left;
other/edited builds still safely refuse when they do not fit. The strict package
validator and recoverable EXE+four-file module use complete dependency/ownership
checks and reviewed EXE/module/runtime fingerprints. Source/transaction tests
preserve unrelated resources and never replace Qt/VC/system DLLs.

Actual-player Qt/import-path/deployment behavior, visible game-frame effects,
Windows presentation safety and the physical Intel/AMD/NVIDIA renderer matrix
remain unverified. Do not count fixtures, resource capacity, recovery tests or
software shader pixels as end-to-end validation. **No merge yet.**
See the detailed [capacity results, achieved tests and merge gates](SHADERS-INTEGRATION.md).

## Interactive Settings-window preview

This is the generated **in-player Settings shell**, not the Customizer's Settings-panel editor:

```sh
python -m pip install PySide6==6.8.3
python .harness/settings_window_preview.py --port 8080
```

Open the server's port in your browser. It binds to `0.0.0.0` and uses relative browser URLs, so proxied previews work. Linux also needs the usual Qt libraries (`libgl1`, `libegl1`, `libxkbcommon0`, `libdbus-1-3`).

- The window template, viewport-sizing helper, selected styling and About page are extracted directly from `SettingsPanelPatcher.cpp`; there is no separate HTML imitation of the Settings UI.
- The supplied pack is converted to actual PNGs. Browser pointer, wheel and key events reach real Qt controls; frames are rendered by Qt on its GUI thread.
- Click/hover the top tabs, scroll over the strip to reach the last tabs, edit the sample form and try Save changes. The category picker also scrolls to a category and clicks its actual icon area. The toolbar can simulate a smaller player or a different selected-tab accent, and reopen the window after closing it.
- **Page values, save handling, the theme service and shadow/close helpers are explicit fixtures.** No emulator, executable, license service or system setting is accessed. This previews the shell design; it does not certify a complete emulator installation.
- `tests/settings_window_preview_test.py` renders all twelve pages and exercises real tab/icon, slider, Save and Close input, reopen, resize and theme changes, with no shell runtime warnings.

For the optional second-runtime regression, install `PyQt5==5.15.11` and run:

```sh
BSI_TEST_QT_API=PyQt5 python tests/settings_panel_post_patch_gui_test.py
```

Use separate Python processes for Qt 5 and Qt 6.

## Default pack and custom-icon regression

Use a disposable player copy and an empty Settings-panel profile for the first check:

1. Inspect the player. Each known native tab should preview a **Pack default** icon. Inspection alone must not modify the player or save these defaults as custom overrides.
2. Apply without uploading any icons, then restart the disposable player. Verify all visible tabs have the supplied artwork. A twelve-tab player uses eleven distinct SVGs: Game settings and Gamepad share the controller. Devices uses the monitor/phone drawing; Phone uses the standalone phone drawing. Unknown future tabs are not assigned a guessed icon.
3. Replace one tab with a valid SVG (also try an editor-exported `<path ... />` SVG). The preview should update immediately, other pack icons should remain, and the player should change only after **Apply** and restart. Malformed SVGs should be rejected when selected.
4. Replace the same tab again with a visibly different drawing/color. Apply and restart; the new icon must appear rather than a cached older image.
5. Set a second custom tab icon. Choose **Use default** on the first one and Apply. Only the first should return to pack artwork; the second should remain customized.
6. Hide a tab, Apply and restart. Its label, icon and gap should disappear together; the remaining pack/custom icons must stay on their correct pages.
7. Use **Reset tab visibility & artwork**, then Apply. All tabs should be visible with pack icons again, the custom About logo should be cleared, and the About name/links should remain.
8. Restore the original Settings panel. Confirm only the Settings-window/About sources are restored and unrelated customizations remain.

Repeat with both Qt 5 and Qt 6 players when available. The patcher writes immutable PNG runtime copies under the Customizer's local application-data `settings-panel-assets` directory. Keep that directory while the patch is in use; the original SVGs and custom SVG overrides remain editable. Explicit SVG colors must remain unchanged in stored artwork and on unselected tabs; the selected icon intentionally follows the player's selected-tab color. Uncolored `currentColor` artwork should be readable light gray, not black or blank.

Keep the Customizer open while inspecting the GUI. Closing it ends the manual session and removes the disposable GUI copy and temporary settings. A GUI Apply/Restore operates only on that copy.

The same safe-copy diagnostic is also available from a terminal:

```powershell
& 'C:\path\to\BSI-Customizer.exe' --settings-panel-test 'C:\path\to\HD-Player.exe'
```

Exit code `0` means the target is reported supported, the safe-copy backend apply passed, and the original file remained unchanged. Exit code `1` indicates an unsupported target or failed apply check; `2` indicates a usage or I/O error.
