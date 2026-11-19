# Built-in Shaders: blocked renderer integration

**Status: not implemented; do not merge or advertise a working Shaders tab.**

The requested feature is a new **Shaders** category in the player's own Settings,
with real game-image effects and **no additional DLL**. Only shader/visual-effect
features are in scope. Optimizers, FPS/vsync overrides, ADB/debloat, registry or
power-plan changes, and the uploaded overlay are not part of this request.
The existing regular-DLL startup feature is separate and is not a shader backend.

## What the supplied archive actually provides

Reference upload: [`zlag-dll-source.zip`](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/blob/main/zlag-dll-source.zip).
The inspected Git blob is `7d5f717ba1f76d2c41bc9244a24cd83636064f16`
(935,703 bytes, 89 archive entries). Inspection extracted selected text only;
no archive binaries or scripts were executed or incorporated.

- Its `version.dll` proxy installs `wglSwapBuffers` / `eglSwapBuffers` hooks.
- Its pipeline captures the game's backbuffer and performs post-processing on
  the emulator's render thread, before presentation.
- The visible effects include color grading, sharpening, FXAA, bloom and motion
  blur. A Settings page, persistent values or shader-source files alone cannot
  cause those effects to run.
- Its README declares GPLv3 for original code, and attributes shader sources to
  recovered ENIGMA work. This MIT project has not copied that implementation or
  assumed that its shader provenance is cleared. Independently authored effects
  or an appropriate licensing review would be required.

## Native contracts checked

The four pinned read-only references documented in the existing test workflow
were inspected as data, not launched or modified. In the **two latest** MSI and
BlueStacks reference EXEs, the registered `plr.qml` defines
`iGlContainerComponent` as a black `Rectangle`. The boot loader switches to this
component after startup. That placeholder does **not** expose the guest frame as
a Qt Quick texture.

The registered `UiDropShadowEffect.qml` does use `ShaderEffectSource` and
`ShaderEffect`, but its source is a QML shadow item, not the game's backbuffer.
The presence of these Qt types or shader-related EXE strings is not evidence
that a QML shader can process the native game surface. No supported guest-frame
texture provider or native post-processing control API was established from
these resources. This is a limitation of the integration currently available,
not a claim that a native implementation is impossible.

The public pinned QML source was also checked, but is not authoritative proof of
the renderer contract: its GL-container declaration differs from the registered
resources, and it already includes unrelated modifications.

## Safe preparation included

`src/assets/settings-panel-icons/shaders.svg` is original artwork matching the
supplied icon pack: 24 × 24, `currentColor`, no fill, 1.7 px rounded strokes.
It is embedded in both Customizer builds. The catalog reserves the identity
`bsishaderssetting.qml`, using the same default/custom-icon mechanism as other
pages **if that page is actually discovered**. The upload checksum still refers
only to the original supplied pack; the new drawing has separate attribution.

This reservation does **not** add a page to the native Settings model, repurpose
Graphics/About, install any hook, write shader settings, or modify rendering.
Existing twelve-tab players keep twelve tabs and eleven distinct default pack
icons. No dummy sliders, fake game-image preview or hidden DLL fallback were
added. There is no completed Shaders tab to navigate to in this build.

## Required before implementation and merge

1. Obtain the native player renderer source/integration point, or a documented,
   working in-process frame-texture/post-processing API accessible without an
   additional DLL. Establish threading, texture ownership, synchronization,
   supported rendering backends and device-loss handling.
2. Implement the shader-only processing in that native path, with independently
   authored or license-cleared code. Disabled/neutral settings must bypass the
   processing; unsupported renderers must report their limitation, not silently
   accept ineffective settings.
3. Add the actual Settings category, original default/customizable icon,
   accessible value controls, reset, persistence and save/discard behavior.
   Preserve existing pages, callbacks, cleanup/title fixes and recovery. Check
   complete generated resources against their fixed slots, not just templates.
4. Physically navigate to the tab in a running supported Windows emulator and
   verify changed **game-frame pixels**, not just UI bindings. Test neutral
   bypass, supported effects, reset, save/reopen, multiple instances, resize,
   fullscreen, renderer restart/device loss, and unsupported-backend behavior.
5. Run the current-head full application/reference/portable/Chromium CI matrix
   and merge only when the implementation and real-renderer checks pass.

The icon catalog/style checks and Qt 5/6 raster tests cover the prepared artwork
only. The existing native Settings tests cover the shell and previous fixes;
they do not certify game shader effects. The shader feature remains blocked,
so the user's conditional merge approval has **not** been exercised.
