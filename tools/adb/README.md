# Optional Windows ADB sidecars (not included)

For a **build with bundled ADB**, put a matching, trusted Windows platform-tools
set in this folder: `adb.exe`, `AdbWinApi.dll`, `AdbWinUsbApi.dll`, `NOTICE.txt`.
`Build-Installer.ps1` will detect it, or use `-AdbDirectory` for another folder.
These external files are ignored by Git. Do not copy Android authentication keys
(`adbkey`, `adbkey.pub`) here. Check the supplier's redistribution terms yourself;
this project does not grant rights to external SDK binaries.

To **run the app**, these three binaries may instead sit beside the delivered
BSI-Customizer EXE, or inside its `adb/` subfolder. You do not need Qt or Inno Setup
just to add ADB to an existing copy. The portable launcher retains its outer
location even though Qt runs from a private temporary extraction directory.

See [installer instructions](../../docs/INSTALLER.md).
