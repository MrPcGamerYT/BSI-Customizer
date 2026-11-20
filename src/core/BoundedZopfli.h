#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Fixed 15-iteration, <=256 KiB input, <=64 MiB live allocation budget. Returns
// zero on allocation/budget/fatal-encoder failure or insufficient output space,
// leaving output bytes untouched. No allocation owned by the encoder escapes.
int bsiBoundedZopfli(const unsigned char *input, size_t inputSize,
                     unsigned char *output, size_t capacity, size_t *outputSize);
#ifdef BSI_ZOPFLI_TESTING
struct BsiZopfliStats { size_t allocations, peakBytes, remainingBytes; int failed; };
int bsiBoundedZopfliTest(const unsigned char *input, size_t inputSize,
                         unsigned char *output, size_t capacity, size_t *outputSize,
                         size_t failAllocation, size_t byteLimit, int forceExit,
                         struct BsiZopfliStats *stats);
#endif
#ifdef __cplusplus
}
#endif
