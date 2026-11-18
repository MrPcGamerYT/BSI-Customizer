# Supplied Z-Lag archive: compatibility review and test boundary

This is a review of the **new compiled payload ZIP**, not the four source
attachments the user previously withdrew as the wrong DLL sources. It is not a
safety certification, license-service approval, or proof of working effects in
an emulator. No payload binary is added to BSI's application resources.

## Exact inputs

User-supplied location:
<https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/blob/main/SCibJBt4nEzcDlU-hNCvTrHLOvpztldpglbqX9tGh8M.zip>

The branch link can change; tests fetch the exact Git blob below and refuse
changed archive/DLL bytes.

| Item | Value |
|---|---|
| Git blob | `980007812e078635ddc2569caf4d885459ecb104` |
| ZIP bytes | 1,457,358 |
| ZIP SHA-256 | `48289b241b789c4cdc0e553e84d0af4eb1cb3afa73b657698256ea5fdb4687c3` |
| DLL member | `dist/Z-Lag.dll` |
| DLL bytes | 1,135,230 |
| DLL SHA-256 | `80fa1503e6071777ff3ba9daabfe27918bbcad94fb1b4726ca88dbdb533be296` |
| Machine | AMD64, PE32+ (`0x8664`) |
| Only named export | `ZLagImportAnchor` |
| Export location | executable `.text`, RVA `0xa0da0`, not a forwarder |

The supplied DLL has a valid PE checksum and no Authenticode certificate
entry. Its normal direct imports are `ADVAPI32.dll`, `GDI32.dll`, `KERNEL32.dll`,
`msvcrt.dll`, `OPENGL32.dll`, `USER32.dll`, and `WINHTTP.dll`. No delay-import
directory was found. This is **not** exhaustive transitive/dependency resolution.

The ZIP contains only an x64 DLL/build description. The Makefile defaults to
`x86_64-w64-mingw32`, `-march=x86-64` and the 64-bit MinHook disassembler. The
quoted claim that both architectures were delivered/tested is not established
by this archive. The BSI startup importer intentionally supports AMD64 only.

## What to enter in BSI

For these exact bytes, in **PATCHER → DLL patcher**:

- **DLL filename:** `Z-Lag.dll`
- **Existing native function export:** `ZLagImportAnchor` (case-sensitive)

Put the actual DLL beside the disposable `HD-Player.exe` first, then inspect
both files and review the hashes/risks. Do not enter the ZIP, `.dll.a` import
library, or source files. The generic `zlag_init` default is **not** exported by
this DLL and must be refused; the payload test checks that refusal.

Windows resolves the anchor but does not call it. `src/menuown.cpp` starts a
worker from `DLL_PROCESS_ATTACH`; that worker accepts only an importing process
whose basename is `HD-Player.exe`, initializes logging and attempts HWID
verification before loading settings or installing presentation hooks.

## Findings that affect real use

1. **Automatic persistent-identifier disclosure.** On each accepted-host load,
   `src/hwid_auth.cpp` reads `GetCurrentHwProfileA`'s hardware-profile GUID and
   sends it in an HTTPS request path to an embedded Firestore project. There is
   no separate consent prompt before that request. Review ownership, permission
   and disclosure before enabling this on a real user's machine. No API key,
   project credential, or actual device identifier is reproduced in this guide.
2. **Authorization gates the effects.** A request failure/non-200 response makes
   the worker show an authorization message and return without installing the
   hooks; `DllMain` still returns success. Thus **loaded != authorized != effects
   working**. The provided check accepts any HTTP 200 without validating a
   license field/body and does not set an explicit WinHTTP timeout. Tests do not
   change, mock, bypass or contact that license service.
3. **Renderer integration remains target-specific.** The worker installs local
   MinHook presentation hooks for WGL/EGL; shader/UI setup needs a compatible
   context on the presentation thread. No compatible presentation path means no
   visual-effects proof. A successful compile/import or headless startup test is
   not a GPU/driver/emulator result. Default settings include master on, flow 1,
   vsync-off and auto-open; this is not a side-effect-free diagnostics DLL after
   authorization.
4. **Optional tools still have effects beyond drawing.** Optimizer/debloat,
   registry/power configuration, temporary-file cleanup and ADB package actions
   remain in the source. They are not exercised by the offline loader test; a
   "clean" label is not an audit of these actions, their reversibility or their
   suitability for a particular installation.
5. **No general hot-unload contract was established.** Detach sets the shutdown
   flag; the entry path does not join its worker or perform a documented explicit
   unhook/shutdown handshake. Test ordinary process lifetime, shutdown and
   repeated launches, not arbitrary `FreeLibrary` hot-unloads. Creating a worker
   in `DllMain` is not a blanket loader-lock/lifecycle safety guarantee.
6. **Source/binary identity and redistribution are separate.** The binary hash
   pins what is tested, but this review does not claim a reproducible build from
   all supplied sources. Preserve third-party notices and establish the payload's
   distribution permissions independently; BSI does not bundle or rewrite it.

## Automated test design

`tests/dll_startup_payload_test.py` is **data only**. It pins the ZIP/DLL hashes,
checks bounded archive paths/size, verifies architecture/executable export and
uses the production native transaction adapter on disposable copies of the four
pinned player references. Independent `pefile` decoding checks exact imports,
entry point, original sections/overlay/certificate bytes and checksum. Recovery
is exercised through the production API with the DLL withheld, verifying the
byte-exact original and retained backup/journal. The test can also prepare an
original probe host; it never launches a player or DLL itself.

Normal push/PR CI runs only the harmless existing loader fixtures, parser,
transaction, UI and static safety-contract tests. Supplied-DLL execution is
**off by default**. Explicitly dispatch CI with `test_supplied_dll=true` to run
`.github/workflows/dll-payload-test.yml` instead of the standard CI jobs:

```sh
gh workflow run ci.yml --ref arena/79d74795-bsi-customizer -f test_supplied_dll=true
```

That opt-in job builds our **original probe**, not a replacement emulator or a
linked payload example. The production patcher adds the actual anchor import,
exits, and only EXE + DLL are copied to a recipient directory. Before any supplied
code executes, the disposable Windows runner enables/verifies its firewall and
blocks outbound access for that probe. Three normal launches check the exact
loaded module path and its pre-authorization startup-worker log marker, with
interpreter/SDK/Qt locations removed from PATH. The probe never calls the anchor
or an initializer. Raw DLL logs/device IDs and binaries are not uploaded; only
sanitized JSON evidence is retained.

**A workflow definition is not a passed test.** Check the exact-head job and its
JSON evidence. Even a green offline job proves only normal loading/worker entry,
not Firestore authorization, rendering, optimizer safety, emulator startup, or a
full recipient installation. Those require an authorized disposable Windows
emulator installation and a legitimate license/compatible renderer. Do not
weaken the user's conditional test-before-merge gate because generic or offline
checks pass.
