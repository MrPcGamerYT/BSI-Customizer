# Shaders uses stock Settings controls, not a painted approximation

**UI implementation and bounded Qt tests, not installed-player acceptance.**
The user confirmed the newer screenshot was no longer transparent, but its
lighter outer frame still mismatched the native page. The v21 uniform-theme
correction and reported stretched polygons/Vulkan “Not detected” still need
installed-player verification. This change does not implement the requested
video-like texture-detail Potato effect. Optional resolution scaling is separate.

## Native analysis before implementation

The four original player images pinned in
[`emulator-qt-evidence.json`](../tests/fixtures/emulator-qt-evidence.json) were
inspected through the production registered-resource parser, without launching
an EXE or loading any proprietary DLL. Their controls, full theme and native
Display/Graphics/Performance footers agree on these public contracts:

| Native component/role | Shared contract used by Shaders |
| --- | --- |
| `UiButton` | `pSize`, `pColor`; medium/accent for Save, medium/secondary for actions, small/transparent for individual Reset |
| `UiCheckBox` | Native medium `pSize`; native image indicator and text colors |
| `UiComboBox` / `UiSlider` | Native medium `pSize`, with their own native color/state defaults |
| `UiColumnLayout` / `UiRowLayout` | Transparent Rectangle wrappers whose default children belong to an internal real layout, not aliases for the Qt layout type |
| `UiScrollView` | Native scrollbars, including `uiBackend.pFlickableMoving` and its native notify signal |
| `UiImage` | Native MenuInfo artwork; use an absolute player-QRC source from the module's different resource directory |
| Full `UiTheme` | The same live context object the native components use, not a palette/body-font snapshot or replacement singleton |

All these controls and `UiThemeObject.qml` are registered at QRC root in the four
samples. The module imports `"qrc:/" as Native`. No player control implementation,
artwork, theme file or extracted resource bundle is shipped inside BSIShaders.
The old `ShaderButton`, `ShaderComboBox`, `ShaderSlider`, `ShaderCheckBox`,
`ShaderScrollBar`, `ShaderSurface` and palette-approximation `ShaderStyle` are
removed, including their resource entries.

### Exact Save is family/theme-specific

Both pinned MSI themes use the native gradient-border/layered button component.
Both pinned BlueStacks themes use native solid accent states. Replacing either
with a generic accent fill is not stock parity. Shaders leaves each native
implementation and its normal/hover/click material objects intact.

Save uses `UiTheme.buttons.sizes.medium`, `UiTheme.buttons.colors.accent`, and
the native `QObject` translation context for “Save changes”. In these samples,
medium means 32 px height, 16 px horizontal padding, 8 px spacing, zero corner
radius, and the 15 px themed medium button font. Clean Save remains visible,
disabled at 0.4 opacity. The footer remains stationary with native MenuInfo at
12 px and the native body-small notice. The approved **6 px corners apply to
the navigation tabs**, not to a custom-rounded Save button.

The native Settings tab/close No/Discard callbacks are retained. There is no
second permanent Discard button. All ten visual sliders and individual Reset
controls remain; the independent resolution draft/recovery is also retained.

## Page/material isolation and update safety

The module owns a flat primary80 page/footer backing. **v21 makes the entire
Settings frame, gutters and page/footer the same flat, opaque native color**,
removing the screenshot's brighter outer gradient/dark inset mismatch. Glass
belongs only to the selected generated tab. A single owned material per tab is
attached by a live selection binding and reused; unselected/hovered tabs have
no Gradient. No shell reflection or custom outer rim is installed.

The shipping About pass stops at every Loader and never follows native control
`background`, `handle`, `popup`, layered parts or Overlay contents. It does not
flatten the native MSI Save gradient to work around a renderer issue.
Installation/update refuses pre-v21 designs and missing/ambiguous/unreadable
root-QRC dependencies, before any target write. **Update the rebuilt Settings
panel first.** Exact removal/recovery of older shader overlays stays available.
No EXE growth or source-slot relaxation is allowed; the added UI lives in the
regular module's resources.

Test all middle Settings tabs, not just Shaders, over a moving contrasting game.
The Qt tests establish the new theme/material boundary, not a physical-driver
root cause or installed-player certification of the geometry/shader path.

## Tests and what they prove

`shaders_native_controls_test.py` requires the pinned original references and
the production `shader-settings-image-test` exporter. Per matching Qt family it:

1. Verifies the reference SHA-256; reads a bounded EXE or a single bounded ZIP
   member without path extraction; never executes the EXE or loads player DLLs.
2. Exports unmodified native QML/artwork to a fresh temporary directory via the
   registered-resource parser. Every byte is hashed again after QRC registration.
   Extracted sources/artwork are temporary only and are not uploaded as artifacts.
3. Loads the actual native controls and **actual native theme**, plus the real
   BSI C++ plugin/page and the unmodified generated About material. Native C++
   tooltip attachment is an explicit nonpainting service double; Display is a
   typed contract endpoint, not an emulator backend.
4. Compares Save pixel-for-pixel against a separate native button using the same
   live theme at identical coordinates in clean, enabled, hover and pressed
   states. The reference is outside the shell decorator's scope.
5. Physically exercises the checkbox, combos, slider, Save, Enhance and all ten
   right-aligned, fully visible Reset buttons at 750/420 px. Includes empty-font
   layout stress with no repeated reveal, click retry or property-only fallback.
6. Samples page and footer over two contrasting host colors and a live theme
   change, checks that native slider/Save materials survive decoration, and
   physically tests the long Keep-current-display recovery action at 420 px.
7. Requires separate transparent-page and altered-Save-material negative runs
   to fail the same unchanged pixel assertions. Neither is accepted as a pass.
8. Checks both temporary and original reference bytes remain unchanged.

The CI workflow schedules both legacy families on the independent Qt 5 host and
both modern families on each independent Qt 6 Windows host; a configured step
is not a passing result. Authored root-QRC contracts
remain for fast standalone and full Settings navigation/prompt tests; their
markers cannot satisfy real-resource mode. The shareable live preview likewise
registers explicit sample root-QRC controls and one complete shared sample
theme; it does not export OEM resources or revive the deleted lookalikes.
`shaders_preview_ui_test.py` covers all 13 physical tab selections in two
palettes, exact opaque frame pixels and physical No/Discard actions through
the production plugin/page. Its authored preview prompt is not the player's
native dialog. Existing strict glass paint/contrast, DPI, callback, persistence,
bounded compression and recovery tests stay required.

The native-page opacity and Save oracle use the public asynchronous
`QQuickItem.grabToImage()` path. `QQuickWindow.grabWindow()` returns an empty
image on the independent Qt 5 software hosts; that is a capture-API limitation,
not permission to skip pixels. `shaders_qt_capture_test.py` independently checks
real changing pixels, immutable snapshots and a rejected nonpainting negative
without needing a shader module or OEM resources. One scene-update boundary is
used before each capture; no capture-until-pass retry. Selected-tab contrast
compares two original sample points in **one** frame with the unchanged strict
`>6` lightness delta. This synchronization hardening alone does not diagnose a
Windows Qt 6.8 reflection failure or certify a physical renderer. Failure
annotations retain the actual tail/values and frames for follow-up.

These are real Qt control/source/material tests. They do **not** establish an
installed player result, readable font paint in the fontless stress case, game
capture, Vulkan detection, texture sampling changes, frame times, NVIDIA profile
compatibility or universal GPU support. PR acceptance/merge remains blocked on
the outstanding player and requested texture-effect work.

## Resolution naming is not a texture implementation

The visible section says **Render resolution (optional)**; human-facing save,
validation and recovery diagnostics also say resolution rather than Potato.
The v2 internal `potatoMode` and `previewPotato` identifiers remain solely to
preserve saved settings and exact restore/interrupted-save recovery. They do
not connect the separate sampler-policy experiment to game textures. See
[resolution limits](POTATO-NATIVE-RESOLUTION.md) and the
[texture-only implementation boundary](TEXTURE-QUALITY-PORTABILITY.md).
