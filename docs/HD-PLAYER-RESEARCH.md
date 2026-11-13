# HD-Player research: missing customization and patching features

Research date: 2026-11-08. This is a feature proposal, not an implementation or a claim of Windows runtime verification.

## What was inspected

The four executables supplied in [MrPcGamerYT/HD-Player](https://github.com/MrPcGamerYT/HD-Player) were downloaded outside this repository and inspected statically. No supplied executable or DLL was run. The labels below reproduce the sample filenames; “latest” does not establish the newest publicly available build.

| Sample | Embedded FileVersion | Qt major evidenced by DLL-name strings | Readable QML recovered by a bounded zlib scan |
|---|---|---|---:|
| `HD-Player MSI v5.9.exe` | 5.9.300.6315 | Qt 5 | 62 streams |
| `HD-Player v.5.10 bluestacks.exe` | 5.10.150.1016 | Qt 5 | 67 streams |
| `HD-Player MSI Latest version.zip` → `HD-Player.exe` | 5.22.75.6322 | Qt 6 | 97 streams |
| `HD-Player latest version Bluestacks 5.zip` → `HD-Player.exe` | 5.22.285.1013 | Qt 6 | 96 streams |

These stream counts are research results, **not** a complete resource-tree enumeration or a compatibility guarantee. Filename records and readable QML were found for `UiThemeObject.qml`, `UiButton.qml`, `UiToggleButton.qml`, `UiSlider.qml`, `PlrSidebar.qml`, `PlrTopbar.qml`, `UiSettingsWindow.qml`, `UiPerformanceSetting.qml`, and `UiGraphicsSetting.qml`. A production scanner must use the existing validated resource mappings rather than treating every zlib match as a writable resource.

No proprietary binaries or extracted player sources have been added to this repository.

## What BSI already provides

The current application already has palette and gradient editing, image/SVG/QML resource extraction and replacement, title-bar/sidebar layout and ordering, UI cleanup, About and Settings customization, boot-video editing, the FPS slider ceiling patch, debloat switches, dedicated-GPU/high-frame-rate preferences, profiles, dry runs and recovery.

The proposed additions should expose **structured, tested controls** for these existing engines—not duplicate the resource editor or claim that arbitrary QML editing is a new feature.

## A. Appearance: recommended next controls

### 1. Player design-token editor

The recovered Qt 6 theme declares `pFontFamily`, title/body/button font sizes, and per-button-size height, radius, padding, spacing and icon dimensions. Its button colors include normal, hover and clicked states, borders and text.

Add a dedicated **Player appearance** section, distinct from the existing Appearance page that styles BSI itself:

- Font-size scale and font-family selection. Custom font-file embedding is a separate, later capability; changing a family string alone does not install a font.
- Compact/comfortable button-density presets.
- Corner-radius, padding and icon-size controls for recognized theme tokens.
- A contrast/readability checker and an optional custom palette generator. Individual hover/pressed colors and derived preset states already exist; the missing part is validating relationships and explaining which actual controls consume them.
- Supported panel/background opacity controls. This is not a promise of Windows acrylic/blur behind the emulator.

Use numeric profile fields and an allowlisted token catalog. Do not accept arbitrary QML expressions through a color or radius field. Inspect the selected build, show unsupported controls as unavailable, and retain exact original values for undo.

### 2. Complete icon packs

The samples contain normal, `_hover` and `_click` artwork families for sidebar/settings controls. Extend the existing image replacement engine with a pack manifest that groups these states, previews them together and validates every member before one dry-run/apply operation. Avoid a half-applied pack when one image is missing or incompatible.

### 3. Reduced-motion and lightweight-chrome presets

Start with recognized player animation durations and optional visual elements. A hidden item is not necessarily an inactive loader or timer: inspect `active`, `running`, timers and effect dependencies before calling anything a performance optimization. Never disable input handlers, navigation or required settings logic as part of an appearance preset.

### 4. Compatibility-aware theme packs

Extend the existing Workspace profile format with the new design tokens and a capability report. Revalidate logical resource paths and the current target hash on import; never replay raw offsets against an updated player. Keep the existing hash-bound dry run, read-back verification and recovery model.

## B. Performance: useful controls without invented keys

The recovered performance/settings QML references player controls that BSI does not currently expose as a dedicated per-instance tuning editor. The existing High frame-rate option already writes `enable_high_fps=1`, `max_fps=240` and `enable_vsync=0` together when those keys exist; a new editor should let the user review/change them independently rather than pretending VSync/high-FPS support is entirely absent. Examples observed in the newer BlueStacks sample:

| Area | Observed settings/callbacks | Proposed behavior |
|---|---|---|
| CPU/RAM | Instance `cpus`, `ram`; machine CPU/RAM queries; allocation warnings | Show current allocation and host limits; leave headroom for Windows and explain multi-instance tradeoffs |
| Frame pacing | `max_fps`, `enable_high_fps`, `enable_vsync`, `enable_fps_display` | Preserve the player's high-FPS/VSync interaction; distinguish a ceiling from measured game FPS |
| Graphics | Instance `graphics_engine`, `graphics_renderer`; `vulkan_supported`; engine-support callback | Offer only supported choices; require the appropriate player restart/reinitialization |
| Player UI rendering | `bst.qt_renderer` | Keep separate from the Android graphics renderer |
| Textures | Instance `astc_decoding_mode` | Read the build's supported choices rather than inventing a universal value |
| Memory behavior | `bst.mem_swap_enabled`, `bst.mem_pcr_enabled`, `bst.mem_pcr_pclimit` | Expose recognized native presets cautiously, with exact-value restore |

The older sample must be inspected separately before enabling these controls for it. QML references prove that the UI declares a setting; they do not prove every engine build accepts the same values or that changing it improves performance.

### Proposed tuning workflow

1. Select an instance and read its existing configuration with `DebloatConfig::File`.
2. Check the selected player's declarations, supported choices and host resources.
3. Preview a **Balanced**, **Low-end host**, or **Multi-instance** plan as a key-by-key diff. These are proposed presets, not existing features or guaranteed speedups.
4. Refuse missing keys, unsupported values and unsafe allocation requests rather than creating guessed settings.
5. Require the player to be closed, preserve byte-for-byte undo, then apply and read back.
6. Offer a user-run before/after benchmark on the same instance, game scene and settings. Record actual results separately from estimates.

Do not auto-apply a “best FPS” profile: more CPU/RAM can worsen host contention, and renderer suitability depends on the GPU, game and player build. Debloating or simplifying the player's Qt UI is also not equivalent to accelerating the Android rendering engine.

## C. What is actually recoverable from EXEs/DLLs

- **Qt resources:** virtual paths and stored payloads can be recovered from resource trees. The original `.qrc` project manifest is generally a build input, not something reconstructed exactly from the executable.
- **QML:** some builds retain readable source; others retain compiled QML units. Bytecode inspection/disassembly is not recovery of the original project, comments and formatting.
- **Native DLLs:** static disassembly/decompilation produces approximations, not original C++ source. Use it only for a specific native question after imports, version information and QML/settings evidence have been checked.
- **Performance:** QML can reveal settings and UI work. Emulator engine behavior and actual frame times still need controlled testing on Windows.

BSI already contains Qt resource readers, PE utilities and an instruction decoder. Reuse them before adding a general-purpose decompiler dependency.

## D. Files worth supplying next

The linked player EXEs are already available; sending identical copies again is unnecessary. For a target not represented above, supply a small, version-matched bundle:

1. Exact `HD-Player.exe` from the installation actually launched, plus its product/OEM name and full version.
2. Matching `Qt5Core/Gui/Qml/Quick.dll` or `Qt6Core/Gui/Qml/Quick.dll` for version identification. For an FPS issue, include the matching `Qt5QuickTemplates2.dll` or `Qt6QuickTemplates2.dll`, its clean `.bak` if present, and the exact Analyze message.
3. `Qt5QuickControls2.dll` / `Qt6QuickControls2.dll` and any player-specific UI plugin actually evidenced by imports or resource discovery. Do not send every engine DLL indiscriminately.
4. For configuration work, a redacted `bluestacks.conf` containing the relevant instance/global keys, plus the instance architecture and screenshots of its Performance/Graphics pages. Remove identifiers, paths, account information and unrelated settings.
5. Screenshots of the desired appearance and a description of the performance problem. For benchmarks: CPU, GPU, RAM, player version, renderer, allocation and the same repeatable game scene.

Do not upload Discord tokens, `auth.json`, account cookies or private credentials. Only supply software you are authorized to inspect and modify. Keep reference binaries outside Git; retain original backups and avoid arbitrary third-party DLL replacements.

## E. Suggested implementation order

1. **Read-only capability inspector:** identify player/Qt version, resource ownership, editable design tokens and declared performance settings.
2. **Appearance MVP:** font scale, button density/radius and interaction-state colors for one verified Qt 6 build, with synthetic-resource regression tests.
3. **Performance MVP:** per-instance CPU/RAM/FPS/VSync diff and exact restore, followed by renderer options only where support is established.
4. **Compatibility expansion:** verify the Qt 5 and MSI samples, then add version-specific adapters where their structures differ.
5. **Windows validation:** actual player launch, visual inspection, settings persistence and repeatable performance measurements. Static Linux inspection cannot certify these results.

### Sample SHA-256 fingerprints

- MSI 5.9.300.6315: `1f473ca2ec1e684cf85dbf1cacd47ed0cb33c27c6fe298ada1775f2a3414f2ed`
- BlueStacks 5.10.150.1016: `bcec63030f032fef60850559b5a5c40fdcce44f3b129aa5ccee05e990957467b`
- MSI 5.22.75.6322: `b373ad8ce5dbc6b1e605b3c153d20b69bf5769b7ff82f896291e4844ab418efd`
- BlueStacks 5.22.285.1013: `d2f8509d025f90bd23604aa1c8f6981c456468bceab4db4a30bb380806826f39`

## F. Additional gap audit: QML, QRC, compiled units, hex and native code

Audit method: compare the current Backend API, patch engines, HTML/QML controls and Workspace schema against the four supplied samples. “Missing” below means **no dedicated feature/workflow in this checkout**. It does not mean an experienced user cannot already make some of the same edits manually through Resources.

### Already implemented — do not rebuild as “new” features

- Solid palettes, eleven presets, derived hover/pressed states, switch recoloring and two-stop horizontal/vertical gradients.
- Manual Qt-resource extraction/replacement, QML/JS/SVG text editing, image preparation, batching, dry runs and recovery.
- Sidebar relocation/order/hiding, UI cleanup, About editing and the rebuilt Settings panel.
- Boot animation/video/text, the high-FPS slider ceiling, the existing high-FPS/VSync config switch, Discord ID/version-resource text editing, debloat and disk tools.
- Targeted compiled-unit invalidation and About-page compiled-string edits. These are real compiled-QML capabilities, but not a general bytecode editor.

### Genuinely missing or only partially covered

| Priority | Proposed feature | Patch layer | Current gap and concrete scope |
|---|---|---|---|
| 1 | **Numeric player design tokens** | QML source + Qt resource writer | **First slice implemented** in Player Appearance Studio (see `docs/PLAYER-APPEARANCE-STUDIO.md`): validated controls for the sample's button `radius`, `height`, `horizontalPadding`, `spacing`, icon dimensions, font `pixelSize`, toggle/slider dimensions and recognized chrome dimensions such as `pSidebarWidth`/`pBorderWidth`. Preserve bindings and restore exact original values. |
| 1 | **Font pack manager** | QML + binary Qt resources | TTFs can already be replaced as generic resources, but there is no font-aware workflow. Validate font headers/family/style, preview text, group regular/medium files, update `pFontFamily` consistently and check storage budgets. The samples contain Rubik; the newer BlueStacks sample also contains Bricolage Grotesque. |
| 1 | **State-aware icon packs** | Qt resources | Individual artwork replacement exists; a normal/`_hover`/`_click` pack manifest, completeness check and one reviewed batch do not. This is a workflow addition on top of the existing writer. |
| 1 | **Player UI scale / overlay / hotkey editor** | Existing config, not a binary patch | The newer sample declares `bst.ui_scale_factor`, `bst.key_controls_overlay_opacity`, `bst.fixed_window_size`, `bst.custom_cursor_enabled` and `bst.shortcut.*`. BSI has no dedicated editors for these. Read existing values, validate the player's formats/conflicts and use exact config restore. |
| 2 | **Advanced gradients** | QML source | Current support is explicitly two-stop linear horizontal/vertical. Add multiple stops and per-state gradients where the native component supports them. Diagonal/radial effects require a compatible rendering implementation (for example an existing Shape/effect); do not assign an unsupported Gradient object to a Rectangle color. |
| 2 | **Animation tuner** | QML source | No general control for recognized transition `duration`, easing or decorative effect intensity. Patch only identified visual animations, never boot completion, input, ads-skip or backend polling timers merely because they are timers. A reduced-motion preset is not a measured game-FPS optimization. |
| 2 | **General UI text/translation catalog** | QML source + supported compiled string tables | About, boot text, removals and PE version strings are specialized. A catalog could list other captions/tooltips with their resource and active source/compiled representation, preserve placeholders such as `%1`, and refuse unsafe/ambiguous edits. The samples contain many `qsTranslate` calls but no recovered `.qm` translation files; external language files would need separate evidence. |
| 2 | **Compiled-QML unit inspector** | `qv4cdata` bytecode | Add read-only unit/version/header, string-table and source-association diagnostics across pages, plus active/shadowed status. The older samples contain approximately 99/105 magic occurrences; the two newer samples contain none. This is a useful compatibility tool before attempting any broader literal edits. It is not a full QML decompiler. |
| 2 | **Direct DLL/RCC resource target browser** | Qt resource trees in PE/RCC | About can locate a neighboring page host, but the main Theme/Resources/Workspace flows remain centered on the selected EXE; Workspace explicitly accepts `.exe`. Add a validated target selector for the UI-bearing DLL/RCC, show which file owns each page and keep recovery per actual target. Registration/ownership must be proven, not guessed from a filename. |
| 2 | **Reconstructed resource-pack export/import** | Qt resources + manifest | Extract All exists, but there is no versioned pack containing logical paths, hashes, compression metadata and replacement assets. Export a reconstructed manifest and import only matching, unambiguous existing paths through Workspace. Do not call it the original `.qrc`; adding new tree entries or registering a new pool is a separate, harder feature. |
| 3 | **Signature-based native Patch Lab** | PE / x64 / hex | Current native patches are specialized (FPS, integrity and RPC). Add masked byte-pattern search, typed UTF-8/UTF-16/int32/int64/float/double previews, section/ownership checks, expected-original bytes and exact undo. Start read-only; instruction-aware validation is mandatory before any code change. No universal “replace every match” button. |
| 3 | **Normal-mode FPS ceiling option** | Existing QuickTemplates2 setter patch | The current setter replaces only the 240.0 high-mode maximum and deliberately leaves normal mode at 60. A separately reviewed normal-mode limit could extend the known setter mechanism. It needs matching DLL fixtures and tests; it is not achieved by randomly replacing all `60.0` constants. |
| 3 | **Windows application icon editor** | Native PE `RT_ICON` / `RT_GROUP_ICON` | Qt logo/image replacement and boot branding exist, but no native Windows icon-group editor was found. This would change the executable's Explorer/taskbar identity rather than only its QML artwork. It needs multi-size ICO validation and resource-directory length/offset handling, not the Qt resource writer. |
| 3 | **Update-aware patch migration** | Semantic recipes + hashes | Workspace already refuses stale fingerprints and imports logical settings, but does not rebase a custom QML edit onto a newer stock page. Store original fragments/structural anchors, compare old stock/new stock/custom, and migrate only unambiguous matches. Conflicts require review; never replay old raw offsets. |
| 3 | **Cross-feature conflict map / unified recovery plan** | All patch layers | Workspace detects conflicts inside its own batch, and Settings has feature-local recovery. A player-wide report could show which features touch the same QML/resource/DLL/config and coordinate separate recovery records. Do not claim cross-file atomicity unless it is actually implemented. |

### Other useful editors, with lower priority

- Resolution/DPI and per-instance display-name presets (`fb_width`, `fb_height`, `dpi`, `display_name`) using the player's own validation rules.
- Graphics renderer, ASTC and memory-mode selection as described in section B.
- A sound-pack editor only after locating the actual assets and loading path. The newer QML references `moneyPrize.wav`, but the recovered resource-name inventory did not establish an embedded audio pack; do not assume its storage location.
- A read-only PE section/import/export/resource-directory inspector. It supports diagnosis and Patch Lab but does not itself customize the player.

### Recommended next package

Build **Player Appearance Studio** first: numeric QML tokens + font packs + icon-state packs, with a capability report and one reviewed Workspace plan. The numeric-token slice is now implemented; font packs and icon-state packs remain next. Then add the **config-based player controls** (UI scale, overlay opacity, shortcuts, CPU/RAM/renderer). Add compiled-unit inspection and direct DLL/RCC targeting before expanding legacy-build support. Keep arbitrary native code patching last.

This order delivers visible changes without pretending that a full decompiler, original `.qrc` recovery or a generic safe hex editor already exists.
