// Qt-free regression test. Run from the repository root:
// g++ -std=c++17 -O2 tests/rpc_scan_optimization_test.cpp src/core/RpcIdScan.cpp -o /tmp/rpc-scan-test
// /tmp/rpc-scan-test
#include "../src/core/RpcIdScan.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>

namespace {
// Frozen pre-optimization implementation: differential tests pin every rule,
// including zero padding and substring (not just half-word) runs of 0/F.
bool originalCompilerConstant(std::uint64_t value)
{
    if ((value & (value + 1)) == 0 || (value & (value - 1)) == 0) return true;
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    const std::string hex(buffer, 16);
    if (hex.find("00000000") != std::string::npos || hex.find("ffffffff") != std::string::npos)
        return true;
    if (std::set<char>(hex.begin(), hex.end()).size() <= 3) return true;
    for (const int width : {2, 4, 8}) {
        bool uniform = true;
        for (int i = width; i < 16 && uniform; i += width)
            uniform = hex.compare(std::size_t(i), std::size_t(width), hex, 0, std::size_t(width)) == 0;
        if (uniform) return true;
    }
    return (value & 0x3FFFFFULL) == 0;
}

std::string encode(std::string_view bytes)
{
    const char *digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}

RpcIdScan::SignatureMatch originalSignature(std::string_view data, std::string_view pre,
                                           std::string_view post, int gap, int minContext)
{
    gap = gap > 0 ? gap : 8;
    for (int n = int(std::min(pre.size(), post.size())); n >= minContext; n -= 2) {
        RpcIdScan::SignatureMatch result;
        for (std::size_t pos = std::size_t(n); pos + gap + n <= data.size(); ++pos) {
            if (data.substr(pos - n, n) != pre.substr(pre.size() - n)) continue;
            if (data.substr(pos + gap, n) != post.substr(0, n)) continue;
            result.offsets.push_back(static_cast<std::int64_t>(pos));
            if (result.offsets.size() > 64) break;
        }
        if (!result.offsets.empty()) { result.contextUsed = n; return result; }
    }
    return {};
}

void compareSignature(std::string_view data, std::string_view pre, std::string_view post,
                      int gap = 8, int minContext = 2)
{
    const RpcIdScan::Signature sig{"test", encode(pre), encode(post), gap};
    const auto expected = originalSignature(data, pre, post, gap, minContext);
    const auto actual = RpcIdScan::findSignature(data, sig, minContext);
    assert(actual.contextUsed == expected.contextUsed);
    assert(actual.offsets == expected.offsets);
}
}

int main()
{
    std::mt19937_64 random(0x425349);
    const auto compare = [](std::uint64_t value) {
        assert(RpcIdScan::compilerConstant(value) == originalCompilerConstant(value));
    };
    for (int i = 0; i < 200000; ++i) compare(random());
    for (int bit = 0; bit < 64; ++bit) {
        compare(1ULL << bit); compare((1ULL << bit) - 1); compare(~(1ULL << bit));
    }
    for (std::uint64_t value : {0ULL, ~0ULL, 0x1234000000005678ULL, 0x12ffffffff345678ULL,
                                0x1234567812345678ULL, 0xabcdabcdabcdabcdULL,
                                0x1212121212121212ULL, 821361671729709086ULL}) compare(value);
    // Force zero/F runs, repeated words, low-bit-zero and few-distinct-digit cases.
    for (int i = 0; i < 10000; ++i) {
        const auto value = random();
        compare(value & ~0x3FFFFFULL);
        compare((value & 0xFFFFFFFFULL) * 0x100000001ULL);
        compare((value & 0xFFFFULL) * 0x1000100010001ULL);
        const int shift = int(random() % 9) * 4;
        compare(value & ~(0xFFFFFFFFULL << shift));
        compare(value | (0xFFFFFFFFULL << shift));
        std::uint64_t fewDigits = 0;
        for (int digit = 0; digit < 16; ++digit)
            fewDigits = (fewDigits << 4) | (random() % 3 + 4);
        compare(fewDigits);
    }
    compareSignature("", "ab", "cd");
    compareSignature("ab12345678cd", "ab", "cd"); // both file boundaries
    compareSignature("ab12345678c", "ab", "cd"); // truncated suffix
    compareSignature("ab12345678ef", "ab", "cd"); // prefix without matching suffix
    compareSignature("ab12345678cdab12345678cd", "ab", "cd"); // two valid hits
    compareSignature("ab12345678cd", "zzab", "cdzz"); // shrink context
    compareSignature(std::string(1024, 'a'), "aaaa", "aaaa", 1); // overlap + cap
    compareSignature(std::string(1024, '\0'), std::string(4, '\0'), std::string(4, '\0'));
    compareSignature("abc12345678def", "abc", "def"); // odd context lengths
    compareSignature("ab12345678cd", "ab", "cd", 0); // default gap
    compareSignature("ab12345678cd", "ab", "cd", 8, 0); // zero-length fallback
    for (int i = 0; i < 2000; ++i) {
        std::string data(256, '\0');
        for (char &c : data) c = char(random() % 8);
        // Exercise NUL and non-ASCII contexts too.
        const auto off = std::size_t(40 + random() % 160);
        const auto pre = data.substr(off - 16, 16), post = data.substr(off + 8, 16);
        compareSignature(data, pre, post);
        data[off - 16] ^= 1; data[off + 23] ^= 1;
        compareSignature(data, pre, post);
    }
    const std::string id = "821361671729709086";
    const std::string fixture = "discord client_id=" + id;
    const auto records = RpcIdScan::harvest(fixture);
    const auto pick = RpcIdScan::pickCandidate(records);
    assert(pick.index >= 0 && records[std::size_t(pick.index)].id == id);
    std::puts("PASS: numeric filtering, signature matching and known-ID discovery");
}
