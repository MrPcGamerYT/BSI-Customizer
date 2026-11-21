# Portable texture-quality experiment — not an enabled player feature

**No universal texture-LOD Potato mode is implemented or enabled in Shaders.
No merge is approved by this experiment.** The separate
[native render-size Potato control](POTATO-NATIVE-RESOLUTION.md) changes actual
Display configuration after Save, not game texture/sampler bindings. This is independently authored, Visual
Studio-buildable source for the common sampler policy and API descriptor
adapters. It does not intercept game textures, rewrite descriptor bindings, or
replace a system DLL. The existing ten visual controls, native Save/Discard,
6 px tabs and game presentation paths are unchanged.

See the [supplied archive review](POTATO-DLL-REVIEW.md) for the reference's actual
behavior and hashes. None of that archive's code or binaries was incorporated.

## Can it work on every renderer and GPU?

**Not unconditionally.** Vendor-neutral source can cover several APIs without
an Intel/AMD/NVIDIA allowlist. That does not guarantee a useful or safe texture
reduction on every GPU, game, driver, profile or emulator.

- Texture detail reduction needs existing, valid mipmaps and an eligible color
  resource. A one-level texture has no coarser mip to select. Samplerless
  `Load`/`texelFetch`/image-fetch operations ignore sampler LOD limits. Creating
  mipmaps blindly costs work/memory and is unsafe for depth, integer and UI resources.
- OpenGL/GLES, D3D11, D3D12 and Vulkan bind resources/samplers differently.
  D3D12 static/root samplers and Vulkan immutable samplers cannot be treated as
  ordinary mutable sampling state. Descriptors/command buffers already in use
  by the GPU cannot safely be rewritten by a presentation callback.
- GL sampler objects/minimum LOD require GL 3.3 or GLES 3 (or a separately
  qualified extension path). GL 2 / GLES 2 are bypasses in this design, not
  silently called supported. Legacy D3D9/10 are not implemented adapters.
- Native HDR/protected/unsupported paths must retain their original rendering.
- An NVIDIA Profile Inspector override is external driver state. GL sampler
  queries do not prove that every such override is absent. No profile reset,
  registry change or universal conflict-detection claim is appropriate.

For a broadly portable **performance** adjustment now, use the emulator's own
Display settings to lower its render resolution while preserving the required
aspect ratio. The new Shaders native-size control wires that same config path
through explicit Save, original-size restoration and recovery. It reduces the
configured native framebuffer independently of GPU vendor/API but is not the
same texture-only appearance as the reference DLL; apps can choose their own
internal resolution. No setting changes on mode selection/open/Discard. Actual
player restart/render size and FPS remain separately unverified.

## Implemented source boundary

| File | Purpose |
| --- | --- |
| `src/shaders/TextureQuality.h` | Data-only shared policy and explicit refusal diagnostics |
| `src/shaders/GlTextureQuality.h` | Relevant GL/GLES sampler-field plan |
| `src/shaders/D3dTextureQuality.h` | Typed D3D11, D3D12 dynamic and D3D12 static sampler descriptor copies |
| `src/shaders/VulkanTextureQuality.h` | Conservative basic `VkSamplerCreateInfo` copy; unknown extensions refused |

The experiment sets a **minimum-LOD floor** of 1.5 (light) or 3.0 (strong),
clamped to the view's visible mip count and the source sampler's maximum LOD.
It does not stack a positive bias on another bias. Minimum LOD is available in
GLES 3, unlike desktop texture-LOD bias, and avoids changing Vulkan device bias
features. This is a consistent bounded policy, **not a byte-for-byte clone** of
the proxy's combined min-LOD/bias behavior.

Every input descriptor is retained. The output is data for a **private sampler
replacement**, not permission to mutate the original application's sampler.
Only the planned minimum-LOD field changes; bindings, wrap/border/filtering,
comparison behavior, maximum LOD and bias must remain intact. The GL adapter
contains the relevant planning fields, so a future caller must separately copy
the **full** original GL sampler description when allocating a private sampler.

All prerequisites default to **false/unknown**. A plan requires:

1. An identified game surface and a matching, implemented game API, not the Qt
   Settings renderer or a merely loaded graphics library.
2. Proven ownership of a sampled, nonprotected **game-color** resource and a
   valid view exposing at least two mips (bounded to at most 32).
3. A supported, private replacement mechanism with correct resource lifetimes.
4. Explicit review of external overrides. **The source does not detect or clear
   NVIDIA profiles.** The test fixtures' reviewed flag is test metadata only;
   it is not proof of a user's real driver configuration.
5. Finite, valid source sampling limits. Existing nonzero bias/minimum LOD,
   anisotropic/comparison/reduction/unnormalized sampling, unknown descriptor
   extensions and unsupported resources are unchanged bypasses. The GL adapter
   recognizes its exact API-default minimum LOD of **-1000** as unrestricted;
   other nonzero GL floors remain unchanged overrides, not accidental opt-ins.

The policy has bounded CPU work, no allocation, no vendor checks, no registry or
file access, no render-thread GL queries and no per-draw scan/clear of every
texture unit. Actual game integration could still introduce cost; only a
qualified binding/lifecycle cache and measurements can establish performance.

## Evidence and limits

Local development checks:

| Check | Result / what it proves |
| --- | --- |
| Shared policy + GL/Vulkan descriptor tests | Pass; five API routes, per-API rejection, view bounds, unchanged inputs and conservative refusal behavior |
| ASan/UBSan shared policy | Pass with leak detection and halt-on-error; not game-hook validation |
| Windows x64 cross-compilation/link | Pass for typed D3D11/D3D12 adapters and complete native fixtures; **not MSVC or Windows execution** |
| Real GLES 3.1 sampler pixels through ANGLE/SwiftShader | Pass; seven solid-color test-owned mips demonstrate 1.5 interpolation and 3.0 selection, original sampler/pixels restore |
| Native SwiftShader Vulkan sampler creation/destruction | Pass; bounded descriptors accepted; **not a texture-quality pixel or game-binding test** |
| Complete ordinary native CTests | 13/13 pass, including unchanged package/source/recovery and existing Vulkan pixels |
| Linux Qt 5.15.2 / Qt 5.15.19 / Qt 6.8.3 topbar scenes | All 20 pass on each with strict painted-text comparison; Windows result must be checked independently |

CI additionally requires desktop GL core and GLES 3 mip-selection pixels,
Windows MSVC/WARP D3D11 sampler creation/GetDesc, and D3D12 dynamic sampler-heap
and static-sampler root-signature creation. The D3D/Vulkan **existing ten-effect
pixel tests are not Potato pixel tests**. Neither descriptor construction nor
an existing color-grading pass establishes that a game's textures were changed.

No physical Intel, AMD or NVIDIA player, NVIDIA profile conflict, actual game
texture binding, FPS/latency improvement or complete universal mode is verified
by these results. Test failures/pending checks must not be called passes.

### Build the policy tests with Visual Studio, without Qt

From an x64 Visual Studio 2022 developer PowerShell with CMake/Windows SDK:

```powershell
cmake -S tests/shaders -B build-texture-quality -G "Visual Studio 17 2022" `
  -A x64 -T v143 -DBSI_TEST_VULKAN=OFF
cmake --build build-texture-quality --config Release --target shader-texture-quality-test
ctest --test-dir build-texture-quality -C Release -R shader-texture-quality-policy --output-on-failure
```

Build `shader-d3d-pixels-test` and run its D3D11/12 CTests for the separate WARP
API fixtures. Vulkan/GL software tests need their explicitly configured ICD/EGL
runtime. Building a test executable does not install a player feature.

## Required before enablement or merge

- Implement and independently review renderer-specific **resource/view
  ownership, private sampler/descriptor binding and synchronization** paths,
  including D3D12 static/root and Vulkan immutable/extended descriptors. Never
  infer that all a device's textures belong to the game from one swapchain.
- Prove no changes to native/UI/depth/integer/protected resources, with correct
  lifetime, sharing, descriptor reuse, context/device recreation and rollback.
- Prove off/neutral is original rendering, with no postprocess pass masquerading
  as reduced game work. Integrate drafts/persistence/native Save/Discard only
  after that path exists; do not ship an enabled-looking but ineffective preset.
- Verify actual game mip-selection pixels on every claimed backend, then measure
  image quality and frame times in repeatable scenes on the real target GPUs.
- Obtain the affected `.nip` profile, emulator/renderer, GPU/driver and existing
  failure log to reproduce and validate the reported NVIDIA startup issue.
- Complete all current-head full/reference/package/browser checks and the
  installed-player requirements in [the integration guide](SHADERS-INTEGRATION.md).

PR121 remains a draft until the requested feature and these conditions are
actually satisfied. A green software-only CI run is not, by itself, authority
to merge an unfinished universal texture-quality feature.
