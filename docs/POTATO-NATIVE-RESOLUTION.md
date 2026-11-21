# Potato in Shaders: actual native render size, not texture LOD

**Experimental implementation; no installed-player, FPS or universal GPU claim.**
The native **Settings → Shaders** page now contains **Potato graphics · native
render size**. This is independently authored source, not the supplied proxy DLL,
a color preset, a blur/pixelation shader, or an unconnected selector.

## Why this implementation

The [static DLL review](POTATO-DLL-REVIEW.md) found OpenGL draw/bind-time texture
LOD overrides of 1.5 / 3.0. A final-frame shader cannot reproduce those overrides:
by presentation time, the game has already drawn geometry and sampled textures.
Blurring or grading that finished image would add work, not remove that work.

A safe portable texture-only implementation also needs proven game-color
resource/view ownership, valid existing mipmaps, private replacement samplers,
qualified bindings and lifetimes, API-specific synchronization and review of
external driver overrides. D3D12 static/root and Vulkan immutable samplers are
not mutable GL texture parameters. Those game-binding paths are **not
implemented**; the [sampler policy experiment](TEXTURE-QUALITY-PORTABILITY.md)
remains disconnected from game resources. No supplied binary/source was copied,
no NVIDIA profile is reset, and the reported profile conflict is not fixed.

The best practical implemented workload reduction here is the player's **own
native framebuffer size**, through the same public config calls used by Display.
It is independent of visual postprocessing and does not require Vulkan detection
to work as a config operation. It makes no claim to lower texture allocation,
geometry, draw distance or every game's internal resolution. The actual guest
render size and performance must be measured after restart.

## Modes

| Selection | Native Display plan from 1920×1080 | Meaning |
| --- | --- | --- |
| Native / restore original | Exact captured size and custom-selection flag | No active override, or restore this profile's unchanged target |
| Light · 720 px short edge | 1280×720 | Approximately 56% fewer configured native pixels |
| Strong · 540 px short edge | 960×540 | 75% fewer configured native pixels |

The original aspect/orientation is preserved to the nearest **two pixels**.
Targets must keep **both** dimensions in the native custom editor's 540–3840
range. Do not force unsupported 360p/270p sizes. If the original short edge is
already at/below the selected cap, Save refuses the mode rather than recording
a pretend performance change. Extreme aspect ratios that do not fit are refused.

The exact original is captured once when enabling a reduction. Strength changes
always plan from that original, not from an already reduced target. Restoration
also preserves an original custom-selection flag or supported larger predefined
resolution (e.g. 5120×2160); it is not a hardcoded 1080p reset.

Fewer configured pixels are **not** a measured FPS improvement. The game may
choose its own internal resolution, be CPU-limited or dynamically scale. A lower
native render size can also make text/UI less sharp. No FPS cap, DPI, renderer,
root, guest/game file, registry, power plan, driver setting or custom-resolution
favorites list is changed. Visual effects can add work independently.

## Save, Discard and restoration

- Selecting a mode edits the **draft only**. It participates in the existing
  native tab/close **No / Discard Changes** prompt. Neither preview, presets,
  opening the page nor Discard writes a new native resolution.
- **Save changes** is the only operation that stages the framebuffer size through
  native Display and requests its native commit. The stationary footer and
  approved Settings geometry remain. A successful config read-back is not a
  measurement of the still-running game.
- Fully close/restart the player after saving a native size. There is **no
  automatic restart** and no claim of a live resolution switch. The page retains
  a restart notice after the current process saves it.
- **Enable visual effects**, Enhance/presets and **Reset visuals** affect only
  the original ten visual effects. They neither disable nor enable Potato.
  A native reduction works with postprocessing off; a neutral shader status
  therefore does not mean the native size is unchanged.
- Choose **Native / restore original**, Save and restart to undo the reduction.
  Restoration writes only if native Display still matches the recorded target.
  A later Display edit is not overwritten by an old restore snapshot.
- In that conflict, explicitly choose **Keep current display**, then Save. This
  forgets the old override/restore metadata without writing any Display field.
  The next reduction will capture the new original. It cannot silently adopt or
  overwrite a native unsaved draft.

**Before removing/downgrading the module, restore Native if you want the original
size back.** Module removal does not undo an already committed native Display
setting. With a missing/older module, use native Display to select the desired
size and restart. Saved v2 shader preferences should be retained for recovery;
an older v1-only module cannot restore their native-size snapshot.

## Public native contract and failure safety

`NativeDisplayBinding` runs only on the application GUI thread and holds a
`QPointer`, not a private emulator/Qt pointer. It requires unambiguous public
meta-methods with the known QString/int/bool signatures:

- `confGetInstanceI32`, `confGetInstanceBool`, `confIsModified`;
- `confSetInstanceI32`, `confSetInstanceBool`, `confCommit`.

Read-only inspection of the `UiBackend` Qt metaobjects in all four hash-pinned
[reference EXEs](../tests/fixtures/emulator-qt-evidence.json) confirms these six
**public slots**, with QString/int/bool arguments and **void** setters/commit.
The string/metadata tables were paired through their static metaobject pointers;
no reference code was loaded or executed. This is signature evidence, not proof
of a running backend, disk flush, restart or game-resolution change.

Void and boolean-returning setters/commit are supported; boolean refusals are
honored. Unknown/incomplete contracts, invalid dimensions, destroyed backends
or cross-thread objects make Potato unavailable, not an active-looking no-op.
The QML page binds the existing `uiBackend` context object after construction;
no QObject is serialized through native page-model arguments.

Only `fb_width`, `fb_height`, and `custom_resolution_selected` may be written.
Any pre-existing native unsaved setting blocks a native-size save. **Shaders
never calls global `confDiscard()`**, so it cannot discard an unrelated draft.
A failed partial stage attempts to restore only its own three fields; uncertain
commits keep recovery evidence and report failure rather than activating the
requested visual parameters or pretending a restart happened.

Preferences remain per executable + native instance, in Qt's user config area:
`BSICustomizer/Shaders/<SHA256(canonical-player-path + newline + instance)>.json`.
Released `bsi.shaders.v1` visual settings are read without changes on open and
migrate to **v2** only on Save, initially with Native mode. V2 contains the ten
visual values + enabled, independent mode and bounded original/target metadata.
Unknown fields/types, nonfinite/fractional values and oversized files are refused.
A corrupt or unknown restore snapshot is never silently replaced on Save.

A per-profile `QLockFile`, link/junction rejection, changed-file comparison and
`QSaveFile` (no direct-write fallback) protect preference writes. Before the
first native write, the previous preferences plus **both** native sizes and the
next configuration are saved as a bounded `pendingDisplay` record. The final
preference write happens only after native commit/read-back reports a clean,
exact target.

On reopen, an interrupted record is reconciled only against a **clean** exact
before/target native configuration; this writes preferences, never native config.
A third size or native unsaved draft is a conflict, not permission to guess.
Use native Settings' explicit Save/close-Discard to resolve an outstanding native
draft, then reopen Shaders. Do not delete the preference file to hide the error.
Keep current display can explicitly clear a conflict once native state is clean.

Native configuration and a preference file cannot be atomically committed as
one object. These are application-level recovery checks, **not power-loss,
filesystem-durability or native-backend disk-flush certification**. No saved
JSON record is a signature or hardware attestation.

## Validation and remaining acceptance gates

Implemented automated coverage includes:

- C++ real planning and public meta-method invocation; landscape, portrait,
  square, ultrawide, native bounds and exact custom/predefined restoration;
- actual temporary-file v1 migration/v2 persistence, conflict/Keep semantics,
  independent visual saves, unrelated-draft refusal and no global Discard;
- native setter/commit refusal, partial-stage rollback, interrupted saves,
  real final preference I/O failure after native commit and exact-side recovery;
- competing writer, external preference change, invalid metadata and destroyed /
  wrong-thread backend rejection;
- actual C++ plugin import and physical QML mode-popup/Save/Discard/restoration,
  with visual effects off, plus complete native-shell No/Discard/close callbacks,
  stationary Save, preset/reset independence and clean bindings.

The native backend endpoints in these tests are **authored contract doubles**.
They prove wiring, values and transaction behavior, not an actual player restart,
Android/game resolution, texture LOD, a visible game-image change or frame times.
Windows/Qt-baseline and current-head package checks must pass separately.

Before calling this working in a player, record: initial Display size/custom
selection, Light/Strong Save and full restart, actual guest/game size and same
scene before/after, exact Native restoration, external Display conflict, No /
Discard, persistence, per-instance separation and the game API/GPU/driver.
Keep the texture-only feature explicitly unimplemented unless its separate
resource-binding and driver-profile requirements are met. **Do not merge PR121
on these config/UI fixtures alone.**
