# Zopfli resource encoder

Unmodified encoder sources from Google Zopfli `zopfli-1.0.3`, commit
`bd64b2f0553d4f1ef4e6627647c5d9fc8c71ffc0`. `UPSTREAM.json` records
SHA-256 hashes of every upstream file copied here. `LICENSE` is the upstream
Apache-2.0 `COPYING` file. Copyright/author notices remain in every source.
Only the zlib encoder and its dependencies are included; no upstream executable,
PNG optimizer, build script or downloaded binary is built or run.

BSI uses this statically linked encoder only as a bounded fallback when Qt's
ordinary level-9 zlib payload exceeds a resource slot. The input limit is 256 KiB,
iterations are fixed at 15, and the result must inflate through Qt to the exact
original bytes before use. A failed capacity check still refuses the whole plan;
compression does not authorize EXE growth, changing another resource, erasing
compiled data for storage or overwriting unowned bytes. No recipient interpreter,
extra runtime DLL, script, setup or system-DLL proxy is introduced.

The encoder does not affect shader math or GPU vendor/backend routing. Its
license must accompany Customizer binary/portable distributions.

## BSI build adapters and correction

The original files/hashes above are still byte-exact. BSI compiles a generated
copy of `lz77.c` with **two unaligned/aliasing typed match loads replaced by
`memcpy` loads of the same words**. `src/core/ZopfliMatch.cmake` applies this
small build-only correction only to the pinned original hash, and requires the
exact corrected SHA-256:
`557de25382d40718ef590ed8d71056d247f9b2843081d3f37845c64a6291fa95`.
No compression option or output semantics are changed; sanitizer checks are not
suppressed. This notice identifies the modified compiled file as required by
Apache-2.0. No original upstream source/license hash was relaxed or replaced.

Only upstream C translation units have malloc/realloc/free/exit calls remapped
to `src/core/BoundedZopfli.c` through `ZopfliAllocator.h`. The guard itself uses
the real CRT, a per-thread allocation list, a **64 MiB live-allocation cap** and
C-only setjmp recovery. Any allocation/budget/encoder-exit failure releases every
tracked block and returns failure without changing caller output. No jump crosses
a C++/Qt frame; the caller retains ordinary compression and must refuse a slot
that still does not fit. There is no subprocess/helper runtime. Isolated tests
inject every allocation-failure point and fatal exit, and test concurrent calls.
