# Direct DLL startup: Pay Load Patcher review

**Decision: the direct-import mechanism is real, but the supplied patchers are not ready for automatic integration.** They are not embedded or run by BSI. This branch instead adds an original native C++ implementation with separate opt-in controls, protected backups and Windows loader tests; see [Regular DLL startup](DLL-STARTUP.md) for that implementation's scope and exact-head verification requirements. The existing RPC proxy is unchanged. The audit script below remains a developer diagnostic of the uploaded revisions, not an installer or an all-clear for the missing real payload.

## Does it work without a proxy?

Yes: all three supplied implementations append a writable, non-executable `.zimp` PE section, copy the existing import descriptors, add `zlag.dll!zlag_init`, and repoint the import directory. This makes the DLL a normal, required dependency of the **modified** EXE. It does not impersonate `version.dll`, inject into a running process, or require a separate launcher at each startup.

For a valid compatible DLL with all dependencies present, Windows normally loads it and invokes its DLL initialization before the executable entry point. **Resolving/importing `zlag_init` does not call `zlag_init`.** None of the supplied scripts adds a call to that function. Whether the intended functionality starts therefore depends on the actual DLL's initialization design. A DLL which only exports an initializer and does nothing during DLL initialization will be loaded but its initializer will not run automatically.

A DLL is not thereby converted into an EXE. Missing DLLs, missing exports, incompatible architecture or unresolved dependent libraries can stop the entire player from starting. The EXE grows and its Authenticode signature is invalidated; recomputing the PE checksum does not repair that signature. Player updates can replace the modified EXE. DLL initialization occurs under the Windows loader lock; heavy Qt/UI/network work there must not be assumed safe.

## Sources reviewed

The Python/JavaScript files and README were attached by the user, who identified the tool as their staff's `TOXICPCCITY/Pay-Load-Patcher-HD-Player`. The previously linked repository was not accessible, so no upstream revision or licensing claim is inferred from that link. The PowerShell implementation was supplied inline and received static review only.

| Attachment | SHA-256 |
|---|---|
| `Pay Load Patcher.py` | `9b5f05aa2897ae94f1a22b28af838f1ac53db35312dddc2b738b469d23ee6832` |
| `Pay Load Patcher.js` | `9de47ff88e6201347df7d342fb16159bd915b370959cad1f820d04449ee03916` |
| `README.md` | `dd3585db40e143f0e181e2e31a793735bfa20b5626e3594003781ffb312e2af3` |

The supplied README has responsible-use text but no explicit distribution licence. Resolve attribution/distribution permission before vendoring the staff tool. The third-party patchers, payload DLL and player binaries are **not bundled in this change**.

## What was actually tested

Executed the reviewed Python and JavaScript patchers with `--no-elevate`, against disposable files only. The DLLs used in the structural tests were minimal, synthetic PE data fixtures, **not runnable DLLs and not the user's `zlag.dll`**. Independent decoding/checks used `pefile==2024.8.26`, rather than trusting the patchers' own DLL-name-only verification.

Both implementations produced identical output for a valid ASCII synthetic fixture. Both preserved its original import, entry point and existing section bytes, added `zlag.dll!zlag_init`, produced a valid PE checksum, saved the original backup and made no change on an identical repeat. Both refused a missing DLL, missing export, wrong export case, mismatched architecture and insufficient new-section-header space without changing the target.

They were also run on temporary copies of all four pinned player references from `MrPcGamerYT/HD-Player@6e4dc894eb1b6956060b3f76d13fc438e5178d03`:

| Reference (package label, not a claim of newest version) | Original bytes | Patched bytes | Imports before → after | Python / JS structural result |
|---|---:|---:|---:|---|
| MSI 5.9.300.6315 | 10,068,960 | 10,070,016 | 40 → 41 | Both pass |
| BlueStacks 5.10.150.1016 | 11,991,040 | 11,992,064 | 41 → 42 | Both pass |
| MSI 5.22.75.6322 ("latest" package) | 23,858,608 | 23,860,224 | 54 → 55 | Both pass |
| BlueStacks 5.22.285.1013 ("latest" package) | 33,149,872 | 33,151,488 | 59 → 60 | Both pass |

For each successful reference-copy patch, the independent checks confirmed the exact additional DLL/function import, preservation of the original imports/functions and every existing section's raw bytes, unchanged entry point, valid checksum and unchanged original input file. Reference fingerprints are recorded in [HD-Player research](HD-PLAYER-RESEARCH.md#sample-sha-256-fingerprints).

**These uploaded-script results are structural file tests, not player-launch tests.** In this audit no Windows EXE or DLL was loaded/executed, no emulator was launched, and neither UAC nor PowerShell execution was tested. The new independent native implementation has a separate harmless Windows host/DLL test; it does not turn the structural table above into an actual payload/player-launch result. Neither kind of test establishes that the unavailable real `zlag.dll` initializes safely in HD-Player.

## Reproduced safety failures

These are findings, **not passing tests**:

| Finding | Python | JavaScript | Why integration must wait |
|---|---|---|---|
| Existing module, different requested export | Reports success without adding the requested import | Same | After importing `zlag_init`, requesting the also-exported `other_init` says "already patched" although the EXE still only imports `zlag_init`. |
| Existing explicit backup path | Overwrites the existing file | Same | `--backup` can destroy an earlier recovery copy instead of refusing it. |
| PE32+ payload without `IMAGE_FILE_DLL` | Accepts and modifies the target | Same | Same machine type and a named export do not establish that the supplied file is a DLL. |
| Export name present, function address zero | Accepts and modifies the target | Same | The export-name-only check does not prove the imported symbol resolves. |
| Non-ASCII DLL filename (`zlag-é.dll`) | Throws an uncaught encoding error before modification | Verification fails **after** modification; target stays changed | JavaScript's ASCII conversion can write a different import filename. No automatic rollback restores the target on failed verification. |

The inline PowerShell `Test-PEExport` uses `-eq` for export-name comparison, which is **case-insensitive**. Windows import-by-name resolution is case-sensitive. This check must use an exact comparison (for example `-ceq`) or it can accept `ZLAG_INIT` when the EXE will request `zlag_init`. This is a static code finding, not a PowerShell runtime-test result.

Additional static concerns shared by the supplied implementations:

- The target is overwritten in place; partial I/O or failed verification does not automatically restore the original. A backup alone is not transactional apply.
- Header/RVA/table/string bounds and export ordinals/function addresses need stronger validation for malformed input. Checking PE32+ and equal machine values alone is not a complete AMD64 EXE/DLL validation.
- "Already patched" and read-back verification examine DLL names, not the complete requested module/export relationship.
- The UAC parent reports success after launching an elevated child, without establishing the child's eventual patch result.
- There is no hash-bound, closed-player apply plan or coordinated recovery with BSI's other edits. Appending a section is distinct from BSI's fixed-slot Settings resource edits.

## Reproduce the review safely

[`scripts/Test-DllStartup-Patcher.py`](../scripts/Test-DllStartup-Patcher.py) audits these **exact uploaded revisions**. It requires Python 3.8+, Node.js 14+ and the independent test-only decoder:

```sh
python -m pip install pefile==2024.8.26
python scripts/Test-DllStartup-Patcher.py --source-dir /path/to/attached/files
```

Optional: test additional read-only player references and create a **new** JSON report:

```sh
python scripts/Test-DllStartup-Patcher.py \
  --source-dir /path/to/attached/files \
  --reference /path/to/HD-Player.exe \
  --json /path/to/new-review-report.json
```

Repeat `--reference` for multiple files. The audit snapshots the source bytes after matching the hashes above, always passes `--no-elevate`, patches only private temporary copies, never executes the PE fixtures/references and refuses to overwrite an existing JSON report. Source changes are refused pending review. Do not run with Python `-O`, which disables required assertions.

**This is not a security sandbox.** It executes the reviewed Python/JavaScript scripts with the current user's permissions; hash pinning prevents silently substituting unreviewed revisions. The real supplied payload is not needed for these limited format tests. Exit `1` is expected for these uploads because safety findings were reproduced; exit `0` would still not certify Windows runtime compatibility. Use `python tests/dll_startup_audit_test.py` for the audit's own dependency-free fixture/input-guard checks.

## Before supporting/bundling the supplied staff payload

The original native writer is separate from these scripts and requires an exact validated native function export but does not call it. Its bounded parsing, native transactions and harmless runtime test are described in [the feature guide](DLL-STARTUP.md). These remaining gates apply to claiming that the **specific staff DLL/tool** works, or bundling that third-party payload:

1. Obtain the actual `zlag.dll` source (preferably the source ZIP uploaded directly), its exported initializer, dependencies and intended initialization contract. The patcher attachments are **not** the DLL source.
2. Resolve the reproduced failures above, add bounded PE parsing, safe filename/export validation, exact module/export verification and non-clobbering recovery. Do not replace the existing RPC proxy as an incidental side effect.
3. Stage and independently verify output before apply, detect concurrent target changes, require explicit user consent and protect recovery on write/read-back failures. Never auto-enable a new required native dependency during an unrelated Settings edit.
4. Test a hardened implementation on Windows with a harmless startup DLL/host, explicitly proving DLL attachment versus exported-function invocation. Test PowerShell separately; Python/JS equivalence is not proof of its behavior.
5. Only then perform an authorized, disposable-installation HD-Player launch test with the actual payload, including startup, shutdown, missing-dependency failure, restore and player-update behavior. No all-clear or merge based solely on the structural table above.
