# GIF RPC (RPC-only app-local proxy)

The new **GIF RPC** page is separate from the existing Discord Application-ID
binary patcher. It builds and deploys a clean x64 `version.dll` with configurable
presence, not the supplied optimizer/overlay DLL. There is no RAM cleaner,
priority/VSync tweak, keyboard/window hook, custom Discord SDK ABI or downloaded
runtime DLL. Windows version APIs forward to the real **System32** library via
an absolute system path. The proxy runs RPC only in `HD-Player.exe`.

## Use

1. Close every HD-Player instance. Open GIF RPC and select the actual installed
   `HD-Player.exe`, then **Check DLL / Load settings**.
2. Enter your Discord application ID, state, up to ten rotating details, image
   asset keys or HTTPS URLs, optional tooltips/buttons and elapsed time.
3. Install/apply. The customizer checks the bundled DLL's PE/DLL header, x64
   architecture, schema marker and forwarding exports before deployment.
4. Start Discord Desktop, then the emulator. Its build must load an app-local
   `version.dll`. This tool does not inject into a running process, add remote
   threads, modify Windows/System32, or force the emulator to load it.

The GIF title is a user-facing feature name, not a guarantee that Discord renders
an animated image. GIF/external-asset support, application ownership, asset
availability, client settings and server acceptance determine the final display.
Buttons may not be visible on the same account that owns the presence. No live
Discord account/session is exercised in CI.

## Configuration and safety

RPC settings live in `bsi-gif-rpc.ini` next to the deployed DLL. ASCII hex encodes
UTF-8 text, avoiding Windows INI code-page/quote ambiguity and fixed-size binary
string slots. The customizer edits these settings, rather than guessing pointers
inside an unrelated DLL. No account tokens are stored. IPC is local to Discord's
named pipes; strings are JSON escaped, frames are bounded and retries are paced.

Unknown existing `version.dll` files are protected by default. Explicit
replacement requires a preserved, verified `version.dll.bsi-gif-rpc.bak`; a
conflicting older backup is never overwritten. Removal/restoration touches only
our validated proxy and preserves the saved configuration/backup. Do not deploy
to the Windows directory. A third-party DLL can contain functionality the clean
proxy does not preserve: review the replacement checkbox carefully.

The worker does not join threads or perform IPC in DllMain. The proxy resolves
the absolute System32 library during process attach, before dependent DLL
initializers can call its exports. It pins its module before starting its worker,
has a stop event and reconnects when the app ID changes. API exports are x64
tail jumps, not C++ wrappers. Once loaded it must be
reconfigured with HD-Player stopped; restart the emulator after editing.

## Installer diagnostics

Version 2.6.3 enables Inno setup logging and explicitly gates supported x64
Windows architectures. `/NOLAUNCH` suppresses the post-install launch for tests
and troubleshooting. Default installation and silent self-updates still launch
normally. To capture a failure:

```powershell
.\BSI-Customizer-2.6.3-setup.exe /LOG="$env:TEMP\BSI-setup.log"
```

Send the exact error and the relevant log tail (remove personal paths if needed).
A "setup fails" description alone does not identify the original failure.

On disposable GitHub-hosted Windows runners only (never a self-hosted user PC),
the workflow tests actual install, installed-file SHA-256, administrator
manifest, reinstall/upgrade and uninstall, rather than only verifying the
installer's PE header. This does not substitute for interactive/UAC testing on
the user's Windows configuration.

## V2 startup compatibility repair

V2 adds the missing `GetFileVersionInfoByHandle` export and explicit Windows
version.dll ordinals 1–17; the custom schema export is isolated at ordinal 1000.
V1 had allowed the linker to allocate ordinals alongside that extra export,
which is not a compatible contract for clients importing Windows APIs by ordinal.
This is a concrete proxy defect, not yet proof of the reported PC's crash cause.
V1 is still recognized for safe removal/upgrading.

The RPC worker now uses the CRT-aware thread entry point, catches C++ exceptions,
and delays its IPC work until after initial player startup. A bounded
`bsi-gif-rpc.log` beside the DLL records that the worker started. No credentials or
activity text is logged. Forwarded Windows API calls are not delayed.

Windows selftests additionally launch an isolated `HD-Player.exe`-named test host
against the deployed proxy. It checks all named exports and ordinal identities
against System32, makes real version-resource queries, parses an enabled INI,
and survives worker startup/normal exit. This is not a live emulator boot or
Discord GIF acceptance test. Leave the old DLL removed until testing the V2 build.


## V3 forwarding repair (not yet live-emulator certified)

The released V2 still crashes on the reported PC; its isolated test was not
proof of emulator compatibility. V3 removes typed wrappers (including the guessed
undocumented `GetFileVersionInfoByHandle` prototype) and uses MASM x64 tail jumps
for all 17 exports. Registers, stack arguments, return values and last-error
semantics are left to the original Windows implementation. It removes lazy
`InitOnce`/`LoadLibrary` resolution from forwarded calls, eliminating that
additional lock order when calls occur in another DLL's initializer. The fixed
ordinal contract remains 1–17, with schema at 1000.

The module is pinned **before** a worker address is handed to another thread,
not after it starts. The RPC thread reserves its own 1 MiB stack instead of
inheriting an arbitrary host default. Process-attach filename storage is reduced
from 64 KiB to MAX_PATH; an overlong host path safely skips RPC but still forwards
Windows APIs. V3 deployment validation additionally checks that named export
addresses reference executable, file-backed code. V1/V2 recognition is retained
for removal and upgrades.

The Qt-free Windows workflow builds the same targets as the main app and tests:
static imports before main on a 128 KiB stack; API calls from a dependent DLL's
DllMain; concurrent forwarding/error parity with System32; a fifth stack-passed
argument; FreeLibrary while the worker may still be pending; enabled/disabled
INI settings and worker startup. These tests do **not** boot BlueStacks/MSI or
exercise live Discord. The exact reported emulator version and its Windows
Application Error event (faulting module, exception code, offset), or a crash
dump, are still needed to confirm the reported failure is resolved. Keep the
known-crashing DLL removed until a replacement is tested on that installation.
