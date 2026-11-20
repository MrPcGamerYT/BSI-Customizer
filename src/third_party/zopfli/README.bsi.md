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
