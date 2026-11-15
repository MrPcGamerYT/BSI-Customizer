# Emulator UI Studio

Open **Player Interface → Emulator UI studio**. This is a native customizer screen, not an HTML replacement or a replacement emulator.

## Workflow

1. Select the correct `HD-Player.exe`. Close the emulator before applying.
2. Choose an accent, corner radius, icon family, motion and duration. The base remains black. Save/load an editable `bsi.skin` JSON design as needed.
3. **Manually Scan Resources** on the Resources page. Opening the studio does not scan or inspect the player.
4. Optionally select exact SVG resources and map built-in glyphs or uploaded static SVGs. Hover/click variants are separate targets. The preview illustrates the family, not exact custom target mappings.
5. **Check native coverage**, then **Generate & stage native UI**. A full supported source contract is required; this is not palette-only fallback success. Existing Workspace work is never silently discarded.
6. In **Workspace**, inspect the queued operations, **Dry Run**, review and **Apply**. The standard EXE hash, verified resource/cache mapping, output-hash confirmation and recovery safeguards apply.
7. Start the emulator and check Settings, sidebar/topbar, popups and shared controls. Automated parsing/runtime checks do not substitute for this visual test on your player version.

## Files and integration

Generation publishes a complete, verified folder under:

```
<player folder>/Assets/BSIStudio/studio-<content hash>/
```

The versioned folder avoids collisions with an existing `Assets/Theme.qml`, boot media, installer artwork or another theme. The native player QML is patched to import this actual directory; it is not merely a folder of disconnected reference files.

- `Theme.qml`, `qmldir`: shared color, corners, duration and motion.
- `Icons.js`: icon-family treatment used by the control background.
- `NativeControlStyle.qml`: rounded, outlined hover/press states.
- `SettingsShell.qml`: black shell, accent hairline, real fade/rise/slide/scale entrance.
- `BSISettingsOutline.qml`: selected Settings navigation outline and accent rail.
- `SidebarSkin.qml`, `BSISidebarVisual.qml`, `SidebarVisual.qml`: non-interactive chrome border and breathing edge.
- `TopbarSkin.qml`, `BSITopbarVisual.qml`: non-interactive topbar border/accent edge.
- `profile.json`, `README.txt`: editable design and recovery guidance.

All 13 existing player-skin components bind their skin properties to the generated Theme. Actual icon SVG resource replacements are staged with the same EXE batch, including any exact-target custom mappings. Native setting-page loaders, Save/discard handling, actions, anchors and game rendering remain in their original sources.

**Export Assets bundle** writes the same self-contained versioned bundle to a folder you choose but does **not** patch the emulator. Generation/staging can leave an unused bundle if you cancel the batch or Dry Run fails; the EXE is unchanged until Apply.

## Motion and scope

- `none` disables generated motion and sets shared animation duration to zero.
- `fade`, `rise`, `slide`, `scale` control Settings-shell entrance. Shared popups still use opacity transitions, not arbitrary slide/scale movement of their hit targets.
- Non-none motion enables the low-frequency sidebar edge pulse. No boot particles, loading screen or installer video are generated.
- The customizer's reduced-motion switch governs its own preview, not the exported emulator preference. Choose `none` for a static emulator design.
- The preview is illustrative. The actual native Settings layout and setting pages are retained; this is not a promise of identical OEM layout on every version.
- Unknown, ambiguous, companion-DLL/RCC-only and unsupported compiled sources are refused. An already-rebuilt Settings/About shell may need recovery before this source contract can be used.
- No fake `Navigation.js`, optimizer page or button is generated. Native navigation and existing optimizer workflows remain intact. Android content and OS-native dialogs are outside this QML surface.

## Recovery and editing

Bundles are content-addressed and never overwritten. A modified existing bundle is refused, and missing/tampered bundle files block Dry Run/Apply. Loading another design creates a new bundle and stages updated native imports. Use this studio, not the separate Add-ons skin deployment, to reapply a player that already imports a Studio bundle.

Restore the matching EXE snapshot from **Backups** to undo. Bundles are deliberately retained: an older restored EXE may still reference them. Do not delete an active bundle, or move the emulator to another folder without regenerating/restaging there. This multi-file feature publishes its immutable bundle **before** the normal single-EXE transaction; it does not claim an atomic transaction across EXE and Assets.

Branding remains **𝓥𝓞𝓘𝓓**, **Developed By NOVA XIS**, <https://www.youtube.com/@nova_xis20>.
