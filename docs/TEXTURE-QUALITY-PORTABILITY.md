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
| Desktop OpenGL 3.3+ / GLES 3.1+ | Experimental draw-scoped private sampler replacement after an owned WGL/EGL game presentation is observed |
| GL 2 / GLES 2 / GLES 3.0 | Bypass; the required sampler/texture-level inspection contract is incomplete |
| D3D11 / D3D12 / Vulkan | Shared policy and typed descriptor adapters/tests only; **no game texture-binding interception** |
| Ten presentation effects | Independent processing; their pixel tests are not proof of Potato texture reduction |
| Render resolution | Independent native Display configuration, requiring Save and restart |

The UI switch uses a minimum-LOD floor of **3.0**, clamped to existing exposed
mips and the original sampler's maximum LOD. The shared policy also supports
**1.5** for tests/future callers; there is no second texture-strength selector.
Positive floors select coarser existing mips. They do not shrink allocated
textures, create mipmaps, remove geometry, change the game's draw distance,
change DPI/FPS caps or guarantee any FPS improvement. `texelFetch` and other
samplerless accesses may ignore this policy.

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

The follow-up after that repair is also **not working**: 1,235 processed frames,
2,468 inspected calls, all 2,468 first-rejected as **depth test disabled**, zero
eligible/prepared calls/bindings and zero nested inspected/prepared calls. No
counted draw reached shader/sampler qualification; no-mip/blend/producer/API
conclusions can be inferred from the zero later buckets. Repeating the already
answered active-gameplay/Save/report questions is not the next implementation
step. The separate [original-DLL runtime experiment](POTATO-DLL-PROBE.md) has
now executed the actual supplied bytes: all five unique originals changed
known mip pixels with depth disabled, and the independently authored private-
sampler alternative passed 64 exact fixture Off cycles. This confirms a real
mechanism difference, without removing production ownership, protection,
depth or UI gates, adopting unknown producers or certifying the player's path.

A zero-binding outer wrapper must release the inspection recursion guard and
render mutex **before calling its original**: that original may establish real
scene state and call another intercepted entry. Previously the bypass retained
both leases until after that original, hiding its inner draw. The production
`GlDrawScope` policy now distinguishes this normal bypass from a live private
sampler override. Applied outer bindings stay protected through their original;
inner aliases cannot double-override/count them. Inspection exceptions retain
the fail-closed lease, presentation-internal draws remain excluded, and nested
inspection is bounded to eight API levels. Each original still executes once;
no indirect/index memory is read. The same exact route/protection/scene/sampler
checks apply at each newly inspectable level.

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
- An opaque triangle draw, depth testing and depth writes enabled, a real depth
  attachment of at least 16 bits, and a depth function other than NEVER/ALWAYS.
  Merely enabling the depth-test bit on a UI/no-depth target is insufficient.
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

**Important remaining provenance limit:** an owned context, ordinary color
format and scene-like draw state do not independently prove that every sampled
texture is a game's diffuse/albedo color. Normal/data maps, shared resources or
unusual UI passes can look similar to these checks. The policy's proof fields
are caller metadata, not a hardware attestation. These conservative checks are
not universal resource-semantic safety, protected-driver certification, or
installed-game acceptance. Do not present this experiment as such.

### Private replacements and failure behavior

Original texture/sampler descriptors are never edited. All inspected sampler
fields are cloned into a private sampler; only its minimum LOD differs. Wrap,
filter, comparison, border color (desktop), decode/reduction and other supported
fields are retained. A queried replacement must match its requested descriptor
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
- `bsi.shaders.v3` stores 14 values: ten visual steps, visual enable, resolution
  mode, texture enable and external-override review. Released v1/v2 profiles are
  read without writes and migrate on Save, retaining resolution recovery data.
- Texture Save alone never writes native Display. The texture option does not
  require a restart for arming subsequent eligible draws, but hooks must have
  captured a qualified game presentation. This is not evidence of changed game
  pixels or a hot native-framebuffer resize.
- A saved-On/zero-prepared report explicitly says **no texture override is
  confirmed**; it must not be diagnosed as an unsaved/off draft. The footer
  repeats this instead of implying that saving again will make it active.
- Inspected calls, eligible scene calls, depth/UI, blended, no-qualified-sampler
  and other bypasses are separate from prepared sampler draw/binding counts.
  They reset on texture-setting or presentation-source changes; old lifetime
  success cannot masquerade as activity after a new toggle/context. A later HUD
  bypass cannot erase earlier preparation evidence; its latest reason is kept
  separately in **Copy rendering status**. Arming hooks is not a prepared draw.
- **Copy rendering status** splits the depth gate into its **first blocking
  check per call**: test disabled, writes disabled, NEVER, ALWAYS, no attachment,
  or fewer than 16 depth bits. An unreported/unknown reason stays unspecified;
  it is not guessed to be a GL state. Rejection stops before shader/sampler
  queries, and the depth-disabled case needs only its enabled-state query.
  All subcounts sum to the depth aggregate and reset with the same source or
  setting change. No depth/UI/protection condition is weakened to raise counts.
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
| Strict C++ policy/GL safety | Descriptor preservation, extension legality, protected/unknown-protection refusal, depth/UI/border guards, cache/rollback behavior |
| ASan/UBSan + leak detection | Eight injected allocation failures, partial-binding/active-unit rollback, explicit-release sampler cleanup; public-API doubles, not a protected driver |
| Actual ANGLE/SwiftShader GLES 3.1 pixels | Production private samplers select test-owned mip colors at 1.5/3.0, restore exact native pixels/samplers, preserve pending errors and reject depth/UI/no-mip/existing-override cases |
| Native core CTest | 17 tests, including the production nested-draw lease policy, v3 preferences, depth-reason accounting, GL safety, native source/image/recovery and existing Vulkan pixels; software fixtures only |
| Windows x64 cross-compile/link | Complete EGL/DXGI/Vulkan hook test executables; compilation/linking is **not execution** |
| Actual Qt 6 plugin and editors | Texture review/toggle/Save/Discard/preset independence, truthful no-game diagnostics and independent native-resolution/restart controls |

Windows CI additionally builds/runs real MinHook detours with authored EGL/GL
state and Vulkan lifecycle DLL doubles, native HWND ownership and DirectX WARP.
The EGL regression requires private minimum-LOD samplers **inside** each of the
46 original draw entry points, exact argument forwarding/once-only calls,
compositor-only zero-prepared behavior, another scene context on the exact
selected drawable, UI/protected/pbuffer/other-drawable refusal, off/on evidence
reset, source teardown and a late independent provider. Those tests do not
execute an EGL game driver, protected GPU memory or an emulator. Software
CI also requires GLES and desktop-core pixels and sanitizer tests; a pending or
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
