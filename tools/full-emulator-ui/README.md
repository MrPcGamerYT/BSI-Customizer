# VOID Carbon Suite 07 — full eligible emulator UI

**𝓥𝓞𝓘𝓓 · Developed By NOVA XIS · https://www.youtube.com/@nova_xis20**

This changes the emulator, not the customizer. It is not another optimizer-only
ZIP or a set of disconnected mock QML screens. Actual mapped native sources and
their compiled-cache owners are patched in the established backed-up transaction.

## Scope

- Black Settings shell with a full native-page stack, isolated source→ID→label
  Settings icons, rounded selected navigation, version badge and restrained motion.
- All eligible native Settings pages; original models, validation, Save/discard,
  page indices and backend callbacks remain. Optimizer is appended last.
- Sidebar and topbar passive icon/hover styling; existing input/shortcuts remain.
- Buttons, dropdowns, switches, checks/radios, fields and sliders on those surfaces.
- All mapped eligible dialog/popup QML and controls-editor QML, including native
  volume/game-controls/exit/editor surfaces when they exist in the selected build.
  Palette/corners/hover color transitions are applied across the mapped inventory;
  supported window subtrees also receive the passive native-control adapter.
- Studio04 optimizer page and the latest fixed Helper06 are retained. The complete
  shared icon library stays separate from Settings symbols (no limited Icons.js).

**Not literally every UI-looking file can be rewritten safely.** Boot/loading,
auth/account/legal, shared boot dependencies, Qt framework code, JavaScript
business logic, Android/game content and OS-native dialogs are protected.
Dynamic/custom C++ paint bindings may be unsupported. `UI-COVERAGE.json` lists
changed, protected, verified/unchanged resources and unsupported visual bindings;
missing resources are not silently advertised as redesigned. The source-only test
snapshot does not prove compatibility or exact appearance on a live OEM player.

## Windows installation

1. Restore and close any optimizer session. Close all players and the multi-instance
   manager. Keep `.VOID-Reference-UI.json` and all old backup folders.
2. Extract the entire ZIP outside the emulator into a permanent folder. Python
   **3.9+** is required. The scripts do not download anything at installation time.
3. Run **CHECK-UI.cmd**, choose the folder containing `HD-Player.exe`. This explicitly
   scans mapped resources but changes no installation files. Inspect the resulting
   **UI-COVERAGE.json**. It previews planned changes; it does not mean installed.
4. Optional: **EXPORT-ALL-UI-SOURCES.cmd** writes a read-only ZIP containing the
   actual planned native QML/JS/SVG and replacement Assets plus coverage. This
   exports all mapped sources, not just Settings. Review before sharing: source
   paths/logic may contain private build information. No automatic upload occurs.
5. Run **INSTALL-UI.cmd**, select the SAME root, inspect the proposed changes and
   type **APPLY**. EXE/DLL resource edits invalidate Authenticode signatures. File
   permission elevation, if required, uses the existing explicit installer flow.
   Nothing disables security controls. Backup/journal and read-back checks cover
   every changed binary, external asset and the UI state.
6. Restart the player. Settings must show **CARBON SUITE 07**. Test every native
   page's controls/Save/discard, sidebar/topbar, volume/game-controls/exit popups,
   controls editor and optimizer. Boot/auth must behave exactly as before.
7. If a different/cached page appears or any safety check refuses, STOP. Run
   **DIAGNOSE-UI.cmd** and review/share the diagnostic ZIP. Do not delete state,
   force replacement, or copy loose source-preview files over the emulator.

A tracked earlier full-UI installation can be upgraded only when its current
files match its state. In particular, manual icon/shell edits without state hash
updates are refused, not blindly adopted. A different source contract/build can
also be refused. In either case diagnostics are needed before a safe migration.
Do not use the separate historical mixed-UI migration unless specifically audited.

## Existing Helper06

The optional **Helper/** directory is the complete tested Helper06 package.
If your existing helper already says HELPER 06, keep that registered installation;
Suite07 does not register another copy. Settings continues to use voidoptimizer-v3.
If upgrading an older helper, restore/close it and replace its EXE + BUILD-REPORT
in the same registered folder as previously instructed. Never delete recovery.

## Editing and recovery

Edit the extracted pack's **design.json**, then run Check again before Apply.
Colors, font family, radius, motion and duration are validated as data, not arbitrary
QML expressions. The font must already exist on the user's Windows installation.
Set `motion` to false for static styling. These choices do not change boot/auth.
Assets are visibly included under `Assets/VOIDReferenceUI` for review. Generated
Theme.qml reflects the current profile at Check/Apply time. Do not manually copy
that directory over a player with tracked state.

**UNDO-UI.cmd** restores the latest matching UI transaction using its journal,
including exact prior state/assets/binary bytes. Close the player first. The
helper's protocol registration is independent and unchanged by UI restore.
Backups are kept under the emulator's `VOID-UI-Backups`. Do not delete them.
If interrupted, preserve the journal and files and use recovery; never clear state
merely to get around a mismatch. Do not run other installers concurrently.

## Tests and provenance

Synthetic PE/RCC tests exercise the full reviewed source inventory, source/cache
mapping, all-source export, untouched boot/auth/JS, idempotence, asset-state mismatch
refusal and byte-exact restoration. Qt tests exercise the connected shell, native
control doubles, click routing, popup open/close, lazy optimizer and passive chrome.
Fixture screenshots are labelled examples, not live MSI/BlueStacks captures.
Helper06's own tested ZIP/build report is included without rebuilding its binary.
See `BUILD-REPORT.json` and `Source/PROVENANCE.json`. No player EXE/DLL is distributed.
