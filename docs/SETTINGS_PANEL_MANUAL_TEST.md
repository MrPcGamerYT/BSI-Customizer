# Manual Settings-panel check

Use the Windows manual launcher when you want to inspect the **same HD-Player.exe that you actually launch** in the Customizer and verify the Settings-panel rebuilder without risking that executable.

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

To run only the check and skip opening the UI, add `-NoLaunch`.

## What it checks

1. Reads the selected player's real Settings-panel state through `Backend::settingsPanelRebuildState()` and reports support, native tab count, QML dependencies, compiled units, and the analysis detail.
2. Copies the executable to an automatically deleted temporary directory and calls `Backend::applySettingsPanelRebuild(true)` on that copy.
3. Confirms the temporary copy reports the rebuild applied and hashes the selected original before and after to ensure it did not change.
4. Unless `-NoLaunch` is used, starts the real Customizer with that HD-Player selected. In the UI, open **Interface → Settings panel rebuild** and compare its Support status and detail with the JSON report.

The script never applies the Settings rebuild to the selected original. To test applying it to an installed player, use the Customizer's Apply button yourself after reviewing the report and taking your normal backup.

The same safe-copy diagnostic is also available from a terminal:

```powershell
& 'C:\path\to\BSI-Customizer.exe' --settings-panel-test 'C:\path\to\HD-Player.exe'
```

Exit code `0` means the target is reported supported, the safe-copy backend apply passed, and the original file remained unchanged. Exit code `1` indicates an unsupported target or failed apply check; `2` indicates a usage or I/O error.
