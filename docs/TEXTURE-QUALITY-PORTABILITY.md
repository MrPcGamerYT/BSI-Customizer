# Potato graphic — experimental GL/GLES texture sampling

**Implemented as a default-off experiment, not universal renderer/player/GPU
support. PR121 stays draft.** The native Shaders page now has a **Potato graphic**
switch, separate from both the ten visual effects and
[Render resolution (optional)](POTATO-NATIVE-RESOLUTION.md). It arms independently
authored draw-time private samplers, not a final-image blur or a disguised
resolution reduction. The production module contains no supplied DLL,
transplanted instruction bytes or recovered proxy source. See the
[static archive review and separate executed original-DLL probe](POTATO-DLL-REVIEW.md).

## Exact implemented boundary

| Path | Texture-detail implementation |
| --- | --- |
| Desktop OpenGL 3.3+ | Experimental private bias + minimum floor, bounded by the queried device limit, on inspected owned-drawable mip-color bindings; saved On selects depth-independent compatibility |
| GLES 3.1+ | Experimental private minimum-LOD floor on the same depth-independent owned-mip path; no desktop bias query/state |
| GL 2 / GLES 2 / GLES 3.0 | Bypass; the required sampler/texture-level inspection contract is incomplete |
| D3D11 / D3D12 / Vulkan | Shared policy and typed descriptor adapters/tests only; **no game texture-binding interception** |
| Ten presentation effects | Independent processing; their pixel tests are not proof of Potato texture reduction |
| Render resolution | Independent native Display configuration, requiring Save and restart |

The UI switch uses a minimum-LOD floor of **3.0**, clamped to existing exposed
mips and the original sampler's maximum LOD. Qualified **desktop GL** now also
uses private additive bias, bounded by that floor and the queried
`GL_MAX_TEXTURE_LOD_BIAS`; GLES remains floor-only and never queries that enum.
The shared policy also supports **1.5** for tests/future callers; there is no
second texture-strength selector. Positive bias/floors select existing mips.
They do not shrink allocated textures, create mipmaps, remove geometry, change
the game's draw distance, change DPI/FPS caps or guarantee an FPS improvement.
`texelFetch` and other samplerless accesses may ignore this policy.

**A minimum-LOD floor is not additive LOD bias.** The [executed original-DLL
comparison](POTATO-DLL-PROBE.md#bias-versus-minimum-lod-floor-a-second-measured-difference)
found that an already-minified native mip-4 sample stayed at mip 4 under the
former floor-only plan, while the originals selected this fixture's last mip 6.
A separate private desktop bias+floor sampler matched the original Strong result
at both tested gradients and restored native Off pixels/state. The initial
[bounded desktop integration](POTATO-DESKTOP-BIAS.md) closed that fidelity gap
behind the existing depth gate and could not address the 4,506 early refusals.
The subsequent [production owned-mip compatibility path](POTATO-DEPTH-COMPATIBILITY.md)
now makes saved On independent of depth test/write/function/attachment, while
retaining the exact owned-drawable/protection/resource checks and draw-scoped
restoration. This is broader sampling, **not new scene/albedo semantic proof**:
opaque mipmapped in-game HUD and RGB-packed data maps may also change. Separate
Qt/Chrome windows and unknown offscreen/share-group producers are not adopted.

The regular `BSIShaders` module observes **20 draw signatures / 46 core and
extension names**: arrays/elements, range, instancing, base vertex/base instance,
multi-draw and indirect/count variants. The earlier five-entry-point set could
see only a compositor's plain `DrawArrays` while missing a translator's scene
geometry. Signatures and ABI-compatible aliases share one bounded inventory;
arguments, pointer arrays, signed base vertices and 64-bit count-buffer offsets
are forwarded unchanged. CPU/GPU indirect/index buffers are never inspected.
This still is not coverage of every vendor command or another process's guest
renderer. Original calls execute once on both supported and bypass paths.

A qualified **presentation must happen first**. A second current scene context
can then be armed on that **exact same provider/display/drawable/game HWND**,
without having to swap itself or stealing the presentation claim. The previous
same-context-only gate missed this arrangement. A draw cannot claim a source.
Unknown, offscreen, pbuffer, share-group-only and other-window/drawable contexts
remain native: a presented compositor image does not prove ownership of its
producer's scene resources. These are repaired interception gaps, **not proof
that either was the cause in the user's game**; installed-player evidence is
still required.

### Nested translator draws and the active-gameplay failure

A later confirmed active-gameplay report had **27,620 inspected calls, all
27,620 rejected by the depth gate, and zero eligible/prepared draws/bindings**,
while ordinary host-OpenGL presentation effects were processing. This is a real
negative texture result, not an unsaved switch or a lobby-only test. It does
not identify which subcheck failed or prove ownership of another producer.
See the [report and remaining uncertainty](FREE-FIRE-RENDERER-ANALYSIS.md).

Both follow-ups after that repair are **not working**: first 1,235 processed
presentation frames / 2,468 inspected calls, then **2,375 / 4,506**. All 4,506
latest calls were first-rejected as **depth test disabled**, with zero eligible/
prepared calls/bindings and zero nested inspected/prepared calls. No counted
draw reached shader/sampler qualification; no-mip/blend/producer/API
conclusions can be inferred from the zero later buckets. Repeating the already
answered active-gameplay/Save/report questions is not the next implementation
step. The separate [original-DLL runtime experiment](POTATO-DLL-PROBE.md) has
now executed the actual supplied bytes: all five unique originals changed
known mip pixels with depth disabled, and the independently authored private-
sampler alternative passed 64 exact fixture Off cycles. This confirmed a real
mechanism difference. The current independently authored compatibility policy
now addresses that depth dependence explicitly, without removing the exact
owned-route, protection, blend, mip/resource or existing-override checks,
adopting unknown producers or certifying the player's path. Original behavior,
new production software pixels and still-missing installed-game acceptance are
separate results; no repeated Save/gameplay/report request follows from this.

A zero-binding outer wrapper must release the inspection recursion guard and
render mutex **before calling its original**: that original may establish real
scene state and call another intercepted entry. Previously the bypass retained
both leases until after that original, hiding its inner draw. The production
`GlDrawScope` policy now distinguishes this normal bypass from a live private
sampler override. Applied outer bindings stay protected through their original;
inner aliases cannot double-override/count them. Inspection exceptions retain
the fail-closed lease, presentation-internal draws remain excluded, and nested
inspection is bounded to eight API levels. Each original still executes once;
no indirect/index memory is read. The same exact route/protection and selected
policy's sampler checks apply at each newly inspectable level.

The portable scope regression includes an old-guard-equivalent failing negative;
the Windows fixture separately exercises both originals through actual MinHook,
checks private bindings **inside** each, and refuses nested UI/protected/other-
drawable/unmapped calls. This is a demonstrable guard gap, **not proof the user's
player uses this nested pattern** and not certification of game texture changes.

### State and resource checks

`GlTextureOverride` currently requires:

- A tracked current context and the **exact current drawable**, on an eligible
  non-Qt/non-Chrome child HWND owned by the attached player. The setting or a
  loaded graphics DLL is not proof. EGL identities include provider, display and
  surface; context identities also include the provider/display. One UI provider
  does not monopolize a later game's provider. Alternate scene contexts repeat
  the same legal EGL protection checks before any GL initialization/inspection.
- An ordinary **unblended triangle draw**. Saved Potato On explicitly selects
  `OwnedMipCompatibility`, which does **not** require depth test/write/function
  or a depth attachment and never changes those states. The conservative
  `DepthQualifiedScene` policy remains a separate test/explicit-caller control;
  it is not the hidden policy behind the UI switch. Unknown policies bypass.
- An active ordinary `sampler2D`, a normalized RGB/RGBA format, dimensions at
  least 64×64, and a matching existing mip chain. Depth, integer, HDR, external,
  array/cube and current draw-attachment textures are not selected.
- Ordinary mip filtering, no comparison/reduction/anisotropic policy and no
  existing nondefault minimum-LOD or bias override. The exact GL default
  minimum of -1000 is unrestricted, not an existing custom override. GLES
  extension-only border/mirror-clamp wrap descriptors are bypassed.
- Complete public GL extension/protection queries. Protection enums are queried
  only when advertised. Protected EGL surfaces/contexts and GL protected
  contexts/textures are excluded. If EGL permits protected resources but GL
  cannot inspect per-texture protection, texture Potato refuses that context.
- Explicit user review of external driver LOD/AA overrides. This is an
  acknowledgement, **not detection or certification** of the real profile.
  No NVIDIA/AMD/Intel profile is inspected, reset or modified.

**Important broader-scope/provenance limit:** an exact owned draw and ordinary
RGB/RGBA format do not prove that every sampled texture is diffuse/albedo color,
or establish its complete creation/sharing history. Opaque mipmapped in-game
HUD, RGB-packed data/normal maps and shared objects used on this draw may also
be sampled more coarsely. The QML explanation and diagnostics disclose this;
there is no blanket "UI/data untouched" assurance. Separate Qt/Chrome surfaces,
foreign drawables, protected resources, blended draws and unsupported formats
remain native. The proof fields are caller metadata, not hardware attestation
or semantic classification. This is not universal resource-semantic safety,
protected-driver certification or installed-game acceptance.

### Private replacements and failure behavior

Original texture/sampler descriptors are never edited. All inspected sampler
fields are cloned into a private sampler; only its planned minimum LOD and,
on qualified desktop GL, bounded additive bias differ. Wrap, filter, comparison,
border color (desktop), decode/reduction and other supported fields are retained. A queried replacement must match its requested descriptor
before it is bound. Original sampler bindings restore immediately after the
original draw, and `ACTIVE_TEXTURE` restores before that draw.

Inspection is bounded (32 used units/draw, 256 uniforms, 128 private cached
samplers/context, 16 tracked active game contexts). It performs render-thread
queries and allocations; it is **not a free performance optimization**. RAII
rolls back partial bindings/active-unit changes before the original draw on
allocation or diagnostic failure. Pending GL errors are not consumed to test
our own work. Cache exhaustion or an incomplete/unsupported state is a bypass.

Surface/context destruction releases the presentation claim so a replacement
can be detected. Module/callback code remains pinned while a trampoline could
still be used. Native share-group/object lifetime, multi-threaded driver
behavior and private GPU-object cleanup across real context recreation still
need player/driver review; the isolated explicit-release leak tests below do
not certify those lifetimes.

## Save and diagnostics

- Both texture flags default off. Selecting the switch/review checkbox is only
  a draft; existing native Save/Discard/navigation semantics are retained.
- The required driver-review checkbox is directly beside/below the switch,
  before the explanation, and is revealed/focused when needed. Save guides the
  user to that checkbox instead of submitting an unreviewed draft to a remote
  red error. No review is auto-checked; the C++ validator still refuses a bypass.
- Status explicitly separates **Not saved / current saved On or Off** from
  **Saved setting / runtime draw evidence**. A checked draft cannot imply an
  armed runtime, and a saved toggle cannot imply successful game texture draws.
  Presets, Enhance and Reset visuals preserve the texture flags and independent
  native-resolution mode. Texture-only Save does not open a restart dialog.
- `bsi.shaders.v3` stores the same 14 values: ten visual steps, visual enable,
  resolution mode, texture enable and external-override review. Runtime strategy
  selection adds no persisted flag/control. An already saved On now selects
  depth-independent compatibility on load; Off still disarms it. Released v1/v2
  profiles are read without writes and migrate on Save, retaining recovery data.
- Texture Save alone never writes native Display. The texture option does not
  require a restart for arming subsequent eligible draws, but hooks must have
  captured a qualified game presentation. This is not evidence of changed game
  pixels or a hot native-framebuffer resize.
- A saved-On/zero-prepared report explicitly says **no texture override is
  confirmed**; it must not be diagnosed as an unsaved/off draft. The footer
  repeats this instead of implying that saving again will make it active.
- Inspected calls, **sampler-inspection calls**, conservative depth bypass,
  blended/no-eligible-mip-sampler and other bypasses remain separate from
  prepared sampler draw/binding counts. Compatibility eligibility is **not
  semantic scene proof**. The retained legacy/internal `textureSceneDraws`
  counter is also exposed as `textureEligibleDraws`; QML/diagnostics use the
  truthful name. Policy/source/setting changes reset evidence. A later HUD
  bypass cannot erase earlier preparation; its latest reason stays separate in
  **Copy rendering status**. Hook arming is not a prepared draw or changed pixel.
- **Copy rendering status** reports the actual `texturePolicy` and
  `textureDepthIndependent` state and discloses possible opaque HUD/RGB-data
  effects. Conservative-policy first depth blockers remain available: test
  disabled, writes disabled, NEVER, ALWAYS, missing attachment, under 16 bits,
  or unspecified. Compatibility skips depth qualification instead of inventing
  a successful depth observation; it preserves the ownership/protection,
  unblended-draw and sampler checks. Subcounts still sum/reset correctly.
- Nested inspected/prepared API calls are reported separately. Nested aliases
  are not additional frames or unique game geometry, and their presence is not
  a producer/resource-ownership proof. Presentation-frame and texture counters
  can have different reset epochs; do not infer a draws-per-frame ratio.
- Texture counters are separate from processed presentation-shader frames.
  Neither measures texture appearance, FPS, successful guest rendering or
  driver-profile compatibility. A scene in an unowned/offscreen producer is
  not modified just to make these counters increment.
- On an observed DirectX/Vulkan texture path, status explicitly says texture
  interception is unimplemented. It never substitutes resolution or relabels a
  GL/UI surface as Vulkan success.

## Automated evidence and limits

Current local source checks (CI must run against the pushed review head):

| Check | What passed |
| --- | --- |
| Strict C++ policy/GL safety | Explicit compatibility versus conservative controls, no hidden depth gate, invalid-policy refusal; desktop bias limits, descriptor preservation, GLES legality, protected/unknown-protection, blend/mip/format/review/existing-override/border checks |
| ASan/UBSan + leak detection | Eight injected allocation failures **per desktop/GLES path per draw policy**, partial-binding/active-unit rollback and explicit-release cleanup; public-API doubles, not a protected driver |
| Actual ANGLE/SwiftShader GLES 3.1 pixels | Production native LOD 0/4, both source modes/strengths, 128 exact Off/source-LOD cycles including 64 depth-independent; color-only FBO/no depth attachment; full spatial-material detail reduction and byte-exact full-image Off; pending errors |
| Old-depth-gate negative controls | Restoring unconditional depth rejection fails both production C++ and actual GLES mip-pixel tests at the expected compatibility assertion |
| Native core CTest | 17 tests, including saved/reopened On selecting actual runtime compatibility, policy-only evidence reset, nested-draw lease, depth accounting, GL safety, native source/image/no-growth recovery and mandatory Vulkan pixels |
| Windows x64 cross-compile/link | Complete EGL/DXGI/Vulkan hook test executables; compilation/linking is **not execution** |
| Actual Qt 6 plugin and editors | Texture review/toggle/Save/Discard/preset independence, truthful no-game diagnostics and independent native-resolution/restart controls |

Windows CI additionally builds/runs real MinHook detours with authored EGL/GL
state and Vulkan lifecycle DLL doubles, native HWND ownership and DirectX WARP.
The EGL regression requires private minimum-LOD samplers **inside** each of the
46 original draw entry points under both conservative controls and the actual
compatibility policy with depth test/write/attachment absent. It checks exact
arguments/once-only calls, restoration, live-override recursion, Qt/protected/
pbuffer/other-drawable refusal, off/on evidence reset, teardown and an independent
late provider. Those tests do not execute an EGL game driver, protected GPU
memory or an emulator. Software CI separately requires real GLES/desktop-core
pixels and sanitizers, retains authored native/Strong/Off PNG/JSON evidence and
publishes bounded successful measured mip/material annotations. A pending or
failed check is not a pass. The D3D/Vulkan descriptor tests and their existing
ten-effect pixels remain distinct from missing Potato game-binding tests.

## Build and remaining acceptance

From an x64 Visual Studio 2022 developer PowerShell, without Qt for policy and
hook tests:

```powershell
cmake -S tests/shaders -B build-texture-quality -G "Visual Studio 17 2022" `
  -A x64 -T v143 -DBSI_TEST_VULKAN=OFF
cmake --build build-texture-quality --config Release
ctest --test-dir build-texture-quality -C Release --output-on-failure
```

Use the matching Qt SDK and `src/shaders/CMakeLists.txt` to build the regular
plugin; see [integration](SHADERS-INTEGRATION.md). A successful build does not
install or certify it.

Before calling texture Potato accepted, verify actual game mip-selection/image
changes and exact off restoration; ordinary and shared resource semantics;
resize/context/device teardown and provider/translation layers; driver profiles;
and repeatable image quality/frame times on the target physical GPUs. Implement
and validate resource/view/descriptor ownership and synchronization before
claiming D3D11/D3D12/Vulkan texture support (including static/immutable samplers).
The reported NVIDIA startup issue still needs the affected profile, player,
GPU/driver and existing failure evidence; no fix is established here.

**Do not merge on software fixtures alone. No universal-renderer, physical-GPU,
FPS, anti-cheat/account-safety or NVIDIA-conflict claim is made.**
