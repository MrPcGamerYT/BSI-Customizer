# Supplied Free Fire Potato DLL: static review

**Status: analysis only. No supplied DLL was executed, installed, patched, or
incorporated. No Potato preset has been added and the reported NVIDIA startup
failure has not been reproduced or fixed.** This review does not certify these
binaries as safe. The regular `BSIShaders` runtime and native Settings
behavior are unchanged. Subsequent independently authored sampler-policy work
is described in [the portability experiment](TEXTURE-QUALITY-PORTABILITY.md);
that source does not turn these binaries into an installed or verified feature.

## Input and integrity

User-supplied reference:
[`POTATO FF DLL 7FARFF.rar`](https://drive.google.com/file/d/1QGBvjTpW0mRNPwv2eqj77UEsLQtNDV7Y/view).
Downloaded through the user's connected Drive, with read-only operations.

- Archive size: **344,442 bytes**.
- SHA-256: `80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17`.
- The downloaded SHA-256 and MD5 matched Drive's metadata.
- Seventeen archive entries: eleven directories and six encrypted DLLs;
  **1,465,344 expanded file bytes**, five unique DLL byte sequences.
- All six are Windows **x64 PE DLLs**. None has a populated PE certificate table;
  that fact alone is neither a malware finding nor a safety assurance.
- **No C/C++ source, Visual Studio solution/project, build instructions, or
  license is included.** Product metadata says `geanswag massinha`; it is not
  evidence that the uploader authored the binaries or granted redistribution.

Archive members were checked for path traversal, symlinks and size limits, then
read as data into hash-named, non-executable `.bin` files under the ignored
`.cache/shader-build/potato-review/` directory. The supplied DLLs were never
passed to `LoadLibrary`, Wine, a player, or a DLL registration tool. PE imports,
exports, sections, constants and x64 instructions were parsed with pefile and
Capstone. UnRAR was built as an analysis tool from upstream mirror commit
[`cacdcb8cbe1813b6151e3f92f2ccec7afe4d6a41`](https://github.com/pmachapman/unrar/commit/cacdcb8cbe1813b6151e3f92f2ccec7afe4d6a41);
no archive member or bundled script was used as an analysis tool. Extracted
binaries, disassembly and the archive are not Git/release deliverables.

## What the effect actually does

The inspected hook path uses desktop **OpenGL/WGL**, rather than a screen-space
Potato shader. It hooks `wglSwapBuffers`, `glBindTexture` and several draw calls
(`glDrawArrays`, `glDrawElements` and range/instanced variants). The active path
forces texture/sampler level-of-detail settings before game draw calls.

Paths below are relative to the archive's `POTATO FF DLL 7FARFF/` directory.

| Archive path | Forced LOD | Parameters in the inspected path | Identity |
| --- | ---: | --- | --- |
| `Intel/Bajo/dinput8.dll` | 1.5 | LOD bias; legacy texture-environment bias when no program is bound | A |
| `Intel/Fuerte/dinput8.dll` | 3.0 | Same mechanism as A | B |
| `Nvidia  AMD/BS MSI 4/forte/dinput8.dll` | 3.0 | **Byte-identical to Intel/Fuerte** | B |
| `Nvidia  AMD/BS MSI 4/leve/dinput8.dll` | 1.5 | LOD bias and minimum LOD | C |
| `Nvidia  AMD/BS MSI 5/forte/version.dll` | 3.0 | LOD bias and minimum LOD | D |
| `Nvidia  AMD/BS MSI 5/leve/version.dll` | 1.5 | LOD bias and minimum LOD | E |

The values are observable floating-point constants, not inferred from folder
names alone. For example, the MSI 5 pair has identical `.text` bytes and uses
its 3.0/1.5 constant at RVA `0x1f518`. Its routines at RVAs `0x1000` and `0x11d0`
set `GL_TEXTURE_LOD_BIAS` (`0x8501`) and `GL_TEXTURE_MIN_LOD` (`0x813a`). The Intel
pair also has identical `.text` bytes, with the 1.5/3.0 constant at `0x1f458`;
its texture path sets bias, not the additional minimum-LOD parameter.

Other observed behavior:

- The bind/draw path changes the application's own texture/sampler objects. It
  restores the active texture unit, but does **not** restore those objects'
  original LOD values after each draw.
- Draw interception scans up to **eight texture units**, checking 2D, 3D and
  cube-map bindings plus sampler bindings. Tracking uses two process-global
  65,536-byte arrays, cleared on intercepted swaps. These global arrays are not
  separated by GL context/share group.
- Initialization disables dithering, fog and legacy polygon/line/point
  smoothing, and sets several `GL_FASTEST` hints. These are not equivalent to
  reducing a modern game's geometry, shadows, draw distance or render size;
  some legacy calls are invalid in a core-profile context.
- The Intel path also calls `glGetError` after some operations, consuming GL
  error state belonging to the host application.
- The hook path does not establish ownership of an identified game window
  before installing/applying these changes. Shared process GL state and
  initialization flags can therefore affect a UI/other context as well.
- Driver DLL names are used as fallback symbol-resolution candidates. Finding
  `nvoglv64.dll` does not establish compatibility with a specific NVIDIA
  profile. No NVIDIA Profile Inspector/NVAPI profile inspection was identified.
- No DirectX or Vulkan effect path was identified. The large GL extension-name
  table includes Vulkan/EGL-related names; those strings alone are **not**
  evidence that this effect implements either backend.

### Performance interpretation

Positive LOD can select coarser **existing mipmaps**, reducing texture detail
and potentially texture bandwidth/cache pressure. It is not just pixelating a
finished frame. However, it does not by itself downsize the allocated texture,
remove geometry, lower the emulator's render resolution, or reduce every
shader's work. Textures without a usable mip chain need separate handling.

The repeated GL binding queries, texture-unit switches, per-object changes and
128 KiB of tracking-array clearing per intercepted swap also have CPU cost.
Consequently, neither the code shape nor the tutorial proves an FPS gain on a
particular player, renderer, profile or GPU. No FPS/latency measurement was made.

## The reported NVIDIA Profile Inspector startup conflict

The friend's report is a useful reproduction lead, **not a confirmed diagnosis**.
A profile's forced texture LOD/filtering, anti-aliasing or other driver settings
can interact with application state and graphics hooks, but the DLL bytes do
not reveal which profile/settings were applied on that PC. Profile Inspector is
not inherently incompatible with every graphics DLL.

There are also independent startup risks in the supplied implementation:

1. These are **system-library proxies**, not optional regular QML modules.
   Their process-attach path loads the real system library, resolves forwarding
   exports and starts a hook thread. A required resolution failure returns
   failure from initialization and can prevent a dependent process from loading.
   The proxy's imported runtime dependencies must also resolve.
2. Hook/GL initialization is process-global rather than validated per eligible
   game context. Extension function pointers and object identities are not
   guaranteed to be interchangeable between unrelated contexts.
3. Host texture/sampler state is persistently overridden, including before draw
   calls, without a driver-profile compatibility negotiation or full rollback.

These findings justify **not adopting the proxy design**. They do not prove
which instruction failed on the friend's machine. No driver profile was reset,
no registry was changed and no user graphics settings were overwritten.

Needed to investigate the specific failure:

- Export of the **affected NVIDIA profile** as `.nip`, including the executable
  association and any relevant global overrides (not an unrelated full backup).
- Exact MSI/BlueStacks version, selected game renderer, GPU and driver version.
- Existing startup log and Windows crash event/dump information, preferably
  faulting module, exception code and stack. Redact account details and personal
  paths. Do not run an untrusted DLL again just to produce this evidence.

A profile export alone is insufficient to certify a fix: an independently built
implementation must be tested with that configuration on the affected hardware.

## Source and integration direction

A binary archive cannot supply the **exact original Visual Studio source**.
Disassembly is not a recovered, maintainable project or a redistribution
license. Do not copy the supplied DLLs, transplant their instruction bytes, or
add a cosmetic preset advertised as a performance optimization.

A separately authored texture-quality feature could implement the documented
LOD behavior, but it needs its own design/tests rather than reusing the current
final-frame effect pass:

- Keep a regular `BSIShaders.dll`; no `version.dll`/`dinput8.dll` replacement,
  driver patch, registry write or automatic NVIDIA profile reset.
- Default off, with bounded light/strong settings and the **same native
  Save/Discard/close semantics** as existing controls. Merely selecting a draft
  must not touch rendering. Preserve the ten existing sliders and their Reset
  behavior, tab geometry and palette.
- Identify the actual game renderer/window/context. Apply only to supported,
  verified mipmapped color sampling, not Qt/UI, depth/integer/comparison or
  otherwise unqualified resources. Unsupported APIs remain explicit bypasses;
  a WGL implementation is not DirectX/Vulkan/GLES support.
- Preserve host sampling state and object/context lifetimes, avoid additive
  stacking with existing LOD overrides, and do not scan every texture unit on
  every draw when a bounded state/lifecycle cache can safely suffice.
- Treat driver overrides as external state. Ordinary GL sampler queries cannot
  prove that every driver-profile override is absent. Do not claim universal
  conflict detection or silently force a fallback renderer. Validate the
  affected profile; bypass combinations that cannot be qualified.
- Keep optional initialization out of loader lock, and let the original
  renderer continue when feature prerequisites/hook setup fail. A failure is a
  bypass diagnostic, not an active/processed-frame success.
- Test actual mip selection, state restoration, no-mip/unsupported rejection,
  context recreation, off/on transitions and resource bounds. Then compare
  frame times and image quality on the **real** player with/without the profile.
  Software fixtures alone cannot certify NVIDIA driver compatibility.

**This is the next implementation boundary, not an implemented Potato mode or
an NVIDIA-conflict fix.** The existing final-frame Shaders controls remain
independent of this analysis.

### Existing Visual Studio-buildable shader source

The repository already has independently authored C++ source and a standalone
CMake project at `src/shaders/CMakeLists.txt`. The following generates a Visual
Studio solution for the **existing** regular shader module; it does **not**
recover the supplied proxy source or add its texture-quality effect.

From an x64 Visual Studio 2022 developer PowerShell, with CMake and the matching
release x64 Qt SDK installed, substitute your Qt path:

```powershell
$qt = "C:/Qt/5.15.2/msvc2019_64"
$env:PATH = "$qt/bin;$env:PATH"
cmake -S src/shaders -B build-shaders-qt5 -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$qt" `
  -DBSI_SHADERS_QT_MAJOR=5 -DBSI_SHADERS_BUILD_TESTS=ON `
  -DBSI_TEST_VULKAN=OFF
cmake --build build-shaders-qt5 --config Release
ctest --test-dir build-shaders-qt5 -C Release --output-on-failure
```

For the Qt 6 public-API baseline, use a **separate** build directory and the Qt
6.2.4 MSVC x64 SDK with `-DBSI_SHADERS_QT_MAJOR=6`. Do not mix Qt major versions,
substitute the Customizer's newer SDK for a player baseline, or overwrite the
player's runtimes. The example disables the Vulkan *pixel test* when no working
ICD is available; it does not validate Vulkan or disable that backend's source.
The separate software-driver CI job must run those pixel tests.

The module is written to `build-shaders-qt5/module/BSIShaders/BSIShaders.dll`.
Building it is not installation approval: use the reviewed package/ABI checks
and regular install/remove flow in [the integration guide](SHADERS-INTEGRATION.md).
The real-player verification requirements remain in
[the user test guide](SHADERS-USER-TEST.md).

## DLL SHA-256 identities

| Identity | SHA-256 |
| --- | --- |
| A | `25da83f0c7f5d6052e67e279cb427d20643593321405f017331ebd450e5fdcf8` |
| B | `c956f2bd04b22212ecb8e65a0397c40addf64be136d8d1cd906e473661113554` |
| C | `289e1eb34e815b886f4c2585c37efc78c489f58fd7b28942cfd5aeb1ec38b282` |
| D | `7440db72c09af59e8a3249e17fff0a1e964e4409bc6ec7b9c1686b35368fac22` |
| E | `b68e5b3586ffd2a57c680a0426818e71b06eadc3ce60b80507f42679892b3558` |
