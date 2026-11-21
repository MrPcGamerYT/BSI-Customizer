# Potato graphic — experimental GL/GLES texture sampling

**Implemented as a default-off experiment, not universal renderer/player/GPU
support. PR121 stays draft.** The native Shaders page now has a **Potato graphic**
switch, separate from both the ten visual effects and
[Render resolution (optional)](POTATO-NATIVE-RESOLUTION.md). It arms independently
authored draw-time private samplers, not a final-image blur or a disguised
resolution reduction. No supplied DLL, instruction bytes or source was copied.
See the [static archive review](POTATO-DLL-REVIEW.md).

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

The regular `BSIShaders` module observes selected public draw entry points
(arrays/elements, range and instanced variants). It does not cover every
multi-draw/indirect/extension entry point. A context is armed only after a
qualified presentation; earlier draws remain native. Off and unsupported paths
retain the original calls, and the original draw/present executes once.

### State and resource checks

`GlTextureOverride` currently requires:

- A tracked current context and the **exact current drawable**, on an eligible
  non-Qt/non-Chrome child HWND owned by the attached player. The setting or a
  loaded graphics DLL is not proof. EGL identities include provider, display and
  surface; one UI provider does not monopolize a later game's provider.
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
- Enabling without review is refused. Presets, Enhance and Reset visuals preserve
  the texture flags and independent native-resolution mode.
- `bsi.shaders.v3` stores 14 values: ten visual steps, visual enable, resolution
  mode, texture enable and external-override review. Released v1/v2 profiles are
  read without writes and migrate on Save, retaining resolution recovery data.
- Texture Save alone never writes native Display. The texture option does not
  require a restart for arming subsequent eligible draws, but hooks must have
  captured a qualified game presentation. This is not evidence of changed game
  pixels or a hot native-framebuffer resize.
- Prepared sampler draw/binding counters are separate from processed
  presentation-shader frames. Neither counter measures texture appearance,
  FPS, successful guest rendering or driver-profile compatibility.
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
| Native core CTest | 16/16, including v3 preferences, routing, GL safety, native source/image/recovery and existing Vulkan pixels; software fixtures only |
| Windows x64 cross-compile/link | Complete EGL/DXGI/Vulkan hook test executables; compilation/linking is **not execution** |
| Actual Qt 6 plugin and editors | Texture review/toggle/Save/Discard/preset independence, truthful no-game diagnostics and independent native-resolution/restart controls |

Windows CI additionally builds/runs real MinHook detours with authored EGL/Vulkan
lifecycle DLL doubles, native HWND ownership and DirectX WARP. Those tests do
not execute an EGL game driver, protected GPU memory or an emulator. Software
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
