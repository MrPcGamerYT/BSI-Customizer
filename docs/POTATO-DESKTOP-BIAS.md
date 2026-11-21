# Potato: initial bounded private desktop LOD-bias integration

> **Historical integration at `9906981`, not the latest draw policy.** That
> commit left the depth classifier unchanged and could not address the 4,506
> depth-disabled calls. The subsequent [depth-independent owned-mip production
> path](POTATO-DEPTH-COMPATIBILITY.md) now makes saved Potato On select an explicit
> broader compatibility strategy. This page preserves the first integration's
> evidence/boundary; it must not be read as saying current On still requires
> depth. Installed-game success remains unverified.

## Latest installed-player result remains negative

The latest user-supplied rendering status is:

```text
Requested / configured hint / observed host API: Automatic / OpenGL / OpenGL
Presentation effects: active; processed presentation-shader frames: 2375
Texture inspection: armed; inspected calls: 4506
First depth blocker: TestDisabled=4506; every other depth subcount=0
Eligible / prepared calls / prepared bindings: 0 / 0 / 0
Blended / no-qualified-sampler / other bypass: 0 / 0 / 0
Nested inspected / prepared: 0 / 0
Qt bridge / host: 5.15.2 / 5.15.4
Vulkan: 1 loader, 1 instance, 1 device; 0 Win32 surfaces/swapchains/presents
```

**Potato is not working in this player.** The earlier 1,235-frame / 2,468-call
follow-up had the same first blocker. Every counted call stops before shader,
texture or sampler inspection; zero later buckets are not evidence of missing
mips or absent blending. Nested zero does not describe excluded/unhooked calls.
The report proves neither a different producer/API nor a safe resource to adopt.
Presentation and texture counters are separate; no ratio or FPS is inferred.
No repeat Save, active-gameplay/Settings-closed confirmation or full-report
request is needed, and no lower-resolution substitute is proposed.

## A separate, proved implementation gap is now integrated

The [original-DLL experiment](POTATO-DLL-PROBE.md#bias-versus-minimum-lod-floor-a-second-measured-difference)
actually demonstrated that the former minimum-LOD-only plan was not equivalent
to the originals' additive bias. A native mip-4 sample stayed at mip 4 under a
floor of 3, whereas a private desktop sampler with bias 3 + floor 3 selected this
fixture's last mip 6 and restored native Off pixels/state.

At `9906981`, that independently authored mechanism reached the production
`GlTextureOverride` path **only after all then-existing scene/resource checks passed**:

1. Identify desktop GL 3.3+, then complete the existing protection/extension
   checks. Resolve `glGetFloatv` and query the actual `GL_MAX_TEXTURE_LOD_BIAS`.
   Missing, nonfinite or nonpositive support fails initialization closed. This
   enum is never queried on GLES and no pending GL error is consumed.
2. Run the unchanged route, review, primitive, depth, blend, shader, resource,
   mip, protection and existing-sampling-policy checks. A capability is **not**
   resource ownership or permission to bypass any of these checks.
3. Plan the same minimum floor, clamped to the view's exposed mip count and the
   source sampler's maximum LOD. For desktop GL only, also plan additive bias
   bounded by that floor and the queried device limit. Strong requests 3.0;
   Light 1.5 remains a policy/test option, not a new UI selector.
4. Clone supported source state into a **private** sampler. Change only its
   planned floor/bias, read back the full supported descriptor before binding,
   and restore the exact original binding after the draw. Original texture and
   sampler objects, existing LOD/AF overrides and driver profiles are not edited.
5. GLES 3.1 retains its portable **floor-only** path. A desktop bias capability
   cannot be supplied to authorize GLES bias. The default descriptor-planning
   API also stays floor-only unless an explicit valid desktop limit is supplied.

The cache already keys the full supported descriptor, including bias. Existing
allocation-failure rollback, bounded cache, active-unit restoration, recursion
lease and pending-error preservation remained in force. That initial integration
changed no preferences/QML/native Settings source, save/restart behavior,
Shaders-before-About order or protection/owned-drawable gate. No original DLL
code or binary was integrated.

This closed a **sampling-fidelity gap on already-qualified desktop draws**. It
could not make the reported 4,506 calls reach preparation: they stopped earlier
at the unchanged depth gate. Depth is not a GL prerequisite for coarser mips,
but `9906981` used it as a conservative scene-classification rule. The follow-up
now defines and tests an [explicit owned-mip compatibility contract](POTATO-DEPTH-COMPATIBILITY.md)
without requiring depth or pretending broader bindings are proven albedo/scene
textures. It discloses opaque mipmapped HUD/RGB-data effects while preserving
ownership/protection/Off restoration; it does not adopt offscreen/share-group
producers or establish installed-game acceptance.

## Validation boundary at the initial integration

The following counts/results describe `9906981`; the subsequent compatibility
regression expands real pixels to 128 cycles and adds depth-independent material
images. See its separate current evidence rather than treating old passes as
validation of new code.

- Policy tests cover both strengths, actual device limits, exposed mip/max-LOD
  bounds, default floor-only behavior, invalid limits, GLES rejection of desktop
  bias, exact Off/source preservation and unchanged ownership/protection/review/
  existing-override refusals.
- Tests around the **actual production** sampler code cover descriptor readback,
  desktop border/wrap/filter preservation, missing/invalid capability refusal,
  a depth-disabled desktop refusal before sampler inspection, source-bias
  preservation, and **eight allocation failures per API path** with rollback
  before the application's original draw and no explicit-release name leaks.
- The real-pixel fixture calls production `begin` / draw / `end`, not a Python
  replacement. It tests native lambda 0 and 4, texture-object and bound-sampler
  sources, both strengths, **64 exact Off/source-LOD cycles per tested API**,
  private bias readback on desktop, review/UI/depth/no-mip/override bypasses and
  preservation of an existing application GL error.
- Local ANGLE/SwiftShader GLES 3.1 pixels pass all 64 cycles and all ten existing
  presentation effects. This does not run desktop GL or emulate its bias enum.
  Desktop software pixels must separately pass in the native Mesa CI job.
- Local native CTest passes **17/17 in 43.43 s**; ASan/UBSan with leak detection
  passes **17/17 in 289.39 s**, both with the mandatory pinned Vulkan library.
  Actual Qt 6.8.3 plugin/native-Settings fixtures retain native Save/restart
  pixels, callback identity, Shaders-before-About order and no-growth placement.
- Strict Windows x64 cross-compilation of the changed production code and
  sampler tests passes (compilation, not Windows execution). A separate local
  old-floor-only negative control omits the production private bias assignment
  and correctly fails the new desktop descriptor assertion.
- All four read-only reference player EXE hashes remain unchanged; none was
  executed. New data-only/source-contract checks pass; no original DLL is run
  again just to validate independently authored production changes.

Pushed-head CI conclusions are recorded with the PR; a pending run is not a
pass. All of these are authored software/API fixtures,
not target-game mip images, physical GPU/driver certification, allocated-memory
reduction, NVIDIA-profile compatibility, universal rendering or FPS evidence.
PR121 stays draft, unmerged and unreleased; the installed-game result above
remains **not working**.
