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

The rebuilt shell (`BSI v15`) keeps the player's own tab model and pages in a flat, rounded window. The dialog is capped at **800 × 460 logical pixels** (about 770 × 430 inside the native 15 px shadow), centered on the player, rather than the previous 1120 × 670 footprint. Smaller viewports retain the existing 96% width / 94% height clamps. Native pages keep their centered column up to 568 px wide, scrolling and fixed footer. Theme, colors, fonts, icons and tab spacing are unchanged; there is no extra content card or selected-tab pill.

In the patched window, check that:

- the window is compact and centered at both screenshot-sized player viewports (1440 × 900 and 1149 × 659), without the old broad empty margins;
- the **Settings** title is centered in the title bar with the close (**X**) button at the top right;
- the category tabs sit as a single centered, horizontally scrollable row under the title, without overlapping the page;
- **every label is centered inside its own tab option**, including short labels such as About and long labels such as Game settings; the label and icon share one horizontal center;
- every neighboring option has the same **4 px hit-target gap**, with **11 px of label padding on each side** (26 px between neighboring label bounds); notification dots stay in that padding instead of shifting labels;
- hiding the first/last tab or several consecutive tabs leaves exactly the same gaps between the remaining tabs; replacing/defaulting icons and changing language must not shift labels off center;
- each supplied pack icon appears beneath its tab label automatically, without uploading artwork first;
- the native page has equal empty gutters on the left and right, and its controls and per-page footer (for example "Some changes will apply on next launch" with its save button) are fully visible;
- clicking a category still switches the page, dragging the title bar still moves the window, and the close button still closes it.

On a narrow player window the centered page column shrinks with the window (48 px total gutter inside the shadow frame); overflowing tabs remain reachable by scrolling. Long native forms scroll independently while **Save changes** stays visible in the footer. Labels use the player's themed font for both painting and measurement, including glyph side bearings, not an unrelated application font.

The automated `settings_panel_post_patch_gui_test.py` measures the actual generated Text and hit-target geometry for all twelve tabs, with multiple fonts (including italic), translated labels, badges, default/custom/missing icons, consecutive hidden tabs, both screenshot viewport sizes plus narrow players, and the player's redirected shadow-container children/insets. It executes the production sizing helper on a real transient window, checks footer visibility and scroll reachability, and scrolls to and clicks the last tab. This is an offscreen Qt Quick test, not a substitute for the live-player check above.

`settings_panel_window_budget_test.py` rebuilds the **entire** legacy Settings window against its exact read-only compressed resource fixture, including the native callback prefix and geometry helper. Counting template bytes alone is not a reliable compression budget. Apply removes only insignificant leading indentation, retaining quoted literal whitespace, line endings, native logic and copyright notices; the complete fixture uses 2396 of its 2468-byte slot. The Windows engine suite additionally patches disposable copies of the real player references.

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

Repeat with both Qt 5 and Qt 6 players when available. The patcher writes immutable PNG runtime copies under the Customizer's local application-data `settings-panel-assets` directory. Keep that directory while the patch is in use; the original SVGs and custom SVG overrides remain editable. Explicit SVG colors must remain unchanged; uncolored `currentColor` artwork should be readable light gray, not black or blank.

Keep the Customizer open while inspecting the GUI. Closing it ends the manual session and removes the disposable GUI copy and temporary settings. A GUI Apply/Restore operates only on that copy.

The same safe-copy diagnostic is also available from a terminal:

```powershell
& 'C:\path\to\BSI-Customizer.exe' --settings-panel-test 'C:\path\to\HD-Player.exe'
```

Exit code `0` means the target is reported supported, the safe-copy backend apply passed, and the original file remained unchanged. Exit code `1` indicates an unsupported target or failed apply check; `2` indicates a usage or I/O error.
