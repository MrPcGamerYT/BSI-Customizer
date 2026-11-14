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

The rebuilt shell (`BSI Settings v9`) keeps the player's own tab model and pages but never stretches a page. The stock Settings pages are fixed-size surfaces — a ~520 px content column pinned to the top-left plus a footer anchored to the bottom edge — so a full-bleed shell is what made a correctly built panel look left-aligned and cramped.

In the patched window, check that:

- the **Settings** title is centered in the title bar with the close (**X**) button at the top right;
- the category tabs sit as a single row, centered, on their own row under the title — no tab is half-clipped at the window edge, and the row does not overlap the content card;
- the page sits in one centered card about 570 px wide, with equal empty gutters on the left and right;
- each page's own rows, presets, sliders and toggles are aligned inside that card, and any per-page footer (for example "Some changes will apply on next launch" with its save button) is fully visible at the bottom of the card, not cut off;
- clicking a category still switches the page, dragging the title bar still moves the window, and the close button still closes it.

On a narrow player window the card shrinks with the window (down to 32 px total gutter) instead of dropping the tabs or hiding the footer.

Keep the Customizer open while inspecting the GUI. Closing it ends the manual session and removes the disposable GUI copy and temporary settings. A GUI Apply/Restore operates only on that copy.

The same safe-copy diagnostic is also available from a terminal:

```powershell
& 'C:\path\to\BSI-Customizer.exe' --settings-panel-test 'C:\path\to\HD-Player.exe'
```

Exit code `0` means the target is reported supported, the safe-copy backend apply passed, and the original file remained unchanged. Exit code `1` indicates an unsupported target or failed apply check; `2` indicates a usage or I/O error.
