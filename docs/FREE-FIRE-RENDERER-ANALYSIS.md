# Free Fire: configured Vulkan is not a captured game renderer

## Evidence from the reported session

The user supplied this diagnostic, not a successful shader result:

```text
Requested API: Vulkan
Observed game API: Not detected
Processed game frames: 0
Game surface observed: no; processing active: no
loaders=1; instances=1; devices=1; Win32 surfaces=0;
swapchains=0; presents=0; missing queues=0; excluded windows=0
Last hooked loader: vulkan-1.dll
Qt bridge: 5.15.2; host: 5.15.4
```

This establishes **captured Vulkan initialization**, but no captured Win32 WSI
presentation chain. It does **not** establish any of the following:

- that Free Fire's Android client is using native Vulkan;
- that Free Fire never supports Vulkan;
- that its final Windows presentation is OpenGL, EGL, DXGI or Vulkan;
- that a shader compiled, received a game image or changed a frame;
- that a window-ownership/queue rejection caused this particular zero-frame run.
  Neither rejection counter increased in this report.

The public Qt bridge is intentionally built against the older same-major API
baseline. Seeing 5.15.2 versus 5.15.4 is not by itself a compatibility failure,
and replacing Qt would neither prove game ownership nor provide a missing WSI
swapchain. Actual module/provider preflight is a separate check.

## Research findings — keep three layers separate

| Layer | What its setting/evidence actually means |
| --- | --- |
| Android game API | The API selected by this exact Free Fire or Free Fire MAX client, its build/device rules and runtime environment. Not observable from the supplied Windows device-creation counts. |
| Emulator graphics renderer | BlueStacks' configured graphics-library path. Its current documentation offers **Vulkan & OpenGL** or **OpenGL Only** on newer supported Android instances, not a guarantee that every app creates a Win32 Vulkan swapchain. [1](https://support.bluestacks.com/hc/en-us/articles/360057389932-How-to-change-the-graphics-settings-on-BlueStacks-5) |
| Windows presentation / interface | The host API and surface finally presented to a Windows HWND. BlueStacks documents **Interface renderer** separately from the game graphics renderer. Capturing Qt Settings is not capturing the game. [1](https://support.bluestacks.com/hc/en-us/articles/360057389932-How-to-change-the-graphics-settings-on-BlueStacks-5) |

BlueStacks' version-specific documentation says Nougat 32/64-bit has
OpenGL/DirectX choices; newer Android Pie/11/13 versions have different choices,
including combined Vulkan/OpenGL. The player version and Android instance are
therefore needed; Qt 5.15.4 alone does not identify either.
[2](https://support.bluestacks.com/hc/en-us/articles/360059893151-How-to-use-a-graphics-renderer-on-BlueStacks-5)

The official Free Fire recommendation page specifies CPU, memory and
performance-mode settings, **not a mandatory Vulkan renderer**. It does not
provide the exact client's runtime API. The Free Fire MAX recommendation is a
separate guide, so the two clients should not be silently treated as identical.
[3](https://support.bluestacks.com/hc/en-us/articles/360057784811-Recommended-settings-for-Free-Fire-on-BlueStacks-5)
[4](https://support.bluestacks.com/hc/en-us/articles/4410423736333-Recommended-settings-for-Free-Fire-MAX-on-BlueStacks-5)

Unity's Android documentation supports multiple graphics APIs, including Vulkan
and GLES. Engine capability is **not proof that an individual game shipped,
selected or used a particular backend**. No exact Free Fire engine build/API
configuration was extracted or established in this investigation.
[5](https://docs.unity3d.com/2022.2/Documentation/Manual/android-requirements-and-compatibility.html)

API translation also makes DLL-based inference unsafe. Android documents ANGLE
as an implementation of OpenGL ES on Vulkan: Vulkan activity can exist below a
GLES app. This is an architectural example, **not evidence that this older
BlueStacks session is using Android 15 ANGLE or that changing ANGLE fixes it**.
[6](https://developer.android.com/games/develop/vulkan/overview)

## What the current hook implementation can and cannot say

The in-repository code was inspected, not just the label in Settings:

- `ShaderBridge::attach()` maps the native `gl` / `dx` / `vlcn` configuration
  into a **requested family**, not a measurement of the running Android app.
- `VulkanHooks.cpp` tracks Vulkan loader/instance/device lifetimes, captured
  `vkCreateWin32SurfaceKHR` HWNDs, swapchains and presentation queues.
- `Runtime::eligible()` requires an identified same-process player **child**
  window, excludes the Qt/Chrome UI, and requires sufficient client bounds.
- `Runtime::accepts()` refuses a different family from the configured one.
  Thus “Not detected” while requesting Vulkan does **not** rule out an
  otherwise-present GL/EGL or DXGI path. It also does not prove one exists.
- No Android game-process API identification, offscreen Vulkan image ownership,
  shared-image interop chain or external-process presentation adoption is
  implemented by these counters. The shader must not touch such images on a guess.

Plausible explanations still to distinguish are **a different game/presentation
API**, **offscreen/interop presentation**, and **a missed surface/provider/process
lifecycle**. These are hypotheses, not diagnoses. This investigation does not
remove the Vulkan request gate, substitute the Qt/interface surface, force a
renderer change or count another API as successful Vulkan processing.

## Smallest useful next evidence

1. Record exact MSI/BlueStacks version, Android instance (Nougat/Pie/11/13),
   Free Fire versus MAX and app version, and both native Graphics and Interface
   renderer settings. Do not infer them from the bridge version.
2. After a normal full player restart, launch the same game/scene and copy
   rendering status again. No game files, startup arguments, global profiles,
   protection settings or DLL proxies need changing.
3. If ADB was **already enabled** and the user elects to inspect it, use the
   installed emulator's ADB to read the game's own graphics-initialization log.
   Select the actual connected instance from `devices`, get the PID of the
   actual package (`com.dts.freefireth` or `com.dts.freefiremax`, only after
   confirming the installed client), and filter a read-only `logcat -d --pid=...`
   for its initialization/API lines. A retained Vulkan/GLES initialization line
   belonging to that app is useful; an unrelated SurfaceFlinger EGL string or
   a supported-device-feature list is **not** proof of the app's chosen API.
   Missing/rolled-over/restricted logs are inconclusive. Share only relevant
   graphics lines after reviewing for personal data, not an entire device log.
4. The missing Windows presentation path needs a supported host trace or
   bounded read-only presentation diagnostics in an authorized test environment.
   Do not attach intrusive tools to a protected online match or disable anti-cheat.
   No RenderDoc/ANGLE forcing, APK modification or game injection was performed.

An optional user-chosen OpenGL run through the **native** graphics menu and a
full restart could be a comparison, not a repair or acceptance of the requested
Vulkan path. Even if that run worked, it must be reported as OpenGL, not Vulkan.
There is no evidence yet that shaders or texture Potato work in the reported
Free Fire session, nor any measured FPS improvement.
