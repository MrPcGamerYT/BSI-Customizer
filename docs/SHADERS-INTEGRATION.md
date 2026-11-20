# Native Settings Shaders: regular-DLL, multi-renderer work in progress

**Status: experimental and incomplete. Do not merge or advertise universal
GPU/renderer support. The Customizer does not yet install this feature.**

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

## Implemented preparation, not a completed feature

- Original 24 px `currentColor`, no-fill, 1.7 px rounded-outline Shaders SVG,
  embedded in the Customizer's default/custom-icon catalog. A derived PNG is
  available for the experimental plugin's runtime icon.
- Standalone experimental Qt plugin, native C++ settings bridge, bounded shader
  values, atomic per-instance persistence, and a shader-only QML page with
  presets, individual/all reset, Save/Discard and renderer diagnostics.
- Independent OpenGL/GLES, DirectX 11, DirectX 12 (Microsoft D3D11On12 on the
  actual captured D3D12 queue), and native Vulkan processing pipelines, sharing
  original effect math: saturation, contrast, warmth, shadow lift, tonal pop,
  ambient light, sharpening, spatial edge smoothing, bloom and vignette.
  Tonal pop is not HDR display output; edge smoothing is not advertised as an
  exact implementation of the upload's FXAA algorithm.
- Experimental WGL/EGL, DXGI factory/Present/Present1/resize/release/color-space,
  and Vulkan loader/getProc/device/surface/swapchain/acquire/present hooks.
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

**There is still no production shader-package installer or working installation
in a player.** These planners are compiled into the Customizer for tests, but
are not exposed as an install action or bundled with a plugin release payload.
Existing twelve-tab player installations remain unchanged.

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
  actual plugin import in a Qt fixture window: real pointer Enable/slider/Save/
  Discard input, preset/reset semantics, bounded native values and atomic
  test-mode persistence. The fixture is not HD-Player category navigation.
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
- Compact v2 currently passes all five local CTests in strict C++ builds and
  ASan/UBSan (including the vendored C encoder), actual Qt 6 plugin/native shell
  and missing-module tests, exact prior-v1 removal, the production Settings
  Update/Restore lifecycle and all four real-reference source-capacity cycles.
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

WGL/EGL and Win32 Vulkan presentation, package ABI/dependency ownership checks,
recoverable EXE+module deployment, real-player navigation/game frames and the
physical GPU matrix have not passed their required checks. Source-capacity
success above does not resolve those gates. Historical regular-DLL loading,
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
2. Preserve the now-passing fixed-resource capacity cycles and complete package
   ownership/ABI/dependency validation and a recoverable installer for regular
   matching-Qt DLLs. The existing in-memory plans are not
   a two-part EXE+module transaction. Preserve fixed slots, verified rollback,
   prior title/menu/gutter fixes, exact original sources and unrelated pages.
   Verify update/removal recovery and missing/incompatible-package fallback in
   an actual player; the missing-module fixture alone is not sufficient.
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
