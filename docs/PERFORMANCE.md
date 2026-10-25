# Main-branch performance review

Baseline: `b5f68a6` (`main`, tag `v2.5.3`), reviewed 2026-10-25.
This review uses main, not the unmerged About changes in PR #55.
Follow-up changes below include the independently applied installer elevation fix.

## Baseline findings and priorities

1. **Avoid scans for hidden pages.** `Main.qml` constructs every page in its
   `StackLayout`. `ResourcesPage.qml` calls `autoScan()` at completion and on
   every EXE path change, without checking visibility. `UiPage.qml` also calls
   `reloadAll()` at completion and on path changes, then again when shown.
   Gate automatic work on visibility/authorization and defer it until the
   current backend operation has finished. Explicit Refresh must still work.
2. **Move remaining expensive reads off the GUI thread.** `Backend::layoutTweaks()`
   and `aboutPageState()` read/analyze the whole EXE synchronously. Layout analysis
   repeats discovery per tweak. `detectInstalls()` also performs DLL detection
   synchronously. Existing `runBackground()` keeps painting through a nested
   event loop but unconditionally emits every model's change signal afterward;
   Resources selection handlers can therefore run after unrelated operations.
   A proper asynchronous result/signal contract and targeted notifications would
   avoid both stalls and re-entry. Do not simply wrap every getter in the existing
   worker: resource selection already explicitly warns against that recursion.
3. **Reuse discovery within an operation.** `EmbeddedResources::allFiles()` does
   registered-pool discovery and full theme discovery. Theme discovery examines
   global zlib candidates and calls resource-link discovery for accepted candidates.
   Resource budget measurement invokes link discovery again. Reusing a validated
   resource index would help multiple features, but a write must always revalidate
   the current disk bytes. Any longer-lived cache needs explicit invalidation on
   writes/restores, path changes and external modifications—not just file size.
4. **Bound packed RPC scan cost without losing compatibility.**
   `RpcIdPatcher::scanPackedBlobs()` copies up to 16 MiB per magic offset, followed
   by more copies in `inflateLoose()`. Its limit counts successful inflations,
   not attempted headers. Thus a file full of false headers can do much more than
   60 attempts. Prefer validated record lengths and bounded streaming inflation;
   retain support for headerless streams and report incomplete scans explicitly.
   This remains unchanged pending Qt-backed regression coverage.
5. **Reduce per-candidate overhead in the Qt-free RPC engine.** Implemented below.

Also observed: the tag is `v2.5.3` while `CMakeLists.txt` declares `2.5.2`.
That can affect the updater's version comparison. The follow-up build declares
`2.6.1` so it has a distinct update identity.

## Implemented first step

- `RpcIdScan::compilerConstant()` now uses fixed-size hexadecimal digits and a
  16-bit digit mask instead of formatting an allocated string and building a
  `std::set` for each plausible number. The zero/F runs, distinct-digit threshold,
  repeated patterns and low-bit rule are unchanged.
- `findSignature()` searches for actual prefix occurrences with
  `std::string_view::find()` before comparing the suffix. It preserves overlapping
  matches, shrinking/odd contexts, file bounds, default gaps and the 65-hit cap.
- Detection scores, ambiguity refusal and all patch/write behavior are unchanged.

## Verification

The Qt-free test compares the optimized predicates with frozen versions of the
old algorithms: 200,000 seeded random integers plus crafted constants, repeated
words and zero/F runs; signature cases include boundaries, shrinking contexts,
NUL bytes, overlaps, ambiguous matches and seeded random buffers. It also checks
known-ID discovery. CI runs this test under AddressSanitizer/UndefinedBehaviorSanitizer.

```sh
g++ -std=c++17 -O1 -g -Wall -Wextra -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/rpc_scan_optimization_test.cpp src/core/RpcIdScan.cpp -o /tmp/rpc-scan-test
/tmp/rpc-scan-test

g++ -std=c++17 -O2 tests/rpc_scan_benchmark.cpp src/core/RpcIdScan.cpp -o /tmp/rpc-benchmark
/tmp/rpc-benchmark
```

Example single-run timings from this Linux sandbox (GCC 12.2, `-O2`), comparing
baseline engine source with optimized source using the same synthetic harness:

| Workload | Baseline | Optimized |
|---|---:|---:|
| Reject 200,000 random numeric constants | 166.92 ms | 12.72 ms |
| Relocate a signature near the end of an 8 MiB buffer | 25.70 ms | 0.52 ms |
| Full discovery in 1 MiB seeded noise plus a known text ID | 62.06 ms | 43.04 ms |

Each before/after workload produced the same checksum. Timings are illustrative,
not thresholds or expected Windows/real-player speedups. Run several times on
representative installations before claiming end-to-end improvements.
The full Qt application and Windows self-test were **not run locally**: this
sandbox has no Qt development SDK or CMake installation. The existing Windows CI
build/self-test remains necessary before merging or releasing.

## Resources follow-up (2.6.1)

- Removed all completion/visibility/path-change scan triggers. Only **Scan
  Resources** starts a user scan. Successful resource writes still refresh their
  existing list so the editor cannot retain obsolete offsets.
- Cache the QML metadata list once per scan instead of constructing every map in
  each property getter. Prepare bounded thumbnails in the worker (128 KiB per
  string, 8 MiB total, decoded entries at most 4 MiB); oversized pictures retain
  a format badge and their full selected preview/extraction remains available.
  Result delegates no longer call synchronous preview I/O while filtering.
- Normalize a metadata search index once, debounce typing by 150 ms and reuse
  delegates. Case-insensitive AND terms, quoted phrases and slash normalization
  make searches such as `sidebar qml` useful. Search does not read resource contents.
- Notify Resources only when its snapshot changes—not after every unrelated
  background action, which used to reselect/re-measure a row and discard edits.
- Existing patching engines still re-read/revalidate on write; no cached search
  result is used to authorize a write.

Search matching tests: `node tests/resource_search_test.mjs` passes. On this
sandbox, 100 filters over a 20,000-entry synthetic list took approximately 70 ms
in Node (not an end-to-end Qt/Windows measurement).

`tests/resources_page_runtime_test.py` loads the real Resources QML with a
counting mock backend and 12,000 entries. It checks manual-only scans, filtering
without preview/budget calls, correct original indices and preservation of
unsaved code. The Linux CI job installs PySide6 6.8.3 and the required graphics
libraries to run it. Local execution initially could not start because this
sandbox lacks `libGL.so.1` and related Qt system libraries; do not count Python
syntax checking as a passing runtime test. New C++ regression cases cover the
metadata/thumbnail snapshot, targeted notifications, x86 installer verification
and refusal to copy over the app with a same-architecture installer. These need
the Windows build/self-test before release.

## Test-branch integration note

During testing, main advanced to `a193f43` (PR #55, version 2.6.0). It was
merged into this branch without switching branches. The next build identity is
therefore 2.6.1. Manual scans and metadata-only filtering are unchanged. The
installer keeps PE validation: valid x86 loaders pass, but merely adding an Inno
marker to a contradictory or truncated executable does not bypass verification.
The CLI installer verdict now calls the actual staged-file verifier.
