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

Keep the Customizer open while inspecting the GUI. Closing it ends the manual session and removes the disposable GUI copy and temporary settings. A GUI Apply/Restore operates only on that copy.

The same safe-copy diagnostic is also available from a terminal:

```powershell
& 'C:\path\to\BSI-Customizer.exe' --settings-panel-test 'C:\path\to\HD-Player.exe'
```

Exit code `0` means the target is reported supported, the safe-copy backend apply passed, and the original file remained unchanged. Exit code `1` indicates an unsupported target or failed apply check; `2` indicates a usage or I/O error.
