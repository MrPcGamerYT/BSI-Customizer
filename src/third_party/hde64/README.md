# HDE64 instruction decoder

Vendored from [MinHook v1.3.4](https://github.com/TsudaKageyu/minhook/tree/v1.3.4/src/hde).
Only the instruction decoder is used, not MinHook's hooking/runtime API.

Local changes: `hde64.c` is named `hde64.cpp` for this C++17-only build;
`hde64.h` uses the standard `<stdint.h>` available in supported compilers.
The upstream copyright notices and BSD license are preserved in `LICENSE.txt`.

`X64Prologue` bounds decoder input, restricts accepted entry instructions,
and rejects unsupported encodings/control flow. HDE64 is not itself a bounds-
checked API or a complete modern x86 instruction-set validator.
