#include "BoundedZopfli.h"
#include "../third_party/zopfli/zlib_container.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

/* All jumps stay inside this C entry point and upstream C frames. No C++/Qt
   frame or destructor is crossed. Heap-owned context avoids setjmp's rule for
   modified non-volatile automatic locals. Per-thread contexts isolate callers.
   Keep vendor sources byte-exact; only their allocation/exit calls are remapped
   at compile time. Every allocation is tracked before upstream can see it. */
typedef struct Context Context;
typedef union Block Block;
union Block {
    struct { Block *previous, *next; Context *owner; size_t bytes; } h;
    long double alignment;
    void *pointerAlignment;
};
struct Context {
    jmp_buf failure;
    Block *head;
    size_t bytes, peak, allocations, failAt, limit;
    int failed, ok;
};
#if defined(_MSC_VER)
static __declspec(thread) Context *active;
#define BSI_NORETURN __declspec(noreturn)
#else
static __thread Context *active;
#define BSI_NORETURN __attribute__((noreturn))
#endif
BSI_NORETURN void bsiZopfliExit(int code)
{
    (void)code;
    /* An encoder call outside the guarded entry point is a programming error,
       never an OOM recovery path. No such call is made by the application. */
    if (!active) abort();
    active->failed = 1;
    longjmp(active->failure, 1);
}
static size_t checkedSize(size_t size, size_t previous)
{
    size_t bytes;
    if (!active) abort();
    if (!size) size = 1;
    if (size > (size_t)-1 - sizeof(Block)) bsiZopfliExit(-1);
    bytes = size + sizeof(Block);
    ++active->allocations;
    if (active->allocations > 1000000 || active->allocations == active->failAt
        || bytes > active->limit || active->bytes - previous > active->limit - bytes)
        bsiZopfliExit(-1);
    return bytes;
}
void *bsiZopfliMalloc(size_t size)
{
    const size_t bytes = checkedSize(size, 0);
    Block *block = (Block *)malloc(bytes);
    if (!block) bsiZopfliExit(-1);
    block->h.previous = NULL; block->h.next = active->head;
    block->h.owner = active; block->h.bytes = bytes;
    if (active->head) active->head->h.previous = block;
    active->head = block; active->bytes += bytes;
    if (active->bytes > active->peak) active->peak = active->bytes;
    return block + 1;
}
void *bsiZopfliRealloc(void *pointer, size_t size)
{
    Block *old, *replacement, *previous, *next;
    size_t oldBytes, bytes;
    if (!pointer) return bsiZopfliMalloc(size);
    old = (Block *)pointer - 1;
    if (!active || old->h.owner != active) abort();
    oldBytes = old->h.bytes; previous = old->h.previous; next = old->h.next;
    bytes = checkedSize(size, oldBytes);
    replacement = (Block *)realloc(old, bytes);
    /* Failure retains the old allocation/list; cleanup still owns everything. */
    if (!replacement) bsiZopfliExit(-1);
    if (previous) previous->h.next = replacement; else active->head = replacement;
    if (next) next->h.previous = replacement;
    replacement->h.bytes = bytes;
    active->bytes = active->bytes - oldBytes + bytes;
    if (active->bytes > active->peak) active->peak = active->bytes;
    return replacement + 1;
}
void bsiZopfliFree(void *pointer)
{
    Block *block;
    if (!pointer) return;
    block = (Block *)pointer - 1;
    if (!active || block->h.owner != active) abort();
    if (block->h.previous) block->h.previous->h.next = block->h.next; else active->head = block->h.next;
    if (block->h.next) block->h.next->h.previous = block->h.previous;
    active->bytes -= block->h.bytes;
    free(block);
}
static int encode(const unsigned char *input, size_t inputSize, unsigned char *output,
                  size_t capacity, size_t *outputSize, size_t failAt, size_t limit,
                  int forceExit, size_t *allocations, size_t *peak, size_t *remaining, int *failed)
{
    Context *context, *previous;
    int ok;
    if (outputSize) *outputSize = 0;
    if (!input || !inputSize || inputSize > 256 * 1024 || !output || !capacity || !outputSize) return 0;
    context = (Context *)calloc(1, sizeof(Context));
    if (!context) return 0;
    context->failAt = failAt; context->limit = limit;
    previous = active; active = context;
    if (setjmp(context->failure) == 0) {
        ZopfliOptions options;
        unsigned char *encoded = NULL;
        size_t size = 0;
        if (forceExit) bsiZopfliExit(-1);
        ZopfliInitOptions(&options); options.numiterations = 15;
        ZopfliZlibCompress(&options, input, inputSize, &encoded, &size);
        if (encoded && size >= 6 && size <= capacity) {
            memcpy(output, encoded, size); *outputSize = size; context->ok = 1;
        }
    }
    while (context->head) bsiZopfliFree(context->head + 1);
    ok = context->ok;
    if (allocations) *allocations = context->allocations;
    if (peak) *peak = context->peak;
    if (remaining) *remaining = context->bytes;
    if (failed) *failed = context->failed;
    active = previous; free(context);
    return ok;
}
int bsiBoundedZopfli(const unsigned char *input, size_t inputSize,
                     unsigned char *output, size_t capacity, size_t *outputSize)
{
    return encode(input, inputSize, output, capacity, outputSize, 0, 64 * 1024 * 1024,
                  0, NULL, NULL, NULL, NULL);
}
#ifdef BSI_ZOPFLI_TESTING
int bsiBoundedZopfliTest(const unsigned char *input, size_t inputSize,
                         unsigned char *output, size_t capacity, size_t *outputSize,
                         size_t failAt, size_t byteLimit, int forceExit, struct BsiZopfliStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    return encode(input, inputSize, output, capacity, outputSize, failAt, byteLimit, forceExit,
                  &stats->allocations, &stats->peakBytes, &stats->remainingBytes, &stats->failed);
}
#endif
