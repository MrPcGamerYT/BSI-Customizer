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

The worker does not join threads or perform IPC/LoadLibrary in DllMain. It pins
its module for the host's lifetime, has a stop event, reconnects when the app ID
changes and forwards through guarded function pointers. Once loaded it must be
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
