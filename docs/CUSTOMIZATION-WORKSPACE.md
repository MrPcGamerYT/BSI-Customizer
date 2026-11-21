# Customization Workspace — 2.7

## Workflow

1. Select the exact emulator EXE. Close all HD-Player instances before writes.
2. Load the target Theme. Edit colors or import a portable profile.
3. Use **Resources → Scan Resources** explicitly for resource replacements. Neither opening Workspace/Resources nor importing a profile scans the pools.
4. Inspect compatibility/capacity, queue the palette and/or exact logical resource paths, then run **Dry Run**.
5. Review the operations, warnings, original/output SHA-256 and confirmation dialog. Apply only the reviewed plan.
6. Keep the recovery snapshot. Restore is blocked if the canonical target or post-patch hash differs.

## Requested feature coverage

| # | Feature | Implementation and boundary |
|---|---|---|
| 1 | Verified replacement | Shared resource writer verifies ownership, aliases and mapped storage. Larger decoded resources are allowed when encoded storage fits. Unknown mappings are never forced. |
| 2 | Capacity inspector | Original, required encoded and available in-place sizes; mapping, relocation-needed and original/output fingerprints. Relocation remains subject to Dry Run, not a guaranteed capacity promise. |
| 3 | Preview / dry run | Isolated temporary EXE simulation; no target, `.bak` or emulator cache changes. Deterministic resource timestamp ticks keep reviewed and committed bytes identical. |
| 4 | Unified profiles | Versioned JSON carries palette colors, boot hold, About text/links/sections and GIF RPC settings. Capture page settings explicitly, then export from Workspace. No credentials, resource offsets or embedded replacement binaries. |
| 5 | Theme mock | Original/staged sidebar, surfaces, action button, sample text and switch. Common aliases resolve with cycle protection; unsupported expressions use fallback colors. It is not an emulator renderer. |
| 6 | History / recovery | AppData `patch-history/<id>.bin` plus JSON contains hashes, canonical target, time and summarized operations. Latest 200 journals are listed; older editor operations still use `.bak`. No automatic cross-version restore. |
| 7 | Diagnostics | Export a readable `.txt` report from Theme or Workspace, without PowerShell or sharing the EXE. Control bytes are escaped. Review paths and resource snippets before sharing. |
| 8 | Compatibility | Explicit inspection reports Qt imports, theme mapping/ambiguity, capacity and About availability, with refusal reasons. Verified mapping is not proof of every emulator feature's runtime compatibility. |
| 9 | Comparison | Original/staged palette mock and logical image/text resource comparison. Text comparison is bounded to 16 KiB previews, not an unrestricted binary diff. |
| 10 | Resource browser | Manual scans only; picture/SVG/code/binary/font/modified filters and `min:10kb max:2mb` metadata search. Modified compares with an existing `.bak` during the scan. |
| 11 | Image preparation | Explicit fit or center crop, quality selection, output dimensions/bytes/alpha and original/output previews. Saves a separate file; staging/apply is another explicit action. JPEG that would lose alpha is refused. Static preparation does not preserve animation. |
| 12 | Conflicts | Duplicate logical paths, shared payload aliases, duplicate palette edits and raw-theme/palette collisions are refused during preflight. |
| 13 | Guided batch | Queue, isolated preflight, reviewed output hash, confirmation and one verified EXE replacement. A Workspace writer lock prevents concurrent Workspace writers; external applications do not participate in that advisory lock. Recovery is preserved if verification fails after replacement. |
| 14 | Profile portability | Settings are portable, offsets are not. Imports warn about different source hashes and unmatched palette names. Load the target theme before importing; capture/import does not deploy Boot/About/RPC files. |
| 15 | Boot assistant | MP4 metadata inspection plus muted looping fit/crop playback in the customizer's decoder. Decoder errors are displayed. Target emulator decoding still needs verification; no automatic conversion or bundled encoder. Profiles store hold duration, not local video content. |
| 16 | GIF RPC diagnostics | Reports profile validation, configured proxy file and observable Discord IPC reachability separately. Does not submit an activity or claim Discord accepted/animated an image merely because a URL or pipe is valid. |
| 17 | Update protection | Source hashes are checked on staging/write/apply; changes invalidate offsets and reviewed plans. Manual Resources rescan/reload is required after updates. |
| 18 | Branding workspace | Filter supported logo/splash/icon/banner names, preview original/replacement, queue assets and navigate to About text/links. EXE resource changes use Workspace recovery; existing About/DLL editors retain their separate backup/restore mechanisms. |

## Safety boundaries

- A batch commits **one EXE only**. Boot Assets, About DLLs/RCCs and GIF RPC configuration/proxy operations are separate. No cross-file atomicity or automatic rollback is promised.
- Ordinary resource replacement cannot grow the EXE. Theme relocation is only attempted through the writer's supported mapped-storage checks; there is no universal force-replace mode.
- Raw compiled cache replacement is refused. Source edits invalidate supported compiled units and player disk caches instead.
- Workspace limits: EXE/input/output 256 MiB, resource 32 MiB, aggregate decoded replacements 64 MiB, 128 operations. Profiles 256 KiB. Images at most 16 megapixels, with bounded source size and crop-before-scale to avoid oversized intermediates.
- History contains original EXE bytes and installation paths. Keep it private. Journals/snapshots must not be linked files or reside under linked/junction directories.
- Hash checks plus atomic replacement protect the intended workflow; they are not a cross-process compare-and-swap against arbitrary external writers. Do not update the emulator or run other patch tools during Apply/Restore.
- A commit or final verification can fail after replacement. Read the result carefully: the preserved journal can recover the EXE only when its current hash matches the recorded post-patch hash.
- The separately reported MSI layout with an unverified active resource mapping remains read-only pending its diagnostic report. Recognizing colors is not enough to establish the active Qt resource tree.

## Validation

Local checks include native Qt Core/Gui synthetic PE/QRC tests for inspection, portable profiles, isolated/deterministic dry runs, named-color canonicalization, conflicts, source/output hashes, concurrent Workspace writers, commit/history/restore, safely fitting decoded growth, reports and image preparation. Metadata search has a 20,000-row regression/performance test. New Workspace/Theme QML is type-checked against real Qt 6.8 metadata; existing About QML is also syntax-checked.

These checks are not Windows installer or live-emulator proof. The full Windows build, desktop QML interaction and actual emulator/Discord playback still require execution in the supported environment. GitHub Actions was blocked by the account billing/spending limit at the last check; no 2.7 installer, merged PR or public release is implied.

Useful checks:

```sh
node tests/resource_search_test.mjs
python tests/workspace_qml_lint_test.py  # PySide6 6.8.3 test toolchain
cmake -P cmake/tests/CollectQtNoticesTest.cmake
```

The native application self-test integrates `tests/workspace_core_test.cpp`. Qt Multimedia is an explicit build/deployment dependency, including its module notices; previews use the platform decoder, not a hidden conversion service.
