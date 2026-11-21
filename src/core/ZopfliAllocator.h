#pragma once
// Force-included ONLY while compiling the unmodified upstream encoder. Include
// the CRT declaration before remapping calls, not its declarations/types. The
// guard implementation is compiled separately WITHOUT these macros.
#include <stdlib.h>
#include <stddef.h>
#if defined(_MSC_VER)
#define BSI_ZOPFLI_NORETURN __declspec(noreturn)
#else
#define BSI_ZOPFLI_NORETURN __attribute__((noreturn))
#endif
void *bsiZopfliMalloc(size_t size);
void *bsiZopfliRealloc(void *pointer, size_t size);
void bsiZopfliFree(void *pointer);
BSI_ZOPFLI_NORETURN void bsiZopfliExit(int code);
#define malloc bsiZopfliMalloc
#define realloc bsiZopfliRealloc
#define free bsiZopfliFree
#define exit bsiZopfliExit
