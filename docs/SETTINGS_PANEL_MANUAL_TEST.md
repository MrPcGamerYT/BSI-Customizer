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

The rebuilt shell (`BSI v21`) keeps the player's own tab model and pages in a rounded **flat, opaque native-themed** window. Glass is restricted to the selected navigation tab, not the frame, pages, footer, controls or hovered tabs. The latest annotated screenshot asks to remove excess side space without making the dialog square. The chosen layout is **750 × 470 visible pixels**, or **780 × 500 logical pixels** including the unchanged native 15 px transparent shadow on each side, centered on the player. This is a layout target, not an exact pixel measurement of the annotations. The native page remains 568 px wide, with equal **91 px gutters** at the cap. Smaller viewports retain the existing 96% width / 94% height clamps; physical pixels scale with the player's DPI.

The user explicitly chose **horizontal top tabs**, not the reference's sidebar. The selected tab has a full-opacity outline and a soft rounded background, with its label **and actual icon** following the player's selected-tab color (`subWindowHeaderClick`). Hover uses a quieter background without changing selection. Unselected custom icons retain their original colors; selection tints their alpha silhouette without modifying the stored SVG/PNG. The rest of the native palette, fonts, controls and page column (up to 568 px), scrolling, footer and callbacks are retained. There is no additional page-content card. Earlier rebuilds remain recognizable for update/restore.

The selected-tab effect is part of the **actual generated player resources**,
not just the Customizer illustration. The user confirmed the newer screenshot
was no longer transparent, but its brighter outer gradient still framed a dark
inset native page. **v21 removes that window-wide gradient/rim entirely.** The
frame, title/tab surroundings, gutters and page/footer backing now paint the
same live `UiTheme.colors.primary80`, so no inset color mismatch is introduced.

Native pages, control backgrounds, slider parts, indicators and Overlay popups
retain their own original materials. The Shaders module imports the actual
stock controls and full live theme, not painted lookalikes. No pass enters a
page Loader or follows native `background`, `handle` or `popup` pointers. The
independent page/footer backing stays flat and opaque, not a rounded content
card. The title, 6 px tab corners, 750×470 geometry and native callbacks remain.

About hosts the small **selected-tab-only** gradient factory for resource-budget
reasons. A discovered generated tab fill owns one reusable material, attached
only while selected; an unselected/hovered tab has **no Gradient**. The bounded
visible-window discovery pass cannot decorate the frame. Page Loaders and
layered items stay hard boundaries. About artwork remains within 160×60 and its
flat link controls retain their input behavior. The source/Qt tests below do
not certify the installed player's physical-GPU scene graph or game hooks.

In the patched window, check that:

- the full frame/page/footer share one **flat, opaque native-theme background**;
  **only the selected tab** reflects light, while hover stays flat and every
  native page/control remains undistorted; visit all middle tabs, open
  combo popups, drag sliders repeatedly, scroll, resize, edit fields and Save;
- with a moving/high-contrast game behind Settings, no page or footer reveals it;
  no stretched triangles appear while sliders move or their content is clipped;
- disabled controls remain disabled and visually quiet; native focus and warning/error borders still appear; changing dark/light palettes or the theme accent updates the glass instead of freezing colors;
- no decorative surface intercepts clicks, typing, keyboard focus, wheel input, tab selection or native Save/Discard/Close prompts;

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

`settings_panel_window_budget_test.py` reconstructs the **entire** legacy Settings resource, including its original callback prefix and the sizing helper. Counting shell-template bytes is not a reliable compression budget. Apply removes redundant horizontal whitespace beside safe punctuation/operators as well as leading indentation, retaining literals, notices, every line ending and JS statement boundary; ambiguous regex/template fragments are left unchanged. Token/Qt regressions protect operator separation, callbacks and quoted line continuations. The complete v21 fixture uses **2434 / 2468 bytes**, retaining 34 bytes of headroom (the test still requires at least 32). Hidden tabs still use the numeric icon-map empty sentinel; the unused separate `bHidden` channel was retired to make room for the new styling. The Windows engine suite additionally patches disposable copies of the real player references.

`settings_panel_glass_test.py` additionally loads the **unmodified generated shell and About material together**, on independent Qt 5 and Qt 6 hosts. It requires exact native-theme pixels across frame/page/footer over a contrasting sibling (software captures are RGB32, so PNG alpha alone is not evidence), a strict rendered reflection only on the selected tab, no Gradient on hover, and full-color outlines. Five DPI runs plus four strict negatives reject both flattened selected-tab reflections and a lighter frame/darker-page mismatch. Authored page contracts exercise Loader-backed buttons, asset-based indicators, sliders, text editing/validation, disabled transparent controls, existing gradients, combo popups reparented to the Overlay, native activation, light/dark/accent rebinding, all twelve pages, no native material mutation (including lazy background replacements and
Overlay popups) and no hidden-window timer work. The page/footer backing is
sampled over two contrasting simulated-game colors, not only the shell gutter.
The material must not touch unrelated siblings. These are real Qt pixels/inputs but **not OEM-player/game verification**.

`settings_panel_icon_pack_test.py` counts the entire glass-bearing About document, including four links, a custom logo, long encoded local paths and **12 or 13 independent image hashes**. Both must still fit the existing **2337-byte** slot. No native Settings page or shared control resource is repurposed, and the production capacity/read-back/recovery gates still safely refuse an oversized or ambiguous player rather than growing its EXE.

## Experimental native Shaders — test workflow, not certified

The native Customizer now has **PATCHER → Player interface → Shaders** with
read-only compatibility/hash inspection and explicit install/update/reapply,
shader-only removal and recovery dialogs. The Single EXE test workflow embeds
regular x64 release MSVC modules. New V3 builds use genuine **Qt 5.15.2 / 6.2.4
public-API baselines**, with newer/equal same-major runtimes eligible only after
full provider/symbol checks; older V2 downloads remain exact-version only.
See [Qt research, current test status and coverage limits](EMULATOR-QT-COMPATIBILITY.md).
Apply the native Settings top-tabs redesign first,
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

## Clean title bar and combined title-details removal

These are two separate controls in **PATCHER → Player interface**:

1. Close every player instance. Apply **Clean player UI**, or **Update clean-up**
   if an older cleanup is already applied. The three-line title-bar menu must
   disappear, including its actual clickable item, not only its SVG. The
   sidebar **Settings** button must remain accessible. Resize, maximize/restore,
   leave fullscreen and exercise any existing pin/mini-mode exit; the menu
   must not reappear. Native navigation/window controls must continue working.
2. In **Ads / UI Removal**, select the single **Remove title details** choice
   and apply it. This one action removes both the version and instance/engine
   label, collapses their row, and vertically centers the remaining player name
   with equal space above and below. It does **not** horizontally center the
   name. Resize/restart and confirm the details do not reappear.
3. Open Settings: its headings, version/instance text, pages, buttons and native
   callbacks must still work. Repeat the two actions in the opposite order on
   another disposable copy. Full compressed records must use bounded lossless
   encoding or safely refuse—never grow the EXE for title-details removal.
4. **Restore stock UI** restores only the clean-up's exact owned fields and
   runtime menu guard; it must not undo the separate title-details removal.
   **Restore EXE backup** is the full undo for removals. Preserve that first
   verified backup. Edited/duplicated owned guards or ambiguous menu owners
   must refuse without writing rather than guessing.

The application self-test includes both orders on synthetic resources and the
selected untouched player reference. For physical Qt contract-scene checks,
set `BSI_TOPBAR_TEST_FIXTURES` to an empty directory when running `--selftest`,
then run `python tests/run_qt_runtime_test.py tests/clean_player_ui_capture_test.py`
with the same variable. Repeat in a separate process with
`BSI_TEST_QT_API=PyQt5`. The 20 production-written scenes cover both native
visibility models, owning-item shapes, callbacks, imperative re-show attempts,
resize/window restore, physical Settings clicks, painted text, and equal
top/bottom name spacing.

The independent host explicitly loads the repository's OFL-licensed Inter
font before creating QML. Qt 5's Windows offscreen backend uses a FreeType font
database rather than ordinary Windows installed-font discovery; without a
supplied font, its box engine can measure a heading while painting no glyphs.
The capture runner repeats all 20 unchanged scenes with host/default and
isolated font search paths. A separately staged transparent-heading negative
must fail the original rendered-text assertion (at least 5 brighter pixels,
each with RGB-sum contrast greater than 96). It cannot pass on an arbitrary
exception. Original exported QML stays byte-identical; case logs and failure
paint/control PNGs are retained under `capture-validation/`. This fixes the
authored test host, not production player fonts, and does not weaken the paint
check. These scenes do not certify a real installed player; carry out the
manual checks above too.

## Editable About links and saved artwork

Both the Chromium and native **Settings panel rebuild** editors now support
**0–24** rebuilt-player About links, with **+ / −** controls, arbitrary HTTP(S)
website labels/URLs and an optional per-row SVG/raster icon. The developer name,
logo and subtitle remain editable. Saving options stores preferences on this PC;
**Apply Settings panel rebuild** is still the separate operation that writes the
player. Native tabs, theme and Save/Discard callbacks are not redesigned.

1. Add at least six links, type distinct labels/URLs and choose different icons
   on two nonadjacent rows. Save, close/reopen the editor and verify every value
   and icon. Name and an intentionally empty subtitle must also survive reopen.
2. Remove the first link, save and reopen. Only that stable id's icon should be
   pruned; later rows must keep their own labels/URLs/artwork, not shift icons.
   Remove all links and save: the empty list must persist, not reset to four.
   Rebuilt-player links do not overwrite the Customizer's own fixed-four links.
3. Invalid/duplicate ids, more than 24 rows, incomplete labels/URLs, credentials,
   file/javascript schemes, control characters and unexpected fields must be
   refused by the shared native validator before changing the saved profile.
4. Apply to a disposable copy and restart the test player. Verify logo aspect
   bounds, name/subtitle, correct row count, icon paint, scrolling, keyboard/
   pointer activation and that links open the intended HTTP(S) destinations.
   Do not launch untrusted destinations merely to test rejection.
5. Apply a changed list/icon, then repeat unchanged; verify idempotence/no EXE
   growth and exact feature-local Restore without undoing unrelated edits.

Stable ids attach artwork to rows rather than list positions. Editable original
assets remain in the profile. Runtime images are bounded PNGs under
`AppLocalData/settings-panel-assets/<canonical-exe-path SHA>/`; large/custom-icon
link lists use an escaped **QtQml 2.15 literal-model asset** (at most 64 KiB).
URLs are encoded absolute local file references, not user-authored QML. Files are
content-addressed and immutable: a changed row/model gets a new URL and corrupt
existing bytes cause refusal, not silent overwrite. Keep these files while a
patched player/recovery snapshot references them; moving/deleting the app-data
folder can break those references. Failed staging cleans only that attempt's
new files; successful prior versions are not deleted under a live player.

Automated coverage executes the actual applied About QML and staged model in an
independent Qt 6 host for both resource-format fixtures: **24 → 23 → 0 rows**,
exact escaped labels/URLs and **2 → 1 → 0** readable local PNG icon sources,
without QML warnings. It caught and fixed an illegal semicolon after a grouped
`icon` property that static string checks had missed. The application self-test
also exercises real Backend/QSettings persistence and validator rejection;
editor-only tests use explicit service doubles. These do not certify the player,
Windows/Qt 5 runtime, painted custom-icon output, external link launch or GPU.
Current-head CI results must be checked independently.

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
