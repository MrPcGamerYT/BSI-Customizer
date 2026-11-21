# Original Potato DLL — executed Windows experiment

**The actual five unique supplied DLLs have now run, not just been disassembled.**
Windows run [44915621801](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44915621801)
completed successfully at head `d2080aa167c043ba78fdc9f74c27441a9386048f`.
All **10 original-DLL cases, 2 no-DLL controls and 2 independent private-sampler
comparisons passed**. Complete, nontruncated per-case measurements were retrieved
through the GitHub check-annotation API and checked independently.

This is **software WGL on authored textures, not a player/game fix, physical-GPU
test or FPS result**. No original DLL/archive/disassembly/Mesa binary is committed,
redistributed or installed in a player. The unrelated Z-Lag startup workflow is
not used as Potato evidence. Production `src/` remains identical to `f05734c`;
these experiments do not change Shaders/About order, approved Settings appearance,
Save/Discard/callback identity or production ownership/protection/depth gates.

## Measured original behavior

Each identity ran in two fresh children: texture-object sampling, then sampling
with an existing sampler object. A real GLSL shader sampled a complete 64×64
color-coded mip chain. **Depth testing stayed disabled throughout.** Before any
swap, every case read red mip 0, RGBA `[255,0,0,255]`.

| Original identity / archive location | Texture bias | Texture minimum LOD | Actual sampled mip / pixel | Both source modes |
| --- | ---: | ---: | --- | --- |
| A — `Intel/Bajo/dinput8.dll` | 1.5 | -1000 (unchanged default) | 2 / blue `[0,0,255,255]` | PASS |
| B — `Intel/Fuerte` and byte-identical `BS MSI 4/forte/dinput8.dll` | 3.0 | -1000 (unchanged default) | 3 / yellow `[255,255,0,255]` | PASS |
| C — `BS MSI 4/leve/dinput8.dll` | 1.5 | 1.5 | 2 / blue `[0,0,255,255]` | PASS |
| D — `BS MSI 5/forte/version.dll` | 3.0 | 3.0 | 3 / yellow `[255,255,0,255]` | PASS |
| E — `BS MSI 5/leve/version.dll` | 1.5 | 1.5 | 2 / blue `[0,0,255,255]` | PASS |

The [static review](POTATO-DLL-REVIEW.md#dll-sha-256-identities) records full hashes
and exact archive paths. When a sampler was bound, its bias/minimum matched the
same identity's expected values. All original cases separately demonstrated:

- Normal import loading of the **exact original bytes**, no anchor/initializer
  call, and successful native child exit without timeout.
- Read-only bounded jump inspection showing actual swap, draw and WGL-resolved
  bind entries leading into the loaded original image.
- Native parameters immediately after swap, then changed texture/sampler
  parameters **after the intercepted draw returned**, plus coarser-mip pixels.
  The original mutation persists; it is not restored at the end of that draw.
- A new texture acquiring the expected bias/minimum on the WGL-resolved bind
  path **before its first draw**. Active texture unit restored correctly.
- Zero remaining GL error in this fixture. This does **not** prove preservation
  of preexisting application errors; some original paths consume error state.

Both no-DLL controls stayed red at mip 0, retained native parameters, and had no
payload-directed detour. Hook installation, setting a value and changing a mip
pixel are separate measurements, not inferred from frame/call counts.

### A real entry-point coverage gap, not hidden by the passing result

Mesa exposes distinct `glBindTexture` addresses for its import export and its
`wglGetProcAddress` dispatch entry. In **all five originals**, the latter was
hooked and changed the new texture; the import export was **not** hooked, and a
new texture bound through that export retained bias 0 / minimum -1000.

Both are explicitly measured. The earlier run's bind assertion failed because
it exercised only the uncovered import export; it was not silently accepted.
The corrected probe calls both real entries and retains the negative evidence.
This is a provider-specific coverage finding, **not** proof that the player's
entry points are identical, fully covered or responsible for its failure.

## Another approach actually tried: reversible, resource-owned private samplers

Two further fresh **no-original-DLL** children used the independent
`planGlTextureQuality` policy directly on the texture created by this host.
This provides an explicit resource contract by construction; it does not label
unknown game resources as owned merely because a proof boolean exists.

| Source sampling | Light floor 1.5 | Strong floor 3.0 | Off | Repeated on/off cycles |
| --- | --- | --- | --- | ---: |
| Texture-object source | Blue mip 2 | Yellow mip 3 | Exact original red mip 0 | 32 PASS |
| Existing sampler source | Blue mip 2 | Yellow mip 3 | Exact original red mip 0 | 32 PASS |

**All 64 cycles passed with depth disabled, source descriptors unchanged, no
original DLL, zero GL errors and an unrelated sampler sentinel untouched.**
Only a private sampler binding changes for the known draw; the exact original
binding restores **before** the next Off draw. The checked state also includes
active unit, original bindings, program, draw/read FBO, VAO, viewport and
depth/blend/cull state. This proves the mechanism can coarsen identified color
mips reversibly without depth testing; it is not a production scene-discovery
implementation or a claim that every possible GL state was tested.

## What this changes about the investigation

The player's latest result is still **not working**: 1,235 processed frames,
2,468 inspected calls, all first-rejected as `TestDisabled`, zero eligible/
prepared calls or overridden bindings and zero nested inspected/prepared counts.
The earlier active-3D/Settings-closed
confirmation stands. No repeat Save/gameplay/full-report request is needed.

The experiments establish a concrete difference: the original applies LOD to
binds/draws regardless of depth testing; our production eligibility gate rejects
these counted calls before sampler inspection. **Depth testing is not an OpenGL
prerequisite for coarser mip selection.** It is a conservative classification
rule in the current implementation, not evidence that a depth-disabled draw
cannot sample scene textures.

The independently tested alternative is **identified-resource, draw-scoped
private sampling**, not the original's persistent process-wide mutation. Using
it in the player still requires a real scene/resource authorization contract;
the existing owned presentation HWND alone does not identify every producer,
shader or texture. These tests do not authorize UI, protected, foreign,
offscreen/share-group-only or otherwise unidentified resources. No production
gate is deleted just to raise counters, no proxy is dropped into the player,
and a fixture success does not overwrite the negative installed-game result.

No in-game texture before/after, in-place original-DLL Off restoration, NVIDIA
profile/startup compatibility, allocated-memory reduction, geometry change,
FPS, universal renderer or anti-cheat/account safety is established. Positive
LOD chooses existing mips; it does not resize the allocated mip chain.

## Reproducibility and execution boundary

Source: `tests/potato-dll/{CMakeLists.txt,prepare.py,prepare_test.py,probe.cpp,run-offline.ps1}`.
The manual `CI` input `test_potato_dll` defaults **false** and is mutually exclusive
with the separate Z-Lag input. Only an explicitly opted-in dispatch calls
`potato-dll-probe.yml`; push/PR/default checks inspect contracts but never run
original supplied code. No emulator, game, account, driver profile or player EXE
is part of this experiment.

- Fresh GitHub-hosted **Windows 2022 x64**, independently built MSVC C++17 hosts,
  no Qt/Customizer module, no inherited secrets, no persisted checkout credentials.
- Original archive SHA-256
  `80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17`;
  17 entries, six members/five unique hashes, paths, symlinks, expanded sizes,
  AMD64 headers and existing native anchors are checked **as data first**.
- Pinned Mesa **MinGW x64 26.2.7**, archive SHA-256
  `19c74aad7b09e682473b8de4c35b47e6498de97b71cae5511a231b8eb02f2b22`.
  Only its test-local x64 DLLs are copied, never deployment/system scripts.
  The host refuses a system `opengl32.dll` or non-llvmpipe renderer.
- Every Windows Firewall profile must be enabled; a verified outbound block
  must cover each exact child image **before staging any original DLL**.
  Failure aborts. The launcher constructs an allowlisted child environment,
  creates it suspended, assigns a kill-on-close job with an active-process limit
  of one, then resumes it with a 30-second bound.
- Original bytes are imported as **`BsiPotatoOriginal.dll`**, not deployed as a
  player/system-library proxy. An existing export is the anchor, but our code
  never calls it or an initializer. Normal Windows DLL attachment runs the
  original initialization. This is not original-name/player startup acceptance.
- Each case is a fresh child; the proxy is never unloaded beneath live hooks.
  Exiting a process is not an in-place Off test. Original DLL files are removed
  after each variant, hashes remain unchanged, and private data is deleted at
  workflow completion.
- These controls are **not a complete hostile-code sandbox or safety
  certification**. The VM is disposable, never self-hosted. Do not relax the
  block or run the experiment on a personal/player installation to make it pass.

### Retrieved evidence

[Sanitized JSON artifact 14711531576](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44915621801/artifacts/14711531576):
1,633 compressed bytes, SHA-256
`afa3b30c3913cf76bbccfc07b781664499d333b46eed298e8bcf29eb1e911570`.
The independent retrieval used the **complete check annotations**, not an assumed
artifact download. Summary plus paired rows stay below GitHub's per-step count
and 4,096-character message limits; every pair is capped below 3,800 characters.
Only known hashes, booleans, numeric values, pixel channels and parent-owned
exit/timeout/containment fields are exported. No original binaries, raw child
reports/logs, module paths or process dumps are uploaded.

### Attempt ledger (failed attempts are not erased)

| Windows run | Head | Result |
| --- | --- | --- |
| [44914131709](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44914131709) | `253e3fa` | MSVC hosts built; data preparation refused; no original execution |
| [44914376767](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44914376767) | `5c0122f` | Drive returned nonmatching bytes; refused before execution |
| [44914638213](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44914638213) | `96dbf73` | Original archive/member hashes passed; prior Mesa WGL package refused before execution |
| [44914982007](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44914982007) | `c8db701` | Original code ran and mip pixels changed; import-export bind assertion failed; per-row annotation count capped retrieval |
| [44915382019](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44915382019) | `29ee08c` | All strict execution checks passed; one large annotation was truncated, not treated as complete per-case data |
| [44915621801](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44915621801) | `d2080aa` | **All 14 children passed; all individual measurements retrieved and checked** |

Drive's fixed-file confirmation is handled with cookies and its fixed-ID form,
without page-script execution, credential acquisition or sharing changes. Six
synthetic no-network tests cover confirmation, wrong-file/host refusal, hashes
and no writes on rejected bytes. The prior MSVC Mesa package lacked the required
provider; upstream [26.2.7 notes](https://github.com/pal1000/mesa-dist-win/releases/tag/26.2.7)
flag the MSVC/32-bit software-rendering build issue and recommend x64 MinGW.
The supplied archive and all five DLL byte sequences remain unchanged.
