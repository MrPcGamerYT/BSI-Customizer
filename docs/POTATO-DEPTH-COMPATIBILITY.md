# Potato: depth-independent owned-mip compatibility

## Why the original could act where our implementation refused

The actual supplied Potato DLLs ran in a disposable Windows software-WGL probe,
not just a static inspection. They changed LOD and authored mip pixels **with
depth testing disabled**, and left application texture/sampler parameters
persistently changed. See the [original measurements](POTATO-DLL-PROBE.md).

Our latest installed-player report was **2,375 processed presentation frames,
4,506 inspected calls, all 4,506 first-rejected as depth test disabled**, with
zero sampler inspection/preparation/bindings or nested calls. Depth was our
conservative scene heuristic, **not an OpenGL mip-selection requirement**. The
[initial bounded desktop-bias integration](POTATO-DESKTOP-BIAS.md) improved LOD
math behind that gate and therefore could not resolve this earlier refusal.
Zero later buckets in that report do not identify a missing mip chain, blending,
a foreign producer or another renderer. The prior active-gameplay/Settings-closed
confirmation stands; repeating Save, lowering resolution or requesting the same
diagnostics is not the implementation change.

## What the production path now does

Saved **Potato graphic On** now explicitly selects
`TextureDrawPolicy::OwnedMipCompatibility` through
`ShaderPreferences::texture()`. It is no longer dependent on passing the legacy
depth gate. This applies to an already saved On preference too; no extra mode,
profile flag, checkbox, native Display write or restart is introduced. The
existing switch and driver-review flags still default **Off** and must be
explicitly enabled/reviewed. Persistence remains the same 14-value v3 schema.

For the exact owned game drawable already selected by presentation:

1. Keep the existing provider/display/drawable/HWND authorization, protected
   EGL/GL checks and bounded draw detours. A new draw cannot claim another
   presentation, offscreen/pbuffer producer or share group.
2. Accept ordinary **unblended triangle calls without requiring depth testing,
   depth writes, a particular depth function or any depth attachment**. Do not
   change the application's depth state or pretend it passed scene validation.
3. Inspect the actual shader's ordinary `sampler2D` bindings. Require the same
   complete matching normalized RGB/RGBA mip chains, supported dimensions and
   descriptors. Unsupported depth/integer/HDR/noncolor, external, cube/array,
   protected and current-draw-attachment resources stay native. No mipmaps are
   generated and no source image/allocation is rewritten.
4. Preserve existing LOD/bias/anisotropic policies. Build and verify a **private**
   sampler: bounded additive bias plus minimum floor on desktop GL; legal
   minimum floor only on GLES. Strong requests 3.0, bounded by available mips,
   source maximum LOD and, on desktop, the queried device bias limit.
5. Bind the private sampler only around the original draw, forwarding its exact
   arguments once, and restore the exact original bindings afterward. Restore
   the active unit before the draw. Exceptions roll partial changes back before
   calling the application's original; pending GL errors are not consumed.

The legacy `DepthQualifiedScene` policy remains a separately tested, fail-closed
control for explicit callers. Invalid policies are rejected. It is **not** the
policy silently selected by the user-facing On switch. A policy-only runtime
change starts a new texture-evidence interval without changing the selected
presentation or inventing processed frames.

This follows the originals' demonstrated **depth-independent LOD mechanism**,
not their persistent mutation or system-proxy loading. No original code, binary,
instruction bytes, driver-profile reset or unrelated payload is incorporated.

## Broader scope is disclosed, not relabelled as proven scene textures

Depth independence is an intentional broader sampling contract. **Opaque
mipmapped in-game HUD and RGB-packed data/normal maps can also change.** An RGB
format cannot prove albedo semantics or an object's complete creation/sharing
history. Separate Qt/Chrome surfaces, other providers/drawables, protected
resources and blended draws remain excluded; this is not a claim that every
possible in-game UI/data resource can be recognized automatically.

The Shaders explanation and Copy rendering status disclose this trade-off.
Counters now say **sampler-inspection calls**, not "eligible scene calls".
Prepared draws/bindings mean temporary replacements were prepared, **not that
game texture pixels changed**. The retained `textureSceneDraws` internal/legacy
metric is exposed under the truthful `textureEligibleDraws` label as well.
`texturePolicy` and `textureDepthIndependent` report the actual runtime policy.
Legacy depth first-blocker counts remain available for conservative callers;
compatibility does not fabricate passed depth observations.

## Executed local validation

- **Actual production GLES 3.1 pixels** on ANGLE/SwiftShader: native lambda 0
  and 4, both source modes and both strengths; **128 exact Off/source-LOD cycles,
  including 64 with depth testing and writes disabled**. Both policies preserve
  pending errors. A test-owned color-only FBO with **no depth attachment** is
  rejected by the legacy control but coarsened by production compatibility.
- **Spatial texture-detail proof**, separate from color-coded mips: upload an
  authored 64×64 material and its box-filtered mip chain; call production
  begin/draw/end at unchanged target resolution with depth disabled. In both
  source modes, adjacent-RGB edge energy falls from **2,483,712 to 62,784**.
  Native and Strong full-image SHA-256 values are respectively
  `322819cac683c81d48f83c1a5d00aa8b7453c8fd98a965b543eebc0eb3d952ab`
  and `ac43e9856339e89d48eb752fbe9fd6100c62d5e3a3e30d00aa6fa348737c871d`.
  **The complete Off image equals native byte-for-byte**, including alpha;
  original source LOD descriptors remain unchanged. This is sampler-driven
  texture detail reduction, not a presentation blur or framebuffer resize.
- **Old-gate negative controls**: a separate build restoring the unconditional
  depth rejection fails both the C++ compatibility regression and the actual
  GLES mip-pixel test at the expected compatibility assertion. Missing APIs or
  dependencies are not accepted as the negative result.
- Production desktop/GLES public-API doubles cover the new policy, invalid-policy
  refusal, blend/missing-mip/depth-format/integer/existing-override/review and
  protected-resource bypass. **Eight allocation failures per API per policy**
  exercise pre-original active-unit/partial-binding rollback and explicit-release
  private-sampler cleanup. Defaults, descriptor readback and desktop bias-limit
  tests remain in place.
- Local native CTest: **17/17 in 50.90 s**; **ASan/UBSan with leak detection
  17/17 in 291.92 s**, both using the mandatory pinned Vulkan library. Includes
  preference/reopen policy selection, runtime policy/evidence reset,
  package/source/image/no-growth recovery,
  draw-scope rules and mandatory software Vulkan tests. Actual Qt 6.8.3
  plugin/Settings tests pass: native Save pixels, precommit restart, callback
  identity, persistent models, missing-module fallback, flat opaque shell and
  **Shaders immediately before About** remain unchanged.
- Windows x64 strict cross-compilation passes for production hooks and the
  extended EGL/MinHook test. Compilation is not execution. The Windows regression
  requires all **46 real draw detours** to expose private samplers inside the
  original while depth test/write/attachment are absent, plus exact restoration,
  live-override recursion, Qt/protected/other-drawable refusal and Off cleanup.

### CI evidence is separate from local GLES evidence

The shader workflow requires **both real desktop-core and GLES software pixels**
for this production implementation. It retains authored native/Strong/Off PNGs
and JSON measurements in `authored-texture-pixels-<run-id>` and publishes bounded
successful mip/material measurements through the check-annotation API, so a
retrieved job conclusion alone need not stand in for measured output. Final
pushed-head conclusions and retrieved measurements are recorded on draft PR121;
a pending/failed workflow is not a pass. Windows hook and cross-version Qt
execution remain separate from software GL pixels and from a running player.

## What has not been established

The previous installed-player result remains **not working**; this new
compatibility implementation has not been deployed to that player or measured
on its actual game textures. Removing the known early rejection does not prove
the remaining bindings qualify, that every guest draw reaches this host route,
or that unknown resources are safe to adopt. The original probe, desktop bias
math, new production software pixels, Windows dispatch doubles and eventual
game acceptance are distinct evidence sets.

GLES remains floor-only: at native lambda 4 a floor of 3 does not change that
sample, unlike desktop additive bias. Arbitrary shader lookup instructions,
physical GPU/driver behavior, real context/share-group lifetimes, exact in-game
Off restoration and game appearance still require target validation. D3D/Vulkan
texture interception is not implemented. Existing mip selection does not shrink
allocations. No FPS, universal renderer, NVIDIA startup-conflict, anti-cheat or
account-safety claim is made. **PR121 stays draft, unmerged and unreleased; no
blind player deployment is authorized by these fixtures.**
