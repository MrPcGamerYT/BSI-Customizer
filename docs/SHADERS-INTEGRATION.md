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

There is no production installer, no root/tab-model resource patch yet, and no
completed native Shaders category to navigate to in a patched player. Existing
twelve-tab players and their original eleven default drawings remain unchanged.
The prototype is not linked into the main Customizer build or release payload.

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
- All Windows pipeline/hook sources and WARP/presentation fixtures cross-compile
  against real Windows headers. Initial Windows Qt 5/6 plugin builds succeeded,
  but execution tests found real compiler/lifetime issues; those must pass the
  current CI rerun, not be excused by successful compilation.
- Current shader CI separately requires native D3D11 and D3D12 WARP pixels, a
  real Win32 DXGI presentation/resize fixture, Qt 5/6 plugin-control input,
  Mesa Vulkan with mandatory Khronos validation, native SwiftShader Vulkan
  under ASan/UBSan, and both desktop GL core and GLES real framebuffer tests.
  Tests fail, rather than skip-pass, when their required APIs are unavailable.
- Existing icon/style and complete Settings/About resource budgets still pass.

Windows execution/presentation checks are still being debugged. WGL/EGL and
Win32 Vulkan presentation, the native installer and a real player have not yet
passed their required checks. Historical regular-DLL loading results, earlier
PR CI runs and a software backend's success do not certify this implementation.

## Mandatory backend and hardware matrix

| Native game API | Current adapter | Intel | AMD | NVIDIA |
| --- | --- | --- | --- | --- |
| OpenGL / GLES | Pipeline prototype; Windows integration unverified | Pending | Pending | Pending |
| DirectX 11 | Native pipeline + DXGI hooks; Windows execution checks in progress | Pending | Pending | Pending |
| DirectX 12 | Actual-queue On12 pipeline + DXGI hooks; Windows checks in progress | Pending | Pending | Pending |
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
2. Finish native Settings model/boot integration and a recoverable installer for
   regular matching-Qt DLLs. Validate dependencies/architecture before changing
   the player; preserve fixed resource slots, atomic writes, rollback, prior
   title/menu/gutter fixes and unrelated pages. Do not break player startup if
   a shader package is missing or incompatible.
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
