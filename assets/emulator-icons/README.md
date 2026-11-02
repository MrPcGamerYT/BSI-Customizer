# BSI Outline — emulator chrome icon pack

22 original, transparent SVGs for **BlueStacks/MSI Player topbar and sidebar**.
These are not a replacement for the BSI Customizer app's own navigation icons,
and they are not traced from the supplied screenshot or bundled with game art.

## Look

- 24 × 24 viewBox, rounded caps/joins, 1.7-unit outline by default.
- Explicit `#F1F5F9` strokes for dark bars (no currentColor/font dependency).
- Plain SVG primitives supported by QtSvg; no scripts, fonts or external assets.
- Open `preview.html` to see the actual SVGs and an illustrative bar mockup.
- In the updated Customizer's **Resources → Emulator icon studio**, color and
  stroke can be previewed before staging an individual icon.

## Apply in the emulator

1. Close all emulator instances. Keep an original EXE backup. A player update
   may replace your customized resources.
2. Select the correct player executable in BSI Customizer and explicitly click
   **Scan Resources**. Opening the page never scans.
3. Find the matching SVG button icon. `manifest.json` has search hints, not
   guaranteed names: resource paths differ between player versions.
4. Extract the original if needed. Select the right icon (and separately review
   normal, hover, active and disabled variants). Never replace a logo or a
   background just because its filename happens to match a hint.
5. **Updated HTML editor:** choose a pack icon, stage it into the selected SVG,
   review the draft/native size measurement, then **Save resource text**.
   Staging itself does not write. Numeric root dimensions are retained when
   possible, with a 24 × 24 fallback. Existing text drafts require confirmation
   before replacement.
6. **Existing builds:** use **Replace with file…** and select the corresponding
   SVG from `outline/`. That uses its fixed 24 × 24 intrinsic dimensions.
7. The original native writer controls backups, capacity, verified relocation,
   stale-target validation and refusals. Do not force oversized icons into a
   slot or truncate SVG data. If it refuses, leave the original unchanged.
8. Restart the emulator and check the actual result. QML sizing/tint rules can
   override appearance. These SVGs do not change actions, bar placement,
   spacing, auto-hide, key mappings or game graphics.

**Recovery:** Restore EXE backup restores the whole backed-up file, potentially
including other edits made since that backup. It is not per-icon undo. For an
individual resource, keep its extracted original and replace that resource.

## Rebuild the assets

`node scripts/export-emulator-icons.cjs` regenerates SVGs/manifest from the
trusted pack in `src/web/emulator-icons.js` (run from the repository).

Copyright (c) 2026 MrPcGamerYT. MIT license; see LICENSE.
