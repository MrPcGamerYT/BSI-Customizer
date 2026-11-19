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
- Independent OpenGL/GLES processing pipeline: saturation, contrast, warmth,
  shadow lift, tonal pop, ambient light, sharpening, spatial edge smoothing,
  bloom and vignette. Tonal pop is not HDR display output; edge smoothing is
  not advertised as an exact implementation of the upload's FXAA algorithm.
- Preliminary Windows WGL/EGL presentation hooks, restricted to identified game
  child surfaces rather than the Qt Settings frame. **Not certified in a running
  player or on physical GPU hardware.**
- Vendor-neutral renderer-family routing. The player setting (`gl`, `dx`,
  `vlcn`) gates the observed API; a loaded OpenGL library cannot substitute for
  DirectX/Vulkan. An automatic route binds to one observed game-API family so
  an ANGLE translation layer cannot grade the same image again.
- Explicit capability gates: DirectX 11/12 and Vulkan adapters are currently
  **not implemented** and cannot report active processing or processed frames.

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
- **The EGL test's software driver itself uses Vulkan. This does not validate a
  native Vulkan game adapter.** It also does not certify Intel/AMD/NVIDIA GPUs.
- Actual Qt 6 C++ plugin/component build with warnings treated as errors, and
  actual plugin import in a Qt fixture window: real pointer Enable/slider/Save/
  Discard input, preset/reset semantics, bounded native values and atomic
  test-mode persistence. The fixture is not HD-Player category navigation.
- Sanitized C++ capability/routing tests, including missing-adapter rejection,
  native-setting isolation, no ANGLE double-processing and honest counters.
- Existing icon/style and complete Settings/About resource budgets still pass.

The regular Windows plugin, Qt 5 build, WGL presentation hooks, native installer
and real player have not yet passed their required checks. Historical regular-
DLL loading results and earlier PR CI runs do not certify this implementation.

## Mandatory backend and hardware matrix

| Native game API | Current adapter | Intel | AMD | NVIDIA |
| --- | --- | --- | --- | --- |
| OpenGL / GLES | Pipeline prototype; Windows integration unverified | Pending | Pending | Pending |
| DirectX (record actual D3D version/feature level) | Not implemented | Pending | Pending | Pending |
| Vulkan | Not implemented | Pending | Pending | Pending |

For DirectX, identify the API actually used by each supported player build;
D3D11 is not evidence of D3D12 or vice versa. Do not silently label an unknown
DirectX version supported. Automatic software tests should separately exercise
OpenGL/GLES, Direct3D WARP and Vulkan software rendering; hardware results must
record GPU model/vendor, driver, OS, player build, actual backend/feature level,
neutral and changed-frame evidence, and failures/limitations. Hybrid systems
must verify the GPU actually rendering the game, not merely the display GPU.

## Required before merge

1. Finish and test the DirectX and Vulkan game-presentation adapters, with
   correct shader formats, state/layout preservation, synchronization, resource
   lifetimes and device-loss recovery. Retain the vendor-neutral routing gates.
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
