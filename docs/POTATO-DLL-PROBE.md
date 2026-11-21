# Original Potato DLL runtime experiment

This is a **standalone experiment, not an emulator feature, installer or game
fix**. It is distinct from `dll-payload-test.yml`, which exercises the unrelated
Z-Lag startup payload. No original Potato binary, archive, disassembly or Mesa
binary is committed or included in release/test artifacts.

## Why this experiment exists

The installed-player report remains negative: 1,235 processed frames, 2,468
inspected draws, all 2,468 rejected first because depth testing is disabled,
zero eligible/prepared calls or overridden bindings, and zero nested inspected
calls. OpenGL grading works. The earlier 27,620-rejection report was already
confirmed to be advancing gameplay with Settings closed; it must not be
reinterpreted as a menu/pause report.

The [static review](POTATO-DLL-REVIEW.md) found a material mechanism difference:
the supplied proxies persistently mutate texture/sampler LOD on binds/draws,
without the independent implementation's depth qualification. Static analysis
is not execution, and testing our independent sampler code or Z-Lag does not
test the supplied Potato bytes. This experiment runs the **actual** original
files on explicitly test-owned mipmapped resources to separate those claims.

## Execution boundary

- A manually opted-in `CI` dispatch with `test_potato_dll=true` calls the
  separate `potato-dll-probe.yml`. Both payload opts default false and are
  mutually exclusive. Push/PR/default tests only inspect safety contracts;
  they do not execute original supplied code.
- A fresh GitHub-hosted Windows 2022 x64 runner builds our small MSVC C++17 host,
  with no Qt, player, game, account, Customizer module or physical-driver/profile
  dependency. No secrets are passed; checkout does not persist credentials.
- The original archive SHA-256 is pinned to
  `80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17`.
  Seventeen entries, paths, symlinks, expanded sizes, all six original members,
  five unique DLL hashes, AMD64 headers and native import anchors are checked
  **as data**, before making any original DLL loadable.
- The software WGL provider is Mesa's MinGW x64 26.2.7 release archive, SHA-256
  `19c74aad7b09e682473b8de4c35b47e6498de97b71cae5511a231b8eb02f2b22`.
  Only its test-local x64 DLLs are copied, not deployment/system-edit scripts.
  The host refuses a system `opengl32.dll` or non-llvmpipe renderer. The prior
  MSVC package was refused for missing WGL-provider data; the upstream
  [26.2.7 notes](https://github.com/pal1000/mesa-dist-win/releases/tag/26.2.7)
  describe an MSVC/32-bit software-rendering build problem and recommend x64
  MinGW. This changes the test dependency, not any original Potato bytes.
- Before staging an original DLL, all Windows Firewall profiles must be enabled
  and a verified outbound-block rule must cover each exact child image path.
  Failure aborts before execution. The launcher constructs an allowlisted
  environment, creates the child suspended, assigns a kill-on-close job with
  an active-process limit of one, then resumes it with a 30-second bound.
- The exact original DLL bytes are imported as **`BsiPotatoOriginal.dll`**, not
  dropped into a player as `version.dll`/`dinput8.dll`. An existing native export
  is the import anchor; our code **never calls that export or an initializer**.
  Normal Windows DLL process attachment starts the original initialization.
  This renamed-host test is not proof of original-name/player startup behavior.
- Each case is a fresh child. Original hooks are not unloaded beneath running
  callbacks. Killing/exiting a process is not an in-place Off/rollback test.
  The original DLL is removed from its test directory after each variant.
- Per-image firewall rules, job limits and an environment allowlist are useful
  controls, **not a security certification or a complete hostile-code sandbox**.
  The VM is disposable; no self-hosted runner is used. Do not run this on a
  personal/player installation or relax the block to obtain a passing result.

## Independent evidence stages

Twelve fresh children exercise the five original identities and a no-payload
negative control, each with texture-object sampling and a bound sampler:

1. Normal import loading (with no anchor call), separately from child exit.
2. A native red mip-zero baseline before any swap.
3. Calls to the real local Mesa `wglSwapBuffers` entry, giving the original
   worker time to install its real hooks. Read-only bounded jump inspection
   records whether swap/draw/bind entries lead into the loaded original image;
   it neither installs hooks nor copies/transplants their machine code.
4. A draw with **depth testing disabled**, sampling a 64×64 color-coded mip chain
   through a real GLSL program. Before/after parameters and readback pixels
   distinguish a configured value from actual coarser-mip selection.
5. A repeated draw without a new bind/swap records persistent object mutation;
   a new test texture separately records bind-time LOD before its first draw.
6. Texture/sampler bias and minimum LOD are checked against the independently
   recorded static expectations (1.5/3.0; bias-only versus bias + minimum LOD).
   Original GL errors are reported separately, not consumed/hidden as success.

Two further fresh **no-original-DLL** children try a reversible alternative,
once with a texture-object source and once with an existing sampler source.
They use the independent `planGlTextureQuality` policy on the exact mip chain
created by this host, **with depth testing still disabled**. Light/Strong private
samplers are bound only for the explicitly known draw; the exact original
binding restores before the next Off draw. Each source mode repeats 32 on/off
cycles, checking source texture/sampler descriptors, active unit, other bindings,
program, FBOs, VAO, viewport, depth/blend/cull state, native red Off pixels and an
unrelated sampler sentinel. This is another actual rendering approach, not a
mock of the original DLL or a weakening of production scene qualification. The
proof booleans refer only to authored fixture resources, never the unidentified
resources in the player's 2,468 rejected calls. Its outcomes are reported
separately from original loading/hook/pixel outcomes.

Only selected booleans, numeric measurements, known hashes, pixel channels and
parent-owned child exit/timeout/containment status are uploaded as sanitized
JSON. No original binaries, process dumps, module paths, raw child reports or
private logs are uploaded. Failed cases remain failures; the experiment does
not replace a missing measurement with inferred success.

## Results

The initial implementation passed the non-executing data-integrity checks,
contract tests, strict x64 Windows cross-compilation and control-host link. In
Windows run [44914131709](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44914131709)
(head `253e3faa6c6931f9e20493e54b96b9b1d1accf0b`), all three MSVC host
builds succeeded, but data preparation failed **before the execution step**.
This is not original-DLL loading, hook or pixel evidence. Raw log retrieval
from the restricted workspace failed; a follow-up publishes small sanitized
preparation errors and measurement rows through GitHub check annotations as
well as the JSON artifact. No original archive was re-uploaded or made public;
the retried archive URL is its existing webContentLink, re-read from Drive
metadata and still constrained by the same hash.

Run [44914376767](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44914376767)
(head `5c0122f1a212d50fad8950010f3d614a191fe544`) again built all three hosts,
but the original download returned nonmatching bytes and was refused, before
any DLL was staged/executed. The next data-only correction handles Drive's
fixed-file confirmation form/cookies without executing page scripts or changing
permissions. Six synthetic no-network tests check that form, wrong-file/host
refusals, exact hash requirements and no writes on rejected data.

Run [44914638213](https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/actions/runs/44914638213)
(head `96dbf73268614d922cc961c448de91b672a674f9`) passed those six synthetic
tests, all MSVC builds/PowerShell parsing, the confirmed original archive
SHA-256/inventory and all member hashes/PE-anchor checks. Preparation then
refused the prior Mesa package's missing unique x64 WGL provider; execution was
again correctly skipped. The pinned dependency is now the release's recommended
x64 MinGW build. Actual DLL and independent rendering outcomes follow here
only after execution.

Even a completely passing authored-host result cannot establish which player
contexts/programs/textures produce the real scene, authorize unrelated/UI
resources, prove NVIDIA profile compatibility, reduce allocated texture memory,
measure FPS or certify a game fix. In particular, it is not permission to delete
production depth/protection/ownership guards merely to raise counters. A viable
independent alternative must combine identified scene-resource ownership with
bounded draw-scoped private samplers and exact Off restoration, not blindly
replicate the original's persistent process-wide mutation.
