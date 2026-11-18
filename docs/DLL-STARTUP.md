# Regular DLL on normal EXE startup (no proxy)

## What this feature does

**PATCHER → DLL patcher** adds one explicitly chosen, ordinary **AMD64 DLL**
as a required PE dependency of the selected AMD64 `HD-Player.exe`. Expand the
**PATCHER** sidebar category and click **DLL patcher**. Alternatively, use
**Open DLL patcher** at the top of **COLLECTION**, above the skin packs. Both
links open one dedicated editor; the form is no longer buried below the packs.
The optional Chromium collection opens the **same native page**. No automatic
file inspection, patching or DLL execution occurs on page entry.

The new implementation is original C++ code, compiled into BSI. It does **not**
embed or run the uploaded Python/JavaScript/PowerShell patchers, impersonate a
Windows DLL, inject into an existing process, change the EXE entry point or
install a recurring launcher. The separate GIF-RPC `version.dll` proxy is
unchanged. The uploaded scripts' safety problems remain documented in
[their review](DLL-STARTUP-REVIEW.md); they are not the shipping writer.

After patching, Windows loads the named DLL on each fresh, ordinary EXE launch.
**Recipients do not need Python, Node, PowerShell, a patch script or BSI running.**
They do need the **patched EXE plus the named DLL and all that DLL's compatible
dependencies**, alongside the rest of a compatible emulator installation. A
standalone copy of HD-Player.exe is not a complete emulator distribution.

**Loading is not the same as calling an exported function.** This writer imports
one exact, case-sensitive existing native function export (default `zlag_init`).
Windows resolves its address but **does not call it**; normal DLL initialization
(`DLL_PROCESS_ATTACH`/the DLL entry point) runs instead. DLLs with no exports,
null/out-of-bounds function addresses, data-only selected exports and selected
forwarders are refused before apply. This is a deliberate supported-layout
constraint, not a proxy requirement.
A DLL that requires an explicit `zlag_init()` or another exported initializer
will need its own supported initialization design; this tool does not invoke
that function. DLL initialization runs under loader-lock constraints. A generic
loader cannot certify that arbitrary DLL initialization is safe.

## Use and distribution

1. Work on an authorized disposable installation first. Keep an independent
   known-good installation/backup; changing a commercial emulator may violate
   its terms or prevent updates/signature verification.
2. Close **every HD-Player instance**. Select the actual `HD-Player.exe` you launch
   on Home/Settings, not another installation's EXE.
3. Put your **trusted AMD64 DLL beside that EXE**. BSI deliberately does not
   download, load, copy, rename, modify or remove payload DLLs. Enter just its
   ASCII filename (default `zlag.dll`), not a path, and an exact existing native
   function export (default `zlag_init`). Filename/export changes remain
   available; restoring the old import is required before choosing another DLL.
4. Click **Inspect EXE + DLL**. Review the EXE/DLL SHA-256 values, dependency
   information and risks. The dependency list is normal, direct imports only,
   **not** a complete transitive/delay-import/API-set dependency resolution test.
5. Click **Review & add import**, explicitly acknowledge the warnings, then
   confirm. The native engine rechecks both hashes. It reports file verification,
   not successful player execution or payload functionality.
6. Test normal startup/shutdown, repeated launches and your DLL's actual behavior
   in that disposable installation. Only then distribute compatible EXE/DLL
   files under the permissions/licences applicable to both. Keep recovery
   snapshots local; recipients do not need them to launch.

A missing/incompatible DLL, missing dependent library or failed DLL initialization
can prevent **the entire player** from starting. Modifying the EXE invalidates
its Authenticode signature even though the PE checksum is repaired. Updates can
replace the modified EXE. Do not run an untrusted DLL to test this feature.

## Recovery and safety boundaries

- The planner only parses data, with bounded headers/RVAs/sections/import
  descriptors/thunks/strings. It rejects wrong architecture, EXE-as-DLL,
  malformed/null/wrong-case exports, overlapping/truncated sections, unsupported
  bound imports and occupied new
  section-header slots. Files are limited to 128 MiB. Unsupported layouts fail
  closed; arbitrary PE32/ARM64/system EXEs are not a supported target promise.
- New import descriptors live in a writable, non-executable `.bsidll` section.
  Existing descriptors, section contents, overlay/certificate bytes and entry
  point are preserved. A recognized identical repeat does not patch again.
- A reviewed plan is bound to **both** EXE and DLL hashes. Apply and restore use
  a per-target operation lock and reject stale files. This lock coordinates BSI
  operations, not every possible external writer; keep the emulator/updater and
  other editing tools stopped throughout the operation.
- Each apply creates a **new, non-clobbering**
  `HD-Player.exe.bsi-startup-dll-<uuid>.backup` and a small active
  `HD-Player.exe.bsi-startup-dll.json`. Both are read back before EXE commit.
  Neither an existing journal nor an arbitrary user `.bak` is overwritten.
- The EXE is staged through `QSaveFile` with **direct-write fallback disabled**,
  checked again immediately before atomic commit, then read back in full. A
  post-commit verification failure attempts a verified rollback. If rollback
  cannot be verified, the error explicitly says **do not launch; recovery is
  required** and retains the original backup/journal. No blanket power-loss,
  hostile-filesystem or malicious-concurrent-writer guarantee is made.
- **Restore original EXE** works even if the DLL is missing. It validates the
  recovery record and backup SHA-256, and only replaces the recorded patched
  image. An update or unrelated later edit blocks automatic restore rather than
  silently discarding that work. On success, the unique backup is retained and
  the active journal is archived beside it. A later apply uses a new UUID.
- If an interrupted apply left the EXE unchanged, Restore verifies the original
  and archives its prepared record without rewriting the EXE. A malformed
  journal/backup is never guessed or automatically overwritten. Preserve the
  files and recover manually from a known-good snapshot if needed.
- Restore uses that exact snapshot, not a heuristic section-removal/merge. Plan
  the order of your edits: restore the startup import before making later
  customizations, then reapply/review the desired changes. If later edits or an
  update already exist, automatic startup-DLL restore is refused; it does not
  silently merge or discard them.
- Backend sign-in/busy guards apply before read/write access. Writers reject a
  running HD-Player, run off the GUI thread and invalidate stale workspace,
  theme and resource analysis after EXE changes. This feature never auto-enables
  as a side effect of Settings/skin patching.

## Verification: keep three kinds of evidence separate

1. **Native parser/file tests:** malformed inputs, unsafe/reserved/Unicode DLL
   basenames, wrong architectures, absent section space, exact repeat behavior,
   stale reviews, locks, corrupt backups/journals, unrelated edits, missing-DLL
   recovery and injected pre-commit/read-back/rollback failures. Fault injection
   is compiled **only into the separate test executable**, never the app.
2. **Real reference data:** independently decoded by pinned
   `pefile==2024.8.26`, with exact original hashes, descriptors/section/overlay/
   entry-point preservation and independent PE checksum checks. All four pinned
   MSI/BlueStacks references are copied, patched and restored as **data only**.
   The originals and original archives must retain their hashes. They are never
   launched by these tests.
3. **Actual Windows loader:** CI compiles an original harmless host and regular
   DLLs with static CRTs. A DLL with **no exports must be refused**; a regular DLL
   with an initializer export must attach while that export is **not** called.
   Missing-DLL startup and a DLL that declines initialization are also tested in
   fresh processes. A separate recipient test copies only the patched EXE and
   regular DLL, **exits the patcher**, removes
   Qt/SDK/interpreter locations from PATH, then launches the EXE normally three
   times. This proves the no-proxy/no-recurring-runtime mechanism, not a
   particular supplied payload or a complete emulator installation.

An earlier empty-thunk prototype passed structural checks but **failed** the
actual Windows startup test. That approach was rejected, not merged. The current
format requires a real validated function import and has regression guards for
missing/null/wrong-case exports and changed-export repeat requests. This is why
structural tests alone are insufficient.

Check the **Regular DLL normal startup (Windows x64, no proxy/runtime)** job and
all other checks for the exact PR head. The existence of these tests is not a
claim that a pending run passed. The user identified the four previously attached
ZLag plugin source files as the **wrong DLL sources**; they are not evidence
about the intended payload, and the filename/export advice derived from them
has been withdrawn. A **separate compiled payload ZIP** was subsequently supplied:
its x64 `Z-Lag.dll` exports the native `ZLagImportAnchor` function. See the
[hash-pinned payload review and opt-in offline test](DLL-STARTUP-PAYLOAD-REVIEW.md)
for the exact inputs, automatic HWID-to-Firestore behavior, test design and
remaining verification boundary. Check actual job results before claiming the
supplied DLL was runtime-tested. Its legitimate authorization, visual effects
and behavior in a complete HD-Player installation remain **unverified** by the
offline probe. Generic defaults are not certification of a payload; no arbitrary
export-calling promise or payload all-clear follows from a loader test.

### Reproduce

Qt-free, sanitized parser checks on Linux:

```sh
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc \
  tests/dll-startup/test.cpp src/core/DllStartupImport.cpp -o /tmp/dll-startup-test
/tmp/dll-startup-test
```

Windows x64, with a Qt 6.5+ development SDK on the build machine only:

```powershell
cmake -S tests/dll-startup -B build-dll-startup -A x64 -DBSI_DLLSTARTUP_FILE_TESTS=ON
cmake --build build-dll-startup --config Release --parallel
ctest --test-dir build-dll-startup -C Release --output-on-failure
python -m pip install pefile==2024.8.26
python tests/dll_startup_native_test.py --engine build-dll-startup/Release/dll-startup-test.exe --file-test build-dll-startup/Release/dll-startup-file-test.exe --reference-dir /path/to/pinned-HD-Player-repo
```

The Python decoder, CMake, compiler, Qt SDK and the two native test adapters are
**development/test tools**, not recipient prerequisites. `BSI-Customizer
--selftest` also runs the production file-transaction checks without injection,
including its optional read-only player reference. Actual UI creation/interaction
is covered separately by `tests/dll_startup_ui_test.py` and
`tests/dll_startup_shell_test.py` with explicit mock services. The latter opens
the full production `Main.qml`, physically clicks **PATCHER → DLL patcher** and
the Collection shortcut, checks there is only one editor, and exercises scrolling,
inspection, cancellation/confirmation, busy and sign-out gates at 1440×930,
980×640 and 691×475. These UI fixtures never access an EXE/DLL or contact Firebase.
