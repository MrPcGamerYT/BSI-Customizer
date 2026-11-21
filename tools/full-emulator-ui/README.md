# VOID OLED Studio 09 — six live native themes

**𝓥𝓞𝓘𝓓 · Developed By NOVA XIS · https://www.youtube.com/@nova_xis20**

This changes the emulator, not the customizer. It is not another optimizer-only
ZIP or a set of disconnected mock QML screens. Actual mapped native sources and
their compiled-cache owners are patched in the established backed-up transaction.

## Scope

- Black Settings shell with a full native-page stack, isolated source→ID→label
  Settings icons, rounded selected navigation, version badge and restrained motion.
- All eligible native Settings pages; original models, validation, Save/discard,
  page indices and backend callbacks remain. No optimizer tab is added.
- Sidebar and topbar passive icon/hover styling; existing input/shortcuts remain.
- Buttons, dropdowns, switches, checks/radios, fields and sliders on those surfaces.
- All mapped eligible dialog/popup QML and controls-editor QML, including native
  volume/game-controls/exit/editor surfaces when they exist in the selected build.
  Palette/corners/hover color transitions are applied across the mapped inventory;
  supported window subtrees also receive the passive native-control adapter.
- No optimizer page, sidebar action, F9 optimizer shortcut or Helper executable
  is included. The complete shared icon library stays separate from Settings symbols.

**Not literally every UI-looking file can be rewritten safely.** Boot/loading,
auth/account/legal, shared boot behavior, Qt framework code, JavaScript
business logic, Android/game content and OS-native dialogs are protected.
Dynamic/custom C++ paint bindings may be unsupported. `UI-COVERAGE.json` lists
changed, protected, verified/unchanged resources and unsupported visual bindings;
missing resources are not silently advertised as redesigned. The source-only test
snapshot does not prove compatibility or exact appearance on a live OEM player.

## OLED Studio 09 — live native Appearance

Open native **Settings → Themes** (top-right). Choose **OLED Black** (default),
**Midnight Blue**, **Amethyst**, **Forest**, **Ember**, or **Rose Noir**. These are
six different background/surface/text/border/accent palettes, not six accents on
one Carbon skin. Each also has its own corner and transition defaults.

- Edit all seven color roles, font family, corners, motion and duration in Appearance.
  **Apply my colors** stores a custom palette; **Reset** returns to OLED. Choosing
  a preset clears corner/duration overrides but keeps your font/motion preference.
- Theme-aware masks tint Settings, known titlebar/sidebar and editor tool icons;
  the shared 57-name Icons.js contract is unchanged. Unknown native icons remain.
- Already-open integrated controls, dialogs, editor cards/key chips and popups
  bind to the active theme in the same QML engine. A separately loaded engine
  reads the persisted choice when its window reopens. No polling is added.
- When the emulator ships Qt.labs.settings, choices persist per Windows user in Qt's GenericConfigLocation under
  `VOID-Emulator-Themes-v9.ini`, separate from tracked UI asset files. Therefore
  changing a theme does not invalidate the install record. Undo restores UI files,
  not user preferences; use Reset before undo if you want preferences cleared.
- A stripped OEM Qt build missing that optional plugin keeps working with live
  session-only themes and a visible Appearance notice, rather than failing to load
  the entire shell. No Qt DLLs or plugins are replaced by this pack.
- `design.json` remains the validated install-time OLED default; runtime custom
  choices override it. Hex colors only, no executable design strings. Fonts must
  already be installed. Text contrast warning is advisory, not a color lock.
- `UI-COVERAGE.json` includes `live_theme_bindings` and `theme_presets`. Unsupported
  native expressions/artwork are reported rather than globally overwritten.

### Native window/dialog coverage retained

The Settings badge is **OLED STUDIO 09**. Native surface handling includes:

- Native Window/Popup `contentItem` and separate Dialog header/footer are visited.
  Previously a Window-root editor could get source colors but no control adapter.
- `UiDialog.qml` and `UiWindowedPopup.qml` receive only an additive passive loader.
  They are NOT globally recolored. The loader activates only for a registered
  player/Settings/editor window or its transient descendants, and not while the
  registered player reports boot incomplete. Original shared source/callbacks
  survive byte-for-byte after removing the hook. Framework/auth sources stay out.
- Registered native message boxes (e.g. close/restart) receive black rounded
  backgrounds, instance-local text/button palettes and native button input.
- Native volume/game-control popup controls and opaque neutral layout panels get
  the same control styling; bindings to native values/actions remain in place.
- Native editor key chips explicitly get a dark fill and legible light labels;
  their previous white fill was an implicit Rectangle default, not a color literal.
- Controls-editor cards get rounded hover states and 22 isolated neutral tool
  symbols, without replacing drag areas, scheme models or save/delete behavior.
- All 57 shared icon names remain available; separate raster glyphs improve
  compatibility on Qt builds where SVG image loading fails. Source generation is
  in `generate-glyphs.cjs`. No rasterization dependency is needed at install time.
- Discovery is coalesced on creation/visibility changes; no idle polling timer is
  added. Conditional native validation/notice colors are not globally overwritten.

Tests include reviewed native UiDialog/UiButton/layout/tile sources with OEM-only
model/enum/tooltip doubles, real Qt windows and input, plus source-inverse proofs.
The labelled fixture captures are not OEM screenshots or a claim that every
possible player build/custom C++/Android surface has been runtime-certified.

## Replace the current installed UI (including stale state)

Use **REPLACE-INSTALLED-UI.cmd** if you want the old UI removed/replaced instead
of a state-verified upgrade. Select the folder containing `HD-Player.exe` and type
**REPLACE**. Optional **CHECK-REPLACEMENT.cmd** previews this mode without writes.

This mode ignores the previous UI record and uses the files actually present.
It replaces existing pack assets, deletes obsolete files inside
`Assets/VOIDReferenceUI`, rebuilds the mapped native UI and writes a new record.
It handles a native/restored player with stale state and missing assets, as well
as edited assets on a recognized installed native bridge. Nothing must be deleted
manually first. Unknown native callback contracts can still require adaptation;
this is not a tool for replacing arbitrary EXE bytes with a different build.

Every replaced/deleted file and the old record are backed up in the same undo
journal. A failed apply rolls back; **UNDO-UI.cmd** restores the pre-replacement
files (including obsolete UI files). Apps, Android data, boot/auth, independent
helper registration and previous backups are not removed. Only choose the pack's
UI directory for its assets: obsolete files there are deliberately deleted.
No optimizer or helper is installed.

## Windows installation

1. Close all players and the multi-instance
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
6. Restart the player. Settings must show **OLED STUDIO 09**. Test every native
   page's controls/Save/discard, sidebar/topbar, volume/game-controls/exit popups,
   controls editor. Boot/auth must behave exactly as before.
7. If a different/cached page appears or any safety check refuses, STOP. Run
   **DIAGNOSE-UI.cmd** and review/share the diagnostic ZIP. Do not delete state,
   force replacement, or copy loose source-preview files over the emulator.

A tracked earlier full-UI installation can be upgraded only when its current
files match its state. In particular, manual icon/shell edits without state hash
updates are refused, not blindly adopted. A different source contract/build can
also be refused. Use the explicit replacement mode above for stale state/missing or edited pack assets.
Unknown source contracts still need adaptation.
Do not use the separate historical mixed-UI migration unless specifically audited.

## Windows access denied / read-only files

For `Program Files`, accept the Windows administrator/UAC prompt. If Windows
still denies the write, close the player and manager and run the CMD with
**Run as administrator**. An administrator token does not override every file
lock, ACL or security-software policy; these are not disabled by the installer.

The corrected writer handles read-only EXE/UI files: it temporarily clears only
the file's DOS read-only bit for replacement/deletion and preserves that bit on
replacement/undo. Staging files remain writable until the atomic replacement;
cleanup no longer masks the original error with a `.void-stage-*` access denial.
If a replacement fails and reports `original files restored`, it rolled back;
keep the backup, then retry after addressing the reported access problem.

## No optimizer

This corrected pack removes the optimizer tab/sidebar entry and its F9 shortcut.
It includes no optimizer/helper executable and registers no protocol. Existing
separately registered helpers/recovery are not deleted or modified. When upgrading
an already-applied tracked UI pack, old now-unreferenced optimizer asset files may
remain on disk; the emulator no longer loads them. Keep original backups/state.

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
control doubles, click routing, popup open/close, absence of optimizer entries and passive chrome.
Fixture screenshots are labelled examples, not live MSI/BlueStacks captures.
See `BUILD-REPORT.json` and `Source/PROVENANCE.json`. No player EXE/DLL is distributed.
