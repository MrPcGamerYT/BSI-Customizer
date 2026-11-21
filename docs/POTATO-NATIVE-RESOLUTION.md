# Optional render resolution in Shaders — not the requested texture Potato

**Experimental Display convenience; not completion of the video-like Potato
request, and no installed-player, FPS or universal GPU claim.** The native
**Settings → Shaders** page labels this separate option **Render resolution
(optional)**. Resolution reduction is not a substitute for texture detail.
The independent default-off **Potato graphic** experiment now arms private
GL 3.3/GLES 3.1 draw-time samplers; its exact boundaries and remaining resource/
player qualification are in [the texture guide](TEXTURE-QUALITY-PORTABILITY.md).
The old internal `potatoMode`/`previewPotato` names remain for resolution-profile
and recovery/API compatibility; they do not identify the texture switch.

## What this separate option does and does not do

The [static DLL review](POTATO-DLL-REVIEW.md) found OpenGL draw/bind-time texture
LOD overrides of 1.5 / 3.0. A final-frame shader cannot reproduce those overrides:
by presentation time, the game has already drawn geometry and sampled textures.
Blurring or grading that finished image would add work, not remove that work.

A safe portable texture-only implementation also needs proven game-color
resource/view ownership, valid existing mipmaps, private replacement samplers,
qualified bindings and lifetimes, API-specific synchronization and review of
external driver overrides. D3D12 static/root and Vulkan immutable samplers are
not mutable GL texture parameters. DirectX/Vulkan game texture-binding paths
are **not implemented**. The independent GL draw-time experiment is no longer
policy-only, but owned software-context pixels are not proof of real game
resource semantics or performance. No supplied binary/source was copied, no
NVIDIA profile is reset, and the reported profile conflict is not fixed.

Separately, this option requests the player's **own native framebuffer size**,
through the same public config calls used by Display. It does not implement or
match the appearance of the video’s texture-detail effect.
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
Newly generated custom targets must keep **both** dimensions in the native
editor's 540–3840 range. Do not force unsupported 360p/270p sizes. The menu now shows only
eligible profiles. A profile above the **original** short edge is unavailable;
selecting a reduction never silently upscales a smaller original. Extreme aspect
ratios that do not fit are also unavailable, with the reason shown beside the
selector. The actual configured dimensions and proposed target are displayed.

When the proposed target **exactly matches the currently configured Display**,
the choice is marked **(current; no resize)** and Save succeeds as an explicitly
reported **no-op**: no Display write, native commit or additional restart, and no claim
of fewer pixels or better performance. Both original dimensions (even an
existing odd dimension) and the custom/predefined flag remain exact. Independent
visual/texture edits can Save at the same time. Switching from Strong back to a
720 px captured original is **not** a no-op if Display is currently 540 px: it
explicitly restores that original with Save/restart, never beyond the original.
If a draft becomes ineligible after an external edit, **Keep saved resolution**
drops only that unsaved selection, preserving the other shader drafts. This is
different from the existing **Keep current display** recovery action.

In the user's 960×540 screenshot, 540 already equals both current and captured
original. No further reduction is supported and 720 would upscale it; this is
not evidence that a native write/restart failed. The page now repeats that
minimum-size limitation beside the selector. C++ preview/status dimensions and
arrows use explicit UTF-16 `QChar` code units, not narrow Unicode escapes (which
MSVC can still encode through its execution code page before Qt widens them).
The reduction, restore and already-current previews all assert the actual Unicode
symbols. Native size/restart behavior is unchanged.

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
- For a changed size, **Save changes opens the player's actual stock
  `qrc:/UiMessageDialog.qml`**, using its `UiDialogWindow.Simple`, native
  `DialogButtonModel` / `DialogButtonModelList`, live theme, localized product
  restart title/description, accent **Restart Now** and secondary **Discard
  Changes**—the same component contract as native Graphics/Display.
  Opening it writes nothing. Closing it keeps the draft; **Discard Changes**
  discards only this shader page's draft, never unrelated native drafts.
- **Restart Now** first submits the reviewed native size through the durable
  journal, commit and exact read-back, finalizes preferences, then requests the
  supported native restart. Only acceptance invokes Settings' restart loader
  and hides the Settings window. The stationary footer/approved shell remain.
- A native restart refusal is not a failed config Save: the exact saved size
  remains configured, Settings stays visible, and a retry/manual notice remains.
  **Restart player…** reopens the same stock dialog with **Later**, not a fake
  Discard of changes already saved. Later does not undo or recommit anything.
  If the public restart method is unavailable, the stock shell has an honest
  manual-restart notice and **Save changes / Discard Changes** before commit.
- An exact-current-size choice saves without a restart dialog or Display write.
  An accepted restart call is not proof of a completed restart, changed guest
  dimensions or a measured running-game result. No live resize is attempted.
- Confirmation is bound to the preview's exact source/target dimensions and
  custom flags plus the saved shader configuration by a C++ concurrency stamp.
  `applyReviewed()` rechecks it under the preference writer lock **before any
  native write**, independently of QML's polling timer. A late external edit,
  newer shader Save or replay cannot silently replan a different size; even a
  reviewed no-op cannot become an unconfirmed native write. This stamp is not
  an authorization token, signature or hardware attestation.
- Restart is allowed only for a finalized change from this session, with an
  unchanged exact target, a live GUI-thread backend and no native unsaved draft
  or pending recovery. Shaders never kills a process, guesses a CLI, commits
  unrelated settings or restarts just because the page was opened.
- **Enable visual effects**, Enhance/presets and **Reset visuals** affect only
  the original ten visual effects. They neither disable nor enable the resolution option.
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
or cross-thread objects make the resolution option unavailable, not an active-looking no-op.
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
Released `bsi.shaders.v1` visual and `bsi.shaders.v2` resolution settings are
read without changes on open and migrate to **v3** only on Save. V1 starts with
Native resolution and texture off; v2 keeps its exact original/target recovery
metadata and starts with texture off. V3 contains ten visual values + enabled,
independent resolution mode, texture enable/review flags and bounded display
metadata. The display journal accepts released v2 and current v3 next records.
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
- actual temporary-file v1/v2 migration and v3 persistence, conflict/Keep semantics,
  independent visual saves, unrelated-draft refusal and no global Discard;
- native setter/commit refusal, partial-stage rollback, interrupted saves,
  real final preference I/O failure after native commit and exact-side recovery;
- competing writer, external preference change, invalid metadata and destroyed /
  wrong-thread backend rejection; exact restart acceptance/refusal, stale-target,
  missing/destroyed restart method and unrelated-native-draft guards;
- actual C++ plugin import and physical QML mode-popup/Save/Discard/restoration,
  with visual effects off; current-size 720/540 combined saves, unavailable-mode
  filtering/index mapping, an open popup surviving status polling, and dropping
  only a stale resolution draft while retaining visual edits;
- stock restart-dialog resource/model/theme/transient/modal identity and exact
  pixels against a separate native factory, with a changed-painted-title
  negative control; pre-commit Discard/close, stale-native-draft refusal,
  restart refusal/retry/Later/acceptance/manual fallback, accepted-only native
  loader, no extra commits and no global discard; plus complete native-shell
  No/Discard/close callbacks, stationary Save and preset/reset independence;
- every visual Reset at 750/420 px with full-control clipping checks, form/row
  width assertions and a single reveal followed by actual pointer input. An
  isolated empty-font search also exercises wide fallback metrics: section
  headings wrap and the preset selector yields without eliding Reset buttons.
  This caught a real horizontal overflow; changing focus alone did not fix it.
  Missing-font layout stress is not a text-paint/readability certification, and
  the independent strict painted-pixel/negative-control suites remain required.

The native backend endpoints in these tests are **authored contract doubles**.
They prove wiring, values and transaction behavior, not an actual player restart,
Android/game resolution, texture LOD, a visible game-image change or frame times.
Windows/Qt-baseline and current-head package checks must pass separately.

Before calling this working in a player, record: initial Display size/custom
selection, Light/Strong Save and full restart, actual guest/game size and same
scene before/after, exact Native restoration, external Display conflict, No /
Discard, persistence, per-instance separation and the game API/GPU/driver.
Keep the GL texture experiment's qualification and missing DirectX/Vulkan
texture paths explicit; resolution is not their substitute. **Do not merge
PR121 on these config/UI fixtures alone.**
