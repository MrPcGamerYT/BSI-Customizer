# Shaders uses stock Settings controls, not a painted approximation

**UI implementation and bounded Qt tests, not universal player/GPU acceptance.**
Preserve the approved v21 flat native shell, selected-tab-only glass and native
controls. A later user report confirms processed **OpenGL/GLES** presentation
with Vulkan configured; it is not Vulkan texture processing. The current UI
repair makes Potato review/draft state clear, handles eligible/current-size
resolution choices and uses the actual stock Graphics-style restart dialog.
The GL/GLES texture experiment remains bounded; optional Display scaling is
separate and is not a substitute for texture detail.

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
| `UiMessageDialog` / `UiDialogWindow.Simple` | Actual Graphics/Display restart shell, native dialog-button models, live theme, localized product title/description and modal/transient ownership |
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

### Stock restart and understandable drafts

A changed resolution's Save opens `Native.UiMessageDialog` **before** any native
write, with the exact stock **Restart Now / Discard Changes** roles. Closing
keeps the shader draft; Discard clears only that page's draft. Confirmation
revalidates the C++ preview stamp, commits/finalizes, then requests the typed
native restart. Acceptance uses the existing Settings restart-loader signal;
refusal keeps Settings visible and the saved target intact. A saved retry uses
**Later**, not a misleading Discard of already-saved values. An unavailable
restart contract has a manual notice and a correctly labeled Save action.
The previous custom inline two-click confirmation has been removed.

The Potato review checkbox is now immediately below its switch and is revealed
when needed; it is never silently acknowledged. Draft status explicitly says
**Not saved / current saved On or Off**, distinct from runtime sampler evidence.
A resolution choice already at the exact native size reports a no-op with no
Display write or restart. Profiles that would upscale a smaller original or
violate native bounds are omitted, with the reason shown; stale size drafts
can be dropped independently of visual/texture edits. These remain the same
native checkboxes, combo, text roles and buttons, not a redesigned form.

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
   tooltip attachment and dialog C++ window/model services are explicit
   authored doubles; Display is a typed endpoint, not an emulator backend.
   `UiMessageDialog.qml`, `UiDialog.qml` and their painting/theme dependencies
   remain unmodified native resources, never copied into the deliverable.
4. Compares Save pixel-for-pixel against a separate native button using the same
   live theme at identical coordinates in clean, enabled, hover and pressed
   states. The reference is outside the shell decorator's scope. Also compares
   the restart dialog pixel-for-pixel against a separate stock factory with the
   same theme/product strings; changing one painted title glyph, without
   changing the title length, must fail image equality (readable-font runs only).
   Normal offscreen tests explicitly register the repo's Inter font in the test
   process, without changing any native theme font property. Windows offscreen
   QPA need not discover desktop fonts; box glyphs are not readable-text proof.
   The separate empty-font stress process installs no font and keeps its wide-
   metric/layout/input checks; it is not substituted for the normal pixel test.
5. Physically exercises the checkbox, combos, slider, Save, Enhance and all ten
   right-aligned, fully visible Reset buttons at 750/420 px. Includes empty-font
   layout stress with no repeated reveal, click retry or property-only fallback.
6. Samples page and footer over two contrasting host colors and a live theme
   change, checks that native slider/Save materials survive decoration, and
   physically tests the long Keep-current-display recovery action at 420 px.
   Exercises visible Potato review/unsaved status, 720/540 current-size no-op
   saves and eligible-mode mapping, pre-commit stock modal close/Discard, stale
   native drafts/newer Saves with refreshed retry previews, restart refusal/
   retry/Later/acceptance and manual fallback. No global Discard, extra native
   commits or pretend game counters.
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

`shaders_native_service_test.py` isolates the nonpainting normal/shortcut
tooltip adapters, without a shader plugin or OEM resources. Bound bool/text
attached properties must survive Python collection, remain independent per
owner and service, and release all retained wrappers when owners are destroyed
across three cycles. PyQt 5's
attached-object factory otherwise lets the wrapper disappear before a later
QML binding write; this was reproduced with a tiny authored Item and one
unmodified native button, without loading the shader plugin. Explicit wrapper
retention ends at QObject destruction. This fixes a test-service lifetime bug,
not a diagnosed player renderer crash; no OEM QML/property types were changed.

The native-page opacity and Save oracle use the public asynchronous
`QQuickItem.grabToImage()` path. `QQuickWindow.grabWindow()` returns an empty
image on the independent Qt 5 software hosts; that is a capture-API limitation,
not permission to skip pixels. `shaders_qt_capture_test.py` independently checks
real changing pixels, a same-length readable-glyph change, immutable snapshots
and separate rejected nonpainting text/patch negatives without needing a shader
module or OEM resources. One scene-update boundary is
used before each capture; no capture-until-pass retry. Selected-tab contrast
compares two original sample points in **one** frame with the unchanged strict
`>6` lightness delta. Synchronization alone did not resolve the intermittent
Windows Qt 6.8 reflection failure. The generated material now explicitly sets
**both** GradientStop positions to 0 and 1: the tested Qt 5/6 releases leave an
omitted position uninitialized, despite the documented default. Public QML
allocation/destruction reproduced stale 0.73/other positions on all three local
runtimes. This matches upstream [QTBUG-140364 and its initialization fix](https://github.com/qt/qtdeclarative/commit/ed607c86a4eff425a708111a63aa2b3feb6d4e66).
`settings_panel_gradient_test.py` checks the exact generated factory after heap
reuse, 192 explicit endpoint pairs, live colors and actual painted reflection;
a poisoned endpoint must fail the same strict pixel check. The full DPI/flat
negative matrix remains unchanged and prints endpoint positions on failure.
Already-patched players require **Update rebuilt Settings panel** and a player
restart to regenerate the owned About material; updating the shader DLL alone
does not rewrite it. This is not proof that the reported in-player stretched
triangles share this cause, nor physical GPU certification. Failure annotations
retain the actual tail/values and frames for follow-up.

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
