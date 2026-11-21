# Native Settings Shaders: regular-DLL, multi-renderer work in progress

**Status: experimental test implementation, not installed-player or universal
GPU/renderer certification. Do not merge on automated fixtures alone.** The
Customizer now exposes the reviewed install/update/remove/recover flow and the
Single EXE test workflow embeds regular modules with explicit compatibility contracts.
The new [V3 public-API baseline policy and Qt evidence](EMULATOR-QT-COMPATIBILITY.md)
supersede the earlier V2-only exact-patch selection described in historical test
entries below; V2 itself remains exact-version only. The user will
verify the actual player. Start with the [user test guide](SHADERS-USER-TEST.md).

## Current user requirements

The user has revised the earlier no-DLL restriction: a **regular shader DLL is
now allowed**, but **not a `version.dll` proxy or another system-DLL replacement**.
Keep the controls in a new **Shaders** category in the player's own Settings,
with the matching default/customizable SVG. Only shader/visual-effect features
are in scope. No optimizer, FPS/vsync override, ADB/debloat, registry/power-plan
feature, overlay menu, or unrelated ZIP functionality.

The latest requirement is **vendor-neutral, renderer-aware support for Intel,
AMD and NVIDIA**, covering the player's **OpenGL, DirectX and Vulkan** choices.
The existing OpenGL prototype alone does not satisfy that requirement.

The running game's actual presentation API must agree with the player renderer
setting. Loading a graphics DLL, seeing a Qt Settings window, or saving values
is not evidence of game-frame processing. ANGLE may implement OpenGL ES using
DirectX or Vulkan underneath; that must not cause double-processing or be
mistaken for a native DirectX/Vulkan game-backend test.

"Any GPU" cannot responsibly mean an unconditional promise for every historical
GPU, feature level, driver and operating system. There must be no vendor-ID
allowlist/lock-in, but each backend must check the device features it needs.
Unsupported combinations must remain untouched and show a precise status.
Compatibility claims require recorded tests, not just a three-option menu.

## Supplied archive and licensing boundary

The separately supplied **Free Fire Potato DLL** archive has now been downloaded
and statically inspected. See [the Potato DLL review](POTATO-DLL-REVIEW.md) for
verified identities, the observed 1.5/3.0 OpenGL texture-LOD behavior, compatibility
risks and Visual Studio build instructions for the existing regular module.
It contains no source/license and has not been executed or incorporated. No
texture-only Potato mode or NVIDIA Profile Inspector startup-conflict fix is
implemented. The separate **Render resolution (optional)** control is a Display
convenience, **not completion or a visual match of the requested Potato effect**:
see [implementation and safety](POTATO-NATIVE-RESOLUTION.md).
It lowers the real native Display configuration (Save + restart), not texture
LOD, and is not proof of a live game-resolution/FPS improvement. The archive
report distinguishes observed binary behavior from the still-unreproduced
failure and prerequisites for separate texture-binding work. The
independent [texture-quality portability experiment](TEXTURE-QUALITY-PORTABILITY.md)
now provides a guarded shared sampler policy, typed API descriptor adapters and
owned-resource tests. It is **not wired to game resources or enabled as a preset**;
that document separates API evidence from missing player/performance qualification.

Reference upload: [`zlag-dll-source.zip`](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/blob/main/zlag-dll-source.zip).
Inspected Git blob: `7d5f717ba1f76d2c41bc9244a24cd83636064f16`
(935,703 bytes, 89 archive entries). Selected text was inspected only; no archive
binaries or scripts were executed or incorporated.

- Its `version.dll` proxy hooks `wglSwapBuffers` / `eglSwapBuffers` and processes
  the backbuffer on the emulator's render thread. It is not a multi-API backend.
- Its README declares GPLv3 for original code and attributes shader sources to
  recovered ENIGMA work. This MIT project has not copied that implementation or
  assumed that its shader provenance is cleared.
- The experimental GLSL and control math under `src/shaders` are independently
  authored. Official MinHook x64 sources are separately vendored from pinned
  upstream v1.3.4 under their BSD-2-Clause license, not taken from the upload.

## Why a native renderer bridge is necessary

Four pinned read-only player references were inspected as data, not launched or
modified. In the two latest MSI and BlueStacks EXEs, registered `plr.qml` defines
`iGlContainerComponent` as a black placeholder `Rectangle`, not a guest-frame
texture provider. The existing `UiDropShadowEffect.qml` processes a QML shadow,
not the game's image. A QML `ShaderEffect` or sliders alone cannot supply the
missing game frames.

A normal Qt QML plugin is being prototyped as **`BSIShaders.dll`**, with separate
Qt 5/6 builds matching the player's Qt ABI. Qt invokes its registration outside
`DllMain`; it does not replace/forward `version.dll`, `dxgi.dll`, `opengl32.dll`
or `vulkan-1.dll`. The existing arbitrary startup-DLL feature remains separate.

## Implemented experimental path, not player certification

- Original 24 px `currentColor`, no-fill, 1.7 px rounded-outline Shaders SVG,
  embedded in the Customizer's default/custom-icon catalog. A derived PNG is
  available for the experimental plugin's runtime icon.
- Standalone experimental Qt plugin, native C++ settings bridge, bounded shader
  values, atomic per-instance persistence, and a Shaders QML page with
  presets, individual/visual reset, Save/Discard and renderer diagnostics.
- Actual stock controls and live full `UiTheme`, imported from the player's
  registered `qrc:/` resources. There are no bundled `Shader*` control lookalikes.
  Save is the same native medium/accent `UiButton` (including native state
  materials, typography, translation context, clean opacity and stationary
  footer). See the [native-UI analysis and tests](SHADERS-NATIVE-UI.md).
- Independent **Render resolution (optional)**: typed public Display
  calls, aspect-preserving 720/540-short-edge plans, original/target snapshots,
  native commit/read-back, external-change refusal/explicit Keep and bounded
  interrupted-save recovery. Presets/Reset visuals leave the native mode alone.
  This is actual native configuration, not a final-frame effect or texture LOD;
  restart/game-resolution/performance validation remains required.
- Independent OpenGL/GLES, DirectX 11, DirectX 12 (Microsoft D3D11On12 on the
  actual captured D3D12 queue), and native Vulkan processing pipelines, sharing
  original effect math: saturation, contrast, warmth, shadow lift, tonal pop,
  ambient light, sharpening, spatial edge smoothing, bloom and vignette.
  Tonal pop is not HDR display output; edge smoothing is not advertised as an
  exact implementation of the upload's FXAA algorithm.
- Experimental WGL/EGL, DXGI factory/Present/Present1/resize/release/color-space,
  and Vulkan loader/getProc/device/surface/swapchain/acquire/present hooks.
  Up to four complete export-discovered Vulkan providers have independent
  originals/TLS routes and handle registries, including late-loaded differently
  named providers. Normal created queues are captured before an explicit getter.
  Surface/window ownership, native setting and capability checks gate effects.
  **Not certified in a running player or on physical GPU hardware.** A DLL
  hooking some graphics entry points alone is not proof of game integration.
- Vendor-neutral renderer-family routing. The player setting (`gl`, `dx`,
  `vlcn`) gates the observed API; a loaded OpenGL library cannot substitute for
  DirectX/Vulkan. An automatic route binds to one observed game-API family so
  an ANGLE translation layer cannot grade the same image again.
- Explicit per-device success gates: an implemented adapter is not enough to
  report active processing. The actual pipeline must confirm device/image
  requirements and successful submission before any processed-frame counter
  increments. Unidentified, unsupported/protected and unsafe presentation
  combinations remain untouched with a diagnostic.
- Vulkan uses its own commands, descriptors, copies, image barriers, render pass
  and SPIR-V generated from the shared original source. Original binary waits
  are consumed once, with per-acquired-image completion semaphores. Unobserved
  acquisitions, non-graphics/protected presentation queues, ambiguous exclusive
  queue-family ownership and unhandled presentation extensions are bypassed.
- DirectX isolates/restores the native immediate-context state; DirectX 12
  wraps/releases backbuffers on its captured queue and restores PRESENT. It
  never guesses a queue from a different device or uses the Settings renderer.
  Compiler/On12 DLL lifetimes include all objects with DLL-owned COM vtables.

- Read-only native source/image planners now discover the model actually passed
  to Settings, append a thirteenth native row without repurposing Graphics/About,
  and prepare an optional, caught regular-plugin bootstrap in `iAppwin`'s startup
  handler. Missing modules leave native startup running and the shader row/page
  inactive. There is no mandatory `import BSIShaders` in the player root.
- Shader drafts now participate in the native navigation/close No/Discard
  prompts. The source overlay can be removed exactly, including original LF or
  CRLF bytes. Ambiguous roots, nested decoys, unknown fragments, dynamic rows,
  edited owned fragments and unsupported handler shapes are refused.
- Settings design Update preserves a validated shader overlay and its stable
  default/custom/hidden icon identity. Design Restore is blocked until that
  dependent overlay is removed. A synthetic registered-PE regression exercises
  Update, idempotence, shader removal and exact original Settings-source Restore.

**There is still no verified working installation in a real player.** The test
implementation now includes:

- **PATCHER → Player interface → Shaders** in the native Customizer, with explicit
  read-only inspection, closed-player/capacity/dependency checks, displayed
  SHA-256 identities, fresh risk acknowledgment, install/update/idempotent
  reapply, shader-only removal and reviewed interrupted-transaction recovery.
  No entry/navigation action installs or inspects automatically. Target,
  resource/workspace changes invalidate the review and its open confirmation.
- A data-only compiled resource bundle of pinned Windows x64 release MSVC builds.
  New V3 source uses **Qt 5.15.2 / 6.2.4 public-API baselines** and verifies newer
  same-major target runtimes through full provider/symbol checks. Earlier V2
  5.15.2 / 6.8.3 modules remain exact-version only. Arbitrary user DLLs/downloads,
  compatibility overrides and player runtime replacement are not exposed.
- EXE/module/runtime review fingerprints, including invalidation after DLL-only
  updates, so unchanged EXE bytes cannot reuse a stale install/remove review.
  Removal remains available with an absent/mismatched application payload when
  the current owned native fragments can safely be removed.
- The Single EXE workflow independently builds/tests both regular modules before
  embedding them, and requires the packed/extracted app to decode and verify the
  embedded descriptors/hashes on a clean PATH. It includes the native installer
  component and mandatory third-party notices. A delivered app needs no build
  tool/interpreter or recurring launcher for the player.
- A processed-frame/game-surface/active status readout in the native shader page
  for user verification. A counter is a successful-submission diagnostic, not
  proof that the expected game image visibly changed.

The recognized redesigned top-tab native shell is a prerequisite; the installer
links to its separate Settings-panel preparation UI rather than guessing an
unknown player's shell. Existing player installations change only after the
explicit authorized operation. See [package/transaction safety and limits](SHADER-PACKAGE-TRANSACTIONS.md)
and the [real-player verification checklist](SHADERS-USER-TEST.md).

### Current rendering mitigation and capture evidence

The **v21** Settings design uses one flat, opaque live native-theme background
across the entire frame/page/footer. The user confirmed opacity was restored,
but the screenshot's brighter outer gradient still mismatched the dark native
page. That window-wide gradient/rim is now removed: **only the selected tab**
has a reflection; hover is flat. Every page Loader stays a hard boundary and
native backgrounds, slider handles, button gradients and global Overlay popups
are untouched. Shaders imports actual player-QRC controls, not flat owned
lookalikes. The legacy window remains **2434/2468 bytes** (34-byte reserve).
Passing Qt fixtures is **not physical-GPU or installed-player shader acceptance**.
See [native-UI analysis and exact tests](SHADERS-NATIVE-UI.md).

Vulkan diagnostics now separate requested/observed game API, processed frames,
loader/instance/device/Win32-surface/swapchain/present lifecycle evidence and
precise queue/HWND exclusion reasons. **Copy rendering status** is user-triggered
clipboard output only. Worker scans cannot erase a newer active game result.
Do not infer game ownership or shader success from lifecycle counters.
The reported device-without-surface run does not establish that Free Fire lacks
Vulkan support or that its final host presentation is Vulkan. See the
[source-backed renderer-layer analysis](FREE-FIRE-RENDERER-ANALYSIS.md); there is
no automatic GL/Qt fallback or claimed game-frame fix.

At `250e98f`, [shader CI 44860490109](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44860490109)
passes all six jobs, including Windows Qt 5.15.2 / 6.2.4 bridges, two distinct
provider DLL doubles through actual MinHook, native Qt UI contracts, 35-phone
recovery on four pinned images, strict glass pixels/negative controls, D3D WARP
and separate Linux native Vulkan/GL software pixels/sanitizers. The first loader
fixture failed because MSVC ICF folded identical authored destruction stubs;
the corrected fixture gives each export a distinct observable body and asserts
unique dispatch addresses before capture. No production safety assertion was
removed. These runs **predate the native-resolution Potato extension** and do
not verify the affected player's “Not detected” condition or physical GPUs.

### Fixed-resource capacity now passes read-only planning on four references

The earlier v1 addition correctly refused all four roots rather than growing
the EXE. Compact v2 retains the native row, optional startup, dirty/close and
missing-module behavior while removing a redundant bootstrap reference/function
and repeated per-expression comments. The window's reserved, versioned
`bsiShaderDirty2` property identifies its format; removal still requires every
exact owned fragment/count. Edited/duplicated fragments and unknown identifiers
are refused. The prior v1 planning format remains exactly removable; it is not
silently migrated or treated as a deployed installation.

An in-process, pinned Apache-2.0 **Zopfli 1.0.3** encoder is now used only when
ordinary Qt level-9 zlib exceeds a slot, only up to **256 KiB input**, with
**15 fixed iterations**. The result must inflate through Qt to the **exact source
bytes**, including BOM/CRLF. The ordinary fitting fast path is byte-unchanged;
no extra DLL, interpreter or recipient setup is introduced. Source provenance,
unmodified-file hashes and mandatory binary/portable license notices are tested.
All encoder allocations/exit paths now run through a C-only, thread-isolated
64 MiB guard: failure cleans up and retains ordinary compression, after which a
nonfitting plan is refused. No longjmp crosses C++/Qt frames. Two upstream
unaligned/aliasing loads exposed by strict UBSan are fixed in a separately
input/output-hash-verified generated source copy using `memcpy`; the 23 original
upstream hashes remain intact, and sanitizer alignment checks are not disabled.

The actual production builder/planner/writer now passes add, byte-idempotent
repeat and exact decoded-resource removal **in memory** on all four pinned EXEs:

| Reference | Packed root / slot | Packed window / slot | Native categories with Shaders |
| --- | ---: | ---: | ---: |
| MSI latest package | 20,045 / 20,619 B | 2,675 / 2,701 B | 13 |
| MSI v5.9 | 6,205 / 6,219 B | 2,460 / 2,468 B | 12 |
| BlueStacks latest package | 24,928 / 25,720 B | 2,675 / 2,701 B | 13 |
| BlueStacks v5.10 | 8,982 / 9,159 B | 2,460 / 2,468 B | 12 |

Every rewritten source stayed in its **original mapped record**, with no EXE
size change, new section, relocated record, overwritten neighboring resource or
cache storage reclaimed as a cave. All original registered resource bytes match
after removal, and disk hashes of all four reference EXEs remain unchanged.
Roots retain their original BOM; these window resources have none. Legacy builds
have eleven original native pages, not the twelve in the newer/contract fixture.

This resolves the measured **source-capacity blocker**, not installation or game
rendering. Margins are small (8 B in the legacy window); an edited/other build
that does not fit must still be refused atomically. Dense no-space fixtures
continue to fail without output, writes or a false installed status. No reference
was executed or written. Current-head Windows/full-app checks remain required.

## Validation achieved so far

- Production C++ GLES pipeline compiled and executed against an actual offscreen
  ANGLE/SwiftShader EGL context. All ten effects change real framebuffer pixels;
  neutral/disabled frames are byte-identical, color grading matches reference
  math, alpha survives, host GL state is restored, pre-existing GL errors are
  not consumed, and invalid values/sizes are refused.
- **The EGL test's software driver itself uses Vulkan. That test does not
  validate the native Vulkan adapter.** A separate actual native Vulkan test
  now executes `VulkanPipeline` on SwiftShader without EGL/GL, including all
  ten pixel effects, neutral byte identity, reference math, alpha, semaphore
  handoff/reuse, invalid-image/value rejection and resource recreation. It
  also passes ASan/UBSan locally. This still is not a Win32 WSI or hardware test.
- Actual Qt 6 C++ plugin/component build with warnings treated as errors, and
  actual plugin import in a Qt fixture window: real pointer Enable/slider/
  Enhance/Save and all ten full-caption Reset actions at 750/420 px, clean Save
  opacity, stationary footer, preset/reset semantics, bounded native values
  and atomic test-mode persistence. The separate native-shell contract test
  physically exercises No/Discard on tab changes and close with the generated
  About material, preserved native indexes and Shaders visually before About.
  Neither fixture is an installed HD-Player or a physical GPU test.
- Sanitized C++ capability/routing tests, including unvalidated-device rejection,
  native-setting isolation, no ANGLE double-processing and honest counters.
- Shader CI [44644329744](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44644329744)
  passes Windows Qt 5 and Qt 6 plugin controls, D3D11/D3D12 WARP pixels, the
  Win32 DXGI fixture and native Settings navigation/No/Discard/close/missing-DLL
  contracts, plus Linux Vulkan/GL checks, at `bb8e363`. Qt 5's actual physical
  test failure was a target clipped by the focused ScrollView: tests now reveal
  it through the real Flickable, check clip bounds and make one pointer click,
  never retry toggles or bypass assertions. Regular CI, portable and Chromium
  checks also pass at that head. **These runs predate compact v2/tighter packing**;
  current-head checks are still required. Cross-compilation is not execution.
- At `55571af`, compact v2 plus the package/recovery/guard suite passed all nine
  local CTests and all nine ASan/UBSan tests with leak detection and halt-on-error
  (211.65 seconds in the final local strict run). The bundle extension adds two
  further CTests; every changed-head suite must be rerun. The actual Qt 6 plugin/native shell and separate missing-module process,
  exact prior-v1 removal, production Settings Update/Restore lifecycle and all
  four real-reference source-capacity cycles pass again with the guarded encoder.
  That final strict ASan/UBSan rerun was 9/9. Older exit-zero
  runs with recoverable UBSan diagnostics are not clean evidence; the new job enables `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, keeps leak
  detection and instruments the C encoder/guard as well as C++.
- The bundled-installer extension passes **11/11 local CTests (31.82 s)** and
  **11/11 ASan/UBSan CTests (244.39 s)** with leak detection and halt-on-error.
  The sanitizer run uses the explicit native SwiftShader loader; an earlier
  invocation without that mandatory loader failed and is not counted as a pass.
  The complete Customizer `Main.qml` is physically exercised at 1440×930 and
  980×640 for inspection, cancel/fresh acknowledgment, installation, removal,
  recovery, version refusal, busy/stale guards and the preparation link, using
  mock services without player files. Native navigation now starts by dragging
  the actual top strip to Shaders, not programmatically selecting its position.
  Separate Qt 5 and Qt 6 missing-module processes pass. These are local fixture
  results; changed-head Windows compilation/embedding/portable checks and real
  player/GPU verification are separate gates, not implied successes.
- Strict read-only Qt 5/6 package fixtures check the actual data export, exact
  release fixed-file/product versions, native Qt entry points and normal/delay/
  transitive/forwarded dependency symbols. The descriptor is a contract, not a
  digital signature or proof of compatibility with every custom Qt build.
- Disposable registered-PE file tests pass complete installation, byte-idempotent
  repeat, module update with no EXE churn and source removal preserving later
  unrelated customizations. They cover 26 ordinary-failure/interruption cycles,
  plus interrupted recovery, rollback failure, locks, symlinks, unowned/extra
  files and changed target/snapshot/journal refusal. No real player was installed.
- Every one of 4,288 allocation points in a small encoder fixture is faulted,
  plus fatal-exit and input/output/memory-limit failures, zero outstanding
  allocations, subsequent success and concurrent calls. The production C++
  call site's fallback is tested with a separate failure-only linker stub.
- At `7b56821`, shader workflow
  [44667528162](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44667528162)
  passes both Windows Qt versions and Linux graphics, and Single EXE packaging
  passes. These predate the package/recovery/guard code; current-head CI must
  separately pass actual compiled Windows module/runtime validation and all
  full-app/reference/portable/Chromium checks.
- New source/image tests pass locally with all their dependencies rebuilt and
  `-Wall -Wextra -Werror`: real registered PE resources/imports/call-site data,
  bounded relocation, unchanged EXE size/import modules/unrelated resources,
  exact decoded resource restoration, BOM, idempotence, unsupported Qt/x86,
  missing resources, orphan/partial integration and dense no-space refusal.
  This is byte planning, **not** an EXE+DLL package transaction or exact binary
  layout/timestamp restoration.
- A Qt 6.8.3 native-contract fixture physically opens the complete generated
  780×500 native shell, navigates to the appended Shaders row, loads the actual
  plugin page, exercises native No/Discard on tab changes and close, and checks
  Save/reopen/reset/persistence. A separate fresh process tests missing-module
  startup, inactive row/page, native callback loops and Settings close. Native
  proprietary models/backend/dialog controls are fixtures, not an installed
  player. Windows Qt 5/6 CI now explicitly includes these tests.
- Current shader CI separately requires native D3D11 and D3D12 WARP pixels, a
  real Win32 DXGI presentation/resize fixture, Qt 5/6 plugin-control input,
  Mesa Vulkan with mandatory Khronos validation, native SwiftShader Vulkan
  under ASan/UBSan, and both desktop GL core and GLES real framebuffer tests.
  Tests fail, rather than skip-pass, when their required APIs are unavailable.
- Existing icon/style and complete Settings/About resource budgets still pass.

WGL/EGL and Win32 Vulkan presentation, actual-player Qt package compatibility
and deployment recovery, real-player navigation/game frames and the physical
GPU matrix have not passed their required checks. Package/recovery fixtures
and source-capacity success above do not resolve those installed-player gates. Historical regular-DLL loading,
Qt fixture navigation, prior PR CI and software pixels do not certify a working
installation of this feature.

## Mandatory backend and hardware matrix

| Native game API | Current adapter | Intel | AMD | NVIDIA |
| --- | --- | --- | --- | --- |
| OpenGL / GLES | Pipeline prototype; Windows integration unverified | Pending | Pending | Pending |
| DirectX 11 | WARP pixels + DXGI fixture pass; game presentation unverified | Pending | Pending | Pending |
| DirectX 12 | Actual-queue On12 WARP pixels pass; game presentation unverified | Pending | Pending | Pending |
| Vulkan | Native pipeline pixel tests pass locally; Win32 WSI unverified | Pending | Pending | Pending |

For DirectX, identify the API actually used by each supported player build;
D3D11 is not evidence of D3D12 or vice versa. Do not silently label an unknown
DirectX version supported. Automatic software tests should separately exercise
OpenGL/GLES, Direct3D WARP and Vulkan software rendering; hardware results must
record GPU model/vendor, driver, OS, player build, actual backend/feature level,
neutral and changed-frame evidence, and failures/limitations. Hybrid systems
must verify the GPU actually rendering the game, not merely the display GPU.

## Required before merge

1. Complete the current DirectX, Vulkan and GL game-presentation validation,
   with correct shader formats, state/layout preservation, synchronization,
   resource lifetimes and device-loss recovery. The presence of these source
   files or three renderer names is not completion. Retain vendor-neutral gates
   and document the explicit unhandled modes (including legacy D3D9/10, HDR,
   protected content, Vulkan extended presents/cross-family ownership and late
   unobserved surfaces/queues) instead of silently processing them incorrectly.
2. Preserve the fixed-resource capacity cycles and validate the experimental
   package/transaction backend against actual Windows player Qt/VC dependencies.
   Verify the new matching-Qt payload bundle and Customizer preflight/install/
   update/removal/recovery UI in the delivered Windows build and actual player;
   the experimental UI is not production certification. Preserve fixed slots, verified rollback, prior
   title/menu/gutter fixes, exact original sources and unrelated pages. Verify
   update/removal recovery, the actual QML import path and missing/incompatible
   package fallback in a player; disposable/Qt fixtures alone are not sufficient.
3. Physically navigate to Shaders in a running supported Windows emulator.
   Verify **game-image pixels** on all three renderer families, not just Qt UI
   bindings. Verify default/custom/hidden icon behavior, reset, Save/Discard,
   persistence/reopen, per-instance isolation, renderer switching, resize,
   fullscreen, multi-monitor/hybrid GPUs, restart and device loss.
4. Complete the compatibility matrix above and current-head full application,
   reference, portable and Chromium CI. Unsupported modes must show a clear
   diagnostic and leave game rendering intact.

**No merge has been performed.** The user's merge approval remains conditional
on the requested multi-renderer feature actually functioning and being tested.
