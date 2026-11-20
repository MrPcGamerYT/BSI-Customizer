#include "BoundedZopfli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void require(int yes, const char *message)
{
    if (!yes) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
int main(void)
{
    const unsigned char input[] = "safe bounded encoding";
    unsigned char output[1024], expected[1024];
    size_t size = 0, allocations, fail;
    struct BsiZopfliStats stats;
    memset(output, 0xa5, sizeof(output)); memcpy(expected, output, sizeof(output));
    require(bsiBoundedZopfliTest(input, sizeof(input) - 1, output, sizeof(output), &size, 0,
                                64 * 1024 * 1024, 0, &stats), "Guarded encoder must work normally");
    require(size >= 6 && stats.allocations && stats.peakBytes && !stats.remainingBytes && !stats.failed,
            "All successful-call allocations must be owned and released");
    allocations = stats.allocations;
    for (fail = 1; fail <= allocations; ++fail) {
        memset(output, 0xa5, sizeof(output)); size = 123;
        require(!bsiBoundedZopfliTest(input, sizeof(input) - 1, output, sizeof(output), &size, fail,
                                     64 * 1024 * 1024, 0, &stats), "Every injected malloc/realloc failure must return, not exit/crash");
        require(!size && !stats.remainingBytes && stats.failed && stats.allocations == fail
                    && !memcmp(output, expected, sizeof(output)), "Failed allocations leak nothing and leave caller output untouched");
    }
    require(!bsiBoundedZopfliTest(input, sizeof(input) - 1, output, sizeof(output), &size, 0, 1024, 0, &stats)
                && !stats.remainingBytes && stats.failed && !size, "Memory budget failure must be recoverable");
    require(!bsiBoundedZopfliTest(input, sizeof(input) - 1, output, sizeof(output), &size, 0,
                                 64 * 1024 * 1024, 1, &stats) && stats.failed && !stats.remainingBytes,
            "An upstream fatal exit must be contained in C");
    memset(output, 0xa5, sizeof(output));
    require(!bsiBoundedZopfliTest(input, sizeof(input) - 1, output, 1, &size, 0,
                                 64 * 1024 * 1024, 0, &stats) && !size && !stats.remainingBytes
                && !memcmp(output, expected, sizeof(output)), "Insufficient output space must never partially publish bytes");
    require(!bsiBoundedZopfli(input, 256 * 1024 + 1, output, sizeof(output), &size), "Input cap is enforced inside the C guard too");
    require(bsiBoundedZopfli(input, sizeof(input) - 1, output, sizeof(output), &size), "A failed call cannot poison the next call/TLS context");
    printf("PASS: all %lu encoder allocation-failure points, fatal exit, memory/output/input limits, unchanged failure output and zero remaining allocations\n", (unsigned long)allocations);
    return 0;
}
