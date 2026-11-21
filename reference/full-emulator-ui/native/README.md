# User-supplied emulator source snapshot

Read-only reference input for the requested VOID controls-editor, overlay, menu and exit-dialog restyle. This is not an installed/patched skin.

- Source: https://github.com/husainprime20/src-code
- Pinned commit: `c13a7eb5f5350a3d12cca364d53a6c72f00fd555`
- Downloaded: all 56 source files, 966,038 bytes.
- `SOURCE-MANIFEST.json` records individual Git blob IDs, sizes and SHA256 hashes. Each downloaded file was checked against the GitHub tree's blob ID.
- Original bytes, CRLF, BOMs and copyright notices are preserved. No source formatting, skin edits or native behavior changes have been applied here.
- No files were changed in the user's src-code repository. This snapshot stays in the existing BSI-Customizer working branch as reference input, not an extra public repository.

## Exact target locations found in the source

- Controls editor panel, scheme selection, control palette, Save/Reset: `KmmEditorWindow.qml`.
- Per-control editable key markers/geometry: `KmmEditor*.qml`, including Tap, Dpad, Pan and the shared BaseImageRect/TextEditBox.
- In-game key overlay styling: `KmmOverlay*.qml`, especially `KmmOverlayItem.qml` plus the per-control overlays.
- Dropdown: `PlrTopbar.qml`, component `iHamburgerMenuComponent` / popup `iHamburgerMenu`.
- Close prompt: `PlrTopbar.qml`, component `iClosePlrPrompt`. Its current title and description use the native display_name; Close invokes plrBackend.onCloseCalled(), Cancel only closes the prompt.
- Shared dialog presentation: `UiMessageDialog.qml` and `UiDialog.qml`. Changes here can affect many dialogs, not just exit; scope any restyle deliberately.
- Player integration/context: `plr.qml`; keymap utility logic: `KmmUtils.js`.

The native Qt modules/backends and UiTheme are dependencies supplied by the emulator; this snapshot is not an independently runnable Qt project. The user said the boot screen is already correct: no boot-controller/video edit is requested for this work.

## Validation

```sh
python3 tests/emulator_source_snapshot_test.py
```

This checks the downloaded files, not QML runtime compatibility. No actual player patch, rendering or behavior test has been performed using this new source snapshot yet.
